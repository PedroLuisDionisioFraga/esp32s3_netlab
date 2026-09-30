#include "wifi_bridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "notification_manager.h"
#include "sdkconfig.h"
#include "wifi_bridge_priv.h"

/* Wait a little between attempts so an absent router does not spin the radio. */
#define RECONNECT_DELAY_US (2 * 1000 * 1000)
/* With the setup network open, retry rarely: every attempt scans off-channel and stutters the phone's link. */
#define RECONNECT_DELAY_AP_US (15 * 1000 * 1000)
/* Keep the setup network a little after going online so the phone can read the "connected" result. */
#define AP_LINGER_US         (15 * 1000 * 1000)
#define PROVISION_TIMEOUT_US (25 * 1000 * 1000)
#define FALLBACK_US          ((int64_t)CONFIG_WIFI_BRIDGE_FALLBACK_SECONDS * 1000 * 1000)

#define SCAN_RECORDS_MAX 30
#define SSID_MAX_LEN     32
#define PASSWORD_MAX_LEN 63

_Static_assert(sizeof(CONFIG_WIFI_BRIDGE_SETUP_AP_SSID) >= 2 && sizeof(CONFIG_WIFI_BRIDGE_SETUP_AP_SSID) <= 33,
               "Setup network name must be 1 to 32 characters");
_Static_assert(sizeof(CONFIG_WIFI_BRIDGE_SETUP_AP_PASSWORD) == 1 ||
                 (sizeof(CONFIG_WIFI_BRIDGE_SETUP_AP_PASSWORD) >= 9 &&
                  sizeof(CONFIG_WIFI_BRIDGE_SETUP_AP_PASSWORD) <= 64),
               "Setup network password must be empty (open) or 8 to 63 characters");

static const char *TAG = "wifi_bridge";

static SemaphoreHandle_t s_lock; /* guards the state below; events, timers and HTTP handlers all touch it */
static esp_netif_t *s_sta_netif;
static esp_netif_t *s_ap_netif;
static esp_timer_handle_t s_reconnect_timer;
static esp_timer_handle_t s_fallback_timer;
static esp_timer_handle_t s_ap_stop_timer;
static esp_timer_handle_t s_prov_timer;
static esp_timer_handle_t s_restart_timer;

static EventGroupHandle_t s_status_events; /* optional: the LED worker waits on it */
static int s_last_state = -1;

static volatile bool s_online;
static volatile bool s_ap_active;
static bool s_sta_failed; /* the last attempt to reach the saved network failed */
static int s_ap_clients;
static char s_ap_ssid[SSID_MAX_LEN + 1];
static char s_ap_ip[16];
static char s_ap_uri[32]; /* DHCP option 114 keeps the pointer: it must outlive the DHCP server */

static bool s_has_saved;
static char s_saved_ssid[SSID_MAX_LEN + 1];
static char s_saved_pass[PASSWORD_MAX_LEN + 1];

static wifi_bridge_prov_state_t s_prov_state;
static char s_prov_ssid[SSID_MAX_LEN + 1];
static char s_prov_pass[PASSWORD_MAX_LEN + 1];
static char s_prov_reason[16];

static inline void lock(void)
{
  xSemaphoreTake(s_lock, portMAX_DELAY);
}

static inline void unlock(void)
{
  xSemaphoreGive(s_lock);
}

/* ---------- state reporting ---------- */

static wifi_bridge_state_t derive_state_locked(void)
{
  if (s_online)
    return WIFI_BRIDGE_STATE_ONLINE;

  if (s_prov_state == WIFI_BRIDGE_PROV_CONNECTING)
    return WIFI_BRIDGE_STATE_CONNECTING;

  if (s_ap_active)
    return WIFI_BRIDGE_STATE_SETUP;

  return s_sta_failed ? WIFI_BRIDGE_STATE_OFFLINE : WIFI_BRIDGE_STATE_CONNECTING;
}

static EventBits_t state_event_bit(wifi_bridge_state_t state)
{
  switch (state)
  {
    case WIFI_BRIDGE_STATE_SETUP:
      return NOTIF_EVT_CONN_SETUP;
    case WIFI_BRIDGE_STATE_CONNECTING:
      return NOTIF_EVT_CONN_CONNECTING;
    case WIFI_BRIDGE_STATE_ONLINE:
      return NOTIF_EVT_CONN_ONLINE;
    case WIFI_BRIDGE_STATE_OFFLINE:
      return NOTIF_EVT_CONN_OFFLINE;
  }
  return 0;
}

/* Every caller holds s_lock, so the clear + set pair cannot interleave with another publisher. */
static void publish_state_locked(void)
{
  int state = (int)derive_state_locked();
  if (state == s_last_state)
    return;

  s_last_state = state;
  if (s_status_events)
  {
    xEventGroupClearBits(s_status_events, NOTIF_EVT_CONN_ALL);
    xEventGroupSetBits(s_status_events, state_event_bit((wifi_bridge_state_t)state));
  }
}

static void publish_state(void)
{
  lock();
  publish_state_locked();
  unlock();
}

/* ---------- station ---------- */

static esp_err_t apply_sta_config(const char *ssid, const char *password)
{
  wifi_config_t cfg = {0};
  size_t ssid_len = strlen(ssid);
  memcpy(cfg.sta.ssid, ssid, ssid_len < sizeof(cfg.sta.ssid) ? ssid_len : sizeof(cfg.sta.ssid));

  if (password && password[0])
  {
    strlcpy((char *)cfg.sta.password, password, sizeof(cfg.sta.password));
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
  }
  else
  {
    cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
  }
  cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
  /* A hidden SSID is joined by name (directed probe). Scanning every channel and
   * sorting by signal picks the strongest AP when several share the SSID. */
  cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
  cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
  return esp_wifi_set_config(WIFI_IF_STA, &cfg);
}

static const char *failure_reason(uint8_t reason)
{
  switch (reason)
  {
    case WIFI_REASON_NO_AP_FOUND:
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
      return "not_found";
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_AUTH_EXPIRE:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
      return "wrong_password";
    default:
      return "failed";
  }
}

/* ---------- setup network (soft AP) ---------- */

static esp_err_t ap_configure(void)
{
  wifi_config_t cfg = {0};
  const size_t ssid_len = strlen(CONFIG_WIFI_BRIDGE_SETUP_AP_SSID);
  memcpy(cfg.ap.ssid, CONFIG_WIFI_BRIDGE_SETUP_AP_SSID, ssid_len); /* up to 32 bytes, no terminator needed */
  cfg.ap.ssid_len = (uint8_t)ssid_len;
  cfg.ap.channel = 1; /* moves to the router's channel as soon as the station joins it */
  cfg.ap.max_connection = CONFIG_WIFI_BRIDGE_SETUP_AP_MAX_CLIENTS;
  if (CONFIG_WIFI_BRIDGE_SETUP_AP_PASSWORD[0] != '\0')
  {
    strlcpy((char *)cfg.ap.password, CONFIG_WIFI_BRIDGE_SETUP_AP_PASSWORD, sizeof(cfg.ap.password));
    cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
  }
  else
  {
    cfg.ap.authmode = WIFI_AUTH_OPEN;
  }

  lock();
  strlcpy(s_ap_ssid, CONFIG_WIFI_BRIDGE_SETUP_AP_SSID, sizeof(s_ap_ssid));
  unlock();
  return esp_wifi_set_config(WIFI_IF_AP, &cfg);
}

/* Runs once the soft-AP interface is up: advertise the portal and start answering DNS. */
static void ap_services_start(void)
{
  esp_netif_ip_info_t ip_info;
  if (esp_netif_get_ip_info(s_ap_netif, &ip_info) != ESP_OK)
  {
    ESP_LOGW(TAG, "Setup network has no IP address yet");
    return;
  }

  lock();
  snprintf(s_ap_ip, sizeof(s_ap_ip), IPSTR, IP2STR(&ip_info.ip));
  snprintf(s_ap_uri, sizeof(s_ap_uri), "http://%s", s_ap_ip);
  unlock();

  /* DHCP option 114 tells modern phones where the captive portal is. The DHCP server
   * has to be stopped while an option is set. */
  esp_netif_dhcps_stop(s_ap_netif); /* already stopped is fine */
  esp_netif_dhcps_option(s_ap_netif, ESP_NETIF_OP_SET, ESP_NETIF_CAPTIVEPORTAL_URI, s_ap_uri, strlen(s_ap_uri));
  esp_netif_dhcps_start(s_ap_netif);

  if (dns_catch_all_start(ip_info.ip.addr) != ESP_OK)
  {
    ESP_LOGW(TAG, "Captive-portal DNS did not start: phones will not open the setup page by themselves");
  }
  ESP_LOGW(TAG, "Setup network '%s' is open: join it and go to http://%s/", s_ap_ssid, s_ap_ip);
}

static esp_err_t ap_enable(void)
{
  if (s_ap_active)
    return ESP_OK;

  ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_APSTA), TAG, "enable AP+STA mode");
  ESP_RETURN_ON_ERROR(ap_configure(), TAG, "configure setup network");

  lock();
  s_ap_active = true;
  s_ap_clients = 0;
  publish_state_locked();
  unlock();
  return ESP_OK;
}

static void ap_disable(void)
{
  if (!s_ap_active)
    return;

  ESP_LOGI(TAG, "Closing the setup network");
  lock();
  s_ap_active = false;
  s_ap_clients = 0;
  s_ap_ip[0] = '\0';
  publish_state_locked();
  unlock();
  esp_wifi_set_mode(WIFI_MODE_STA);
}

/* ---------- provisioning ---------- */

static void prov_finish_success(void)
{
  esp_timer_stop(s_prov_timer);

  char ssid[SSID_MAX_LEN + 1];
  char pass[PASSWORD_MAX_LEN + 1];
  lock();
  strlcpy(s_saved_ssid, s_prov_ssid, sizeof(s_saved_ssid));
  strlcpy(s_saved_pass, s_prov_pass, sizeof(s_saved_pass));
  strlcpy(ssid, s_saved_ssid, sizeof(ssid));
  strlcpy(pass, s_saved_pass, sizeof(pass));
  s_has_saved = true;
  s_prov_state = WIFI_BRIDGE_PROV_SUCCESS;
  s_prov_reason[0] = '\0';
  memset(s_prov_pass, 0, sizeof(s_prov_pass));
  unlock();

  if (wifi_bridge_nvs_save(ssid, pass) != ESP_OK)
  {
    ESP_LOGE(TAG, "Connected, but the network could not be saved: it will be forgotten at the next reboot");
  }
  else
  {
    ESP_LOGI(TAG, "Saved Wi-Fi network '%s'", ssid);
  }
  memset(pass, 0, sizeof(pass));
}

static void prov_finish_failed(const char *reason)
{
  esp_timer_stop(s_prov_timer);

  lock();
  s_prov_state = WIFI_BRIDGE_PROV_FAILED;
  strlcpy(s_prov_reason, reason, sizeof(s_prov_reason));
  memset(s_prov_pass, 0, sizeof(s_prov_pass));
  bool restore = s_has_saved;
  char ssid[SSID_MAX_LEN + 1];
  char pass[PASSWORD_MAX_LEN + 1];
  strlcpy(ssid, s_saved_ssid, sizeof(ssid));
  strlcpy(pass, s_saved_pass, sizeof(pass));
  publish_state_locked();
  unlock();

  ESP_LOGW(TAG, "Connection attempt failed: %s", reason);
  esp_wifi_disconnect(); /* stop a still-running attempt (the timeout case) */

  if (restore)
  {
    /* Go back to the network that was saved before this attempt. */
    apply_sta_config(ssid, pass);
    esp_timer_stop(s_reconnect_timer);
    esp_timer_start_once(s_reconnect_timer, RECONNECT_DELAY_AP_US);
  }
  memset(pass, 0, sizeof(pass));
}

/* ---------- timers ---------- */

static void reconnect_timer_cb(void *arg)
{
  (void)arg;

  lock();
  bool provisioning = (s_prov_state == WIFI_BRIDGE_PROV_CONNECTING);
  int clients = s_ap_clients;
  unlock();

  if (provisioning)
    return;

  if (clients > 0)
  {
    /* Someone is on the setup network: leave the radio alone so the page stays responsive. */
    esp_timer_start_once(s_reconnect_timer, RECONNECT_DELAY_AP_US);
    return;
  }

  esp_err_t err = esp_wifi_connect();
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "esp_wifi_connect: %s", esp_err_to_name(err));
  }
}

static void fallback_timer_cb(void *arg)
{
  (void)arg;
  if (s_online || s_ap_active)
    return;

  ESP_LOGW(TAG, "Not connected for %d s: opening the setup network", CONFIG_WIFI_BRIDGE_FALLBACK_SECONDS);
  if (ap_enable() != ESP_OK)
  {
    ESP_LOGE(TAG, "Could not open the setup network");
  }
}

static void ap_stop_timer_cb(void *arg)
{
  (void)arg;
  if (s_online)
  {
    ap_disable();
  }
}

static void prov_timer_cb(void *arg)
{
  (void)arg;
  prov_finish_failed("timeout");
}

static void restart_timer_cb(void *arg)
{
  (void)arg;
  esp_restart();
}

/* ---------- events ---------- */

static void on_sta_disconnected(const wifi_event_sta_disconnected_t *event)
{
  lock();
  bool provisioning = (s_prov_state == WIFI_BRIDGE_PROV_CONNECTING);
  unlock();

  if (provisioning)
  {
    if (event->reason == WIFI_REASON_ASSOC_LEAVE)
    {
      return; /* that is our own esp_wifi_disconnect() before the attempt */
    }
    prov_finish_failed(failure_reason(event->reason));
    return;
  }

  ESP_LOGW(TAG, "Disconnected (reason %d), retrying", event->reason);
  lock();
  s_online = false;
  s_sta_failed = true;
  bool has_saved = s_has_saved;
  publish_state_locked();
  unlock();

  if (!has_saved)
    return;

  esp_timer_stop(s_reconnect_timer);
  esp_timer_start_once(s_reconnect_timer, s_ap_active ? RECONNECT_DELAY_AP_US : RECONNECT_DELAY_US);
  if (!s_ap_active && !esp_timer_is_active(s_fallback_timer))
  {
    esp_timer_start_once(s_fallback_timer, FALLBACK_US);
  }
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
  (void)arg;
  (void)base;

  switch (id)
  {
    case WIFI_EVENT_STA_START:
      if (s_has_saved)
      {
        esp_wifi_connect();
      }
      break;
    case WIFI_EVENT_STA_DISCONNECTED:
      on_sta_disconnected(data);
      break;
    case WIFI_EVENT_AP_START:
      ap_services_start();
      break;
    case WIFI_EVENT_AP_STOP:
      dns_catch_all_stop();
      break;
    case WIFI_EVENT_AP_STACONNECTED:
      lock();
      s_ap_clients++;
      unlock();
      break;
    case WIFI_EVENT_AP_STADISCONNECTED:
      lock();
      if (s_ap_clients > 0)
      {
        s_ap_clients--;
      }
      unlock();
      break;
    default:
      break;
  }
}

static void on_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
  (void)arg;
  (void)base;

  if (id == IP_EVENT_STA_GOT_IP)
  {
    const ip_event_got_ip_t *event = data;
    ESP_LOGI(TAG, "Got IP " IPSTR, IP2STR(&event->ip_info.ip));

    lock();
    bool provisioning = (s_prov_state == WIFI_BRIDGE_PROV_CONNECTING);
    unlock();
    if (provisioning)
    {
      prov_finish_success();
    }

    lock();
    s_online = true;
    s_sta_failed = false;
    publish_state_locked();
    unlock();

    esp_timer_stop(s_fallback_timer);
    if (s_ap_active)
    {
      esp_timer_stop(s_ap_stop_timer);
      esp_timer_start_once(s_ap_stop_timer, AP_LINGER_US);
    }
  }
  else if (id == IP_EVENT_STA_LOST_IP)
  {
    ESP_LOGW(TAG, "Lost IP");
    lock();
    s_online = false;
    s_sta_failed = true;
    publish_state_locked();
    unlock();
  }
}

/* ---------- public API ---------- */

esp_err_t wifi_bridge_start(const wifi_bridge_config_t *cfg)
{
  ESP_RETURN_ON_FALSE(!s_lock, ESP_ERR_INVALID_STATE, TAG, "already started");
  s_lock = xSemaphoreCreateMutex();
  ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_NO_MEM, TAG, "no memory for the Wi-Fi lock");

  if (cfg)
    s_status_events = cfg->status_events;

  s_sta_netif = esp_netif_create_default_wifi_sta();
  s_ap_netif = esp_netif_create_default_wifi_ap();
  ESP_RETURN_ON_FALSE(s_sta_netif && s_ap_netif, ESP_FAIL, TAG, "create Wi-Fi network interfaces");

  wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_RETURN_ON_ERROR(esp_wifi_init(&init_cfg), TAG, "esp_wifi_init");
  /* Our own NVS entry is the single source of truth: keep the driver from keeping a second copy. */
  ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "set Wi-Fi storage");

  const struct
  {
    esp_timer_handle_t *handle;
    esp_timer_cb_t callback;
    const char *name;
  } timers[] = {
    {&s_reconnect_timer, reconnect_timer_cb, "wifi_reconnect"},
    {&s_fallback_timer, fallback_timer_cb, "wifi_fallback"},
    {&s_ap_stop_timer, ap_stop_timer_cb, "wifi_ap_stop"},
    {&s_prov_timer, prov_timer_cb, "wifi_prov"},
    {&s_restart_timer, restart_timer_cb, "wifi_restart"},
  };
  for (size_t i = 0; i < sizeof(timers) / sizeof(timers[0]); i++)
  {
    const esp_timer_create_args_t args = {.callback = timers[i].callback, .name = timers[i].name};
    ESP_RETURN_ON_ERROR(esp_timer_create(&args, timers[i].handle), TAG, "create timer %s", timers[i].name);
  }

  ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL),
                      TAG,
                      "register Wi-Fi handler");
  ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip_event, NULL),
                      TAG,
                      "register got-IP handler");
  ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, on_ip_event, NULL),
                      TAG,
                      "register lost-IP handler");

  s_has_saved =
    (wifi_bridge_nvs_load(s_saved_ssid, sizeof(s_saved_ssid), s_saved_pass, sizeof(s_saved_pass)) == ESP_OK);

  if (s_has_saved)
  {
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set STA mode");
    ESP_RETURN_ON_ERROR(apply_sta_config(s_saved_ssid, s_saved_pass), TAG, "set STA config");
    ESP_LOGI(TAG, "Joining saved network '%s'", s_saved_ssid);
  }
  else
  {
    ESP_LOGW(TAG, "No saved network: starting the setup network");
    ESP_RETURN_ON_ERROR(ap_enable(), TAG, "open the setup network");
  }

  ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "esp_wifi_start");
  ESP_RETURN_ON_ERROR(esp_wifi_set_ps(WIFI_PS_NONE), TAG, "disable Wi-Fi power save");

  if (s_has_saved)
  {
    esp_timer_start_once(s_fallback_timer, FALLBACK_US);
  }
  publish_state();
  return ESP_OK;
}

bool wifi_bridge_is_online(void)
{
  return s_online;
}

bool wifi_bridge_setup_ap_active(void)
{
  return s_ap_active;
}

esp_err_t wifi_bridge_get_link(wifi_bridge_link_t *out)
{
  ESP_RETURN_ON_FALSE(out, ESP_ERR_INVALID_ARG, TAG, "out is NULL");
  memset(out, 0, sizeof(*out));

  wifi_ap_record_t ap;
  if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK)
    return ESP_OK; /* not associated: everything stays empty/false */


  out->associated = true;
  strlcpy(out->ssid, (const char *)ap.ssid, sizeof(out->ssid));
  snprintf(out->bssid,
           sizeof(out->bssid),
           "%02x:%02x:%02x:%02x:%02x:%02x",
           ap.bssid[0],
           ap.bssid[1],
           ap.bssid[2],
           ap.bssid[3],
           ap.bssid[4],
           ap.bssid[5]);
  out->channel = ap.primary;
  out->rssi = ap.rssi;

  esp_netif_ip_info_t ip_info;
  if (s_online && esp_netif_get_ip_info(s_sta_netif, &ip_info) == ESP_OK)
  {
    out->online = true;
    snprintf(out->ip, sizeof(out->ip), IPSTR, IP2STR(&ip_info.ip));
    snprintf(out->gateway, sizeof(out->gateway), IPSTR, IP2STR(&ip_info.gw));
    snprintf(out->netmask, sizeof(out->netmask), IPSTR, IP2STR(&ip_info.netmask));
  }
  return ESP_OK;
}

esp_err_t wifi_bridge_get_status(wifi_bridge_status_t *out)
{
  ESP_RETURN_ON_FALSE(out, ESP_ERR_INVALID_ARG, TAG, "out is NULL");
  memset(out, 0, sizeof(*out));

  lock();
  out->setup_ap_active = s_ap_active;
  strlcpy(out->ap_ssid, s_ap_ssid, sizeof(out->ap_ssid));
  strlcpy(out->ap_ip, s_ap_ip, sizeof(out->ap_ip));
  out->ap_secured = (CONFIG_WIFI_BRIDGE_SETUP_AP_PASSWORD[0] != '\0');
  out->has_saved = s_has_saved;
  strlcpy(out->saved_ssid, s_saved_ssid, sizeof(out->saved_ssid));
  out->online = s_online;
  out->prov_state = s_prov_state;
  strlcpy(out->prov_ssid, s_prov_ssid, sizeof(out->prov_ssid));
  strlcpy(out->prov_reason, s_prov_reason, sizeof(out->prov_reason));
  unlock();
  return ESP_OK;
}

esp_err_t wifi_bridge_scan(wifi_bridge_ap_t *out, size_t max, size_t *count)
{
  ESP_RETURN_ON_FALSE(out && count && max > 0, ESP_ERR_INVALID_ARG, TAG, "bad scan arguments");
  *count = 0;

  const wifi_scan_config_t scan_cfg = {
    .show_hidden = false,
    .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    .scan_time.active = {.min = 100, .max = 300},
  };
  ESP_RETURN_ON_ERROR(esp_wifi_scan_start(&scan_cfg, true), TAG, "scan");

  wifi_ap_record_t *records = calloc(SCAN_RECORDS_MAX, sizeof(*records));
  if (!records)
  {
    esp_wifi_clear_ap_list();
    return ESP_ERR_NO_MEM;
  }

  uint16_t found = SCAN_RECORDS_MAX;
  esp_err_t err = esp_wifi_scan_get_ap_records(&found, records); /* also frees the driver's list */
  if (err == ESP_OK)
  {
    size_t n = 0;
    for (uint16_t i = 0; i < found; i++)
    {
      const char *ssid = (const char *)records[i].ssid;
      if (ssid[0] == '\0')
      {
        continue;
      }

      size_t existing = n;
      for (size_t j = 0; j < n; j++)
      {
        if (strcmp(out[j].ssid, ssid) == 0)
        {
          existing = j;
          break;
        }
      }
      if (existing == n && n >= max)
      {
        continue;
      }
      if (existing < n && out[existing].rssi >= records[i].rssi)
      {
        continue; /* keep the strongest access point of a name shared by several */
      }

      strlcpy(out[existing].ssid, ssid, sizeof(out[existing].ssid));
      out[existing].rssi = records[i].rssi;
      out[existing].channel = records[i].primary;
      out[existing].secure = (records[i].authmode != WIFI_AUTH_OPEN);
      if (existing == n)
      {
        n++;
      }
    }

    /* Strongest first (insertion sort: at most a few dozen entries). */
    for (size_t i = 1; i < n; i++)
    {
      wifi_bridge_ap_t current = out[i];
      size_t j = i;
      while (j > 0 && out[j - 1].rssi < current.rssi)
      {
        out[j] = out[j - 1];
        j--;
      }
      out[j] = current;
    }
    *count = n;
  }
  free(records);
  return err;
}

esp_err_t wifi_bridge_provision(const char *ssid, const char *password)
{
  size_t ssid_len = ssid ? strlen(ssid) : 0;
  size_t pass_len = password ? strlen(password) : 0;
  ESP_RETURN_ON_FALSE(ssid_len >= 1 && ssid_len <= SSID_MAX_LEN, ESP_ERR_INVALID_ARG, TAG, "bad SSID length");
  ESP_RETURN_ON_FALSE(pass_len == 0 || (pass_len >= 8 && pass_len <= PASSWORD_MAX_LEN),
                      ESP_ERR_INVALID_ARG,
                      TAG,
                      "bad password length");
  ESP_RETURN_ON_FALSE(s_ap_active, ESP_ERR_INVALID_STATE, TAG, "setup network is not open");

  lock();
  if (s_prov_state == WIFI_BRIDGE_PROV_CONNECTING)
  {
    unlock();
    return ESP_ERR_INVALID_STATE;
  }
  s_prov_state = WIFI_BRIDGE_PROV_CONNECTING;
  strlcpy(s_prov_ssid, ssid, sizeof(s_prov_ssid));
  strlcpy(s_prov_pass, password ? password : "", sizeof(s_prov_pass));
  s_prov_reason[0] = '\0';
  s_online = false;
  publish_state_locked();
  unlock();

  ESP_LOGI(TAG, "Trying network '%s'", ssid);
  esp_timer_stop(s_reconnect_timer);
  esp_timer_stop(s_ap_stop_timer);

  esp_err_t err = apply_sta_config(ssid, password);
  if (err == ESP_OK)
  {
    esp_wifi_disconnect(); /* ignore: nothing to disconnect in setup mode */
    err = esp_wifi_connect();
  }
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "Could not start the attempt: %s", esp_err_to_name(err));
    prov_finish_failed("failed");
    return err;
  }
  esp_timer_start_once(s_prov_timer, PROVISION_TIMEOUT_US);
  return ESP_OK;
}

esp_err_t wifi_bridge_forget_and_restart(uint32_t delay_ms)
{
  esp_err_t err = wifi_bridge_nvs_erase();
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "Erasing the saved network failed: %s", esp_err_to_name(err));
    return err;
  }
  ESP_LOGW(TAG, "Saved network erased, restarting in %u ms", (unsigned)delay_ms);
  return esp_timer_start_once(s_restart_timer, (uint64_t)delay_ms * 1000);
}
