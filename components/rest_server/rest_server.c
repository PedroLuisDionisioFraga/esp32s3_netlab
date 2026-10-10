#include "rest_server.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "auth.h"
#include "cJSON.h"
#include "chip_health.h"
#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "heap_monitor.h"
#include "lwip/sockets.h"
#include "notification_manager.h"
#include "sdkconfig.h"
#include "wifi_bridge.h"

#define BASE_PATH_MAX     16
#define HOSTNAME_MAX      32
#define FILE_PATH_MAX     192
#define SCRATCH_BUFSIZE   4096
#define BODY_RECV_RETRIES 3
#define SCAN_RESULTS_MAX  20
#define RESTART_DELAY_MS  500
#define AUTH_HEADER_MAX   80 /* "Bearer " + a 32-character token, with room to spare */
#define CREDENTIAL_MAX    64
#define ENCODING_MAX      64

#if CONFIG_WIFI_BRIDGE_ROUTER_MODE
#define ROUTER_CLIENTS_MAX 8 /* CONFIG_WIFI_BRIDGE_ROUTER_MAX_CLIENTS is at most 8 */
#define LAB_MESSAGES_MAX   8
#define LAB_TEXT_MAX       120

/* A request the lab service received: what any plain-HTTP server gets to read. */
typedef struct
{
  char client[16];
  uint32_t at_s;
  size_t length;
  char text[LAB_TEXT_MAX + 1]; /* the start of the body */
} lab_message_t;
#endif

static const char *TAG = "rest_server";

/* The setup page is compiled into the firmware (see EMBED_FILES in CMakeLists.txt). */
extern const char setup_html_start[] asm("_binary_setup_html_start");
extern const char setup_html_end[] asm("_binary_setup_html_end");

typedef struct
{
  char base_path[BASE_PATH_MAX];
  char hostname[HOSTNAME_MAX];
  char scratch[SCRATCH_BUFSIZE]; /* shared by all handlers: esp_http_server runs them on one task */
#if CONFIG_WIFI_BRIDGE_ROUTER_MODE
  lab_message_t lab[LAB_MESSAGES_MAX]; /* a ring, cleared with the capture */
  size_t lab_next;
#endif
} rest_ctx_t;

/* ---------- helpers ---------- */

static esp_err_t send_json(httpd_req_t *req, cJSON *root)
{
  char *body = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (!body)
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  esp_err_t err = httpd_resp_sendstr(req, body);
  cJSON_free(body);
  return err;
}

/* `status` must be a string literal: it is only read when the response is sent. */
static esp_err_t send_error(httpd_req_t *req, const char *status, const char *message)
{
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, "text/plain");
  return httpd_resp_sendstr(req, message);
}

/* ---------- authentication ---------- */

/* The "Authorization: Bearer <token>" header: the token of the session the web page got at login. A cookie would
 * be sent by the browser to any site that asked for it; a header the page sets itself cannot be forged across sites. */
static bool bearer_token(httpd_req_t *req, char *token, size_t len)
{
  char header[AUTH_HEADER_MAX];
  if (httpd_req_get_hdr_value_str(req, "Authorization", header, sizeof(header)) != ESP_OK)
    return false;
  if (strncasecmp(header, "Bearer ", 7) != 0)
    return false;
  strlcpy(token, header + 7, len);
  return true;
}

/* True when the request may go on. Otherwise the 401 has been sent and the handler must just return. */
static bool require_session(httpd_req_t *req)
{
  if (!rest_auth_enabled())
    return true;

  char token[REST_AUTH_TOKEN_LEN + 1];
  if (bearer_token(req, token, sizeof(token)) && rest_auth_check(token))
    return true;

  send_error(req, "401 Unauthorized", "Sign in required");
  return false;
}

/* The Wi-Fi provisioning routes are what a phone on the setup network uses before it has any login to show: they
 * are open while that network is, and need a session otherwise. */
static bool require_session_unless_setup(httpd_req_t *req)
{
  return wifi_bridge_setup_ap_active() || require_session(req);
}

/* Reads the whole body into ctx->scratch, NUL-terminated. Returns false after sending the error
 * response itself. */
static bool recv_body(httpd_req_t *req)
{
  rest_ctx_t *ctx = req->user_ctx;
  size_t total = req->content_len;

  if (total == 0 || total >= sizeof(ctx->scratch))
  {
    send_error(req, "400 Bad Request", "Missing or oversized request body");
    return false;
  }

  size_t received = 0;
  int retries = BODY_RECV_RETRIES;
  while (received < total)
  {
    int n = httpd_req_recv(req, ctx->scratch + received, total - received);
    if (n == HTTPD_SOCK_ERR_TIMEOUT && retries-- > 0)
      continue;

    if (n <= 0)
    {
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read request body");
      return false;
    }
    received += n;
  }
  ctx->scratch[total] = '\0';
  return true;
}

/* Reads and parses the request body. Returns NULL after sending the error response itself. */
static cJSON *recv_json_body(httpd_req_t *req)
{
  rest_ctx_t *ctx = req->user_ctx;
  if (!recv_body(req))
    return NULL;

  cJSON *root = cJSON_Parse(ctx->scratch);
  if (!root)
    send_error(req, "400 Bad Request", "Invalid JSON");

  return root;
}

static void add_led_json(cJSON *parent)
{
  notification_output_t led;
  notification_manager_get_output(&led);

  cJSON *obj = cJSON_AddObjectToObject(parent, "led");
  if (!obj)
    return;

  cJSON_AddStringToObject(obj, "mode", led.manual ? "manual" : "auto");
  cJSON_AddNumberToObject(obj, "r", led.r);
  cJSON_AddNumberToObject(obj, "g", led.g);
  cJSON_AddNumberToObject(obj, "b", led.b);
}

static const char *prov_state_name(wifi_bridge_prov_state_t state)
{
  switch (state)
  {
    case WIFI_BRIDGE_PROV_CONNECTING:
      return "connecting";
    case WIFI_BRIDGE_PROV_SUCCESS:
      return "success";
    case WIFI_BRIDGE_PROV_FAILED:
      return "failed";
    default:
      return "idle";
  }
}

/* ---------- API handlers ---------- */

static esp_err_t system_info_get_handler(httpd_req_t *req)
{
  if (!require_session(req))
    return ESP_OK;

  chip_health_snapshot_t health;
  chip_health_read(&health);

  cJSON *root = cJSON_CreateObject();
  if (!root)
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");

  cJSON_AddStringToObject(root, "chip", health.chip);
  cJSON_AddStringToObject(root, "idf_version", health.idf_version);
  cJSON_AddNumberToObject(root, "cores", health.cores);
  if (health.temperature_valid)
    cJSON_AddNumberToObject(root, "temperature_c", round((double)health.temperature_c * 10.0) / 10.0);
  else
    cJSON_AddNullToObject(root, "temperature_c");

  cJSON_AddNumberToObject(root, "uptime_s", health.uptime_s);
  cJSON_AddNumberToObject(root, "free_heap", health.free_heap);
  cJSON_AddNumberToObject(root, "min_free_heap", health.min_free_heap);
  cJSON_AddStringToObject(root, "reset_reason", health.reset_reason);
  add_led_json(root);
  return send_json(req, root);
}

/* The setup page reads this too, to show the address the lab got: open while the setup network is. */
static esp_err_t link_get_handler(httpd_req_t *req)
{
  if (!require_session_unless_setup(req))
    return ESP_OK;

  wifi_bridge_link_t link;
  wifi_bridge_get_link(&link);

  cJSON *root = cJSON_CreateObject();
  if (!root)
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");

  cJSON_AddBoolToObject(root, "associated", link.associated);
  cJSON_AddBoolToObject(root, "online", link.online);
  if (link.associated)
  {
    cJSON_AddStringToObject(root, "ssid", link.ssid);
    cJSON_AddStringToObject(root, "bssid", link.bssid);
    cJSON_AddNumberToObject(root, "channel", link.channel);
    cJSON_AddNumberToObject(root, "rssi", link.rssi);
  }
  if (link.online)
  {
    cJSON_AddStringToObject(root, "ip", link.ip);
    cJSON_AddStringToObject(root, "gateway", link.gateway);
    cJSON_AddStringToObject(root, "netmask", link.netmask);
  }
  return send_json(req, root);
}

/* Body: {"r":0-255,"g":0-255,"b":0-255} forces a color, {"mode":"auto"} returns to the status color. */
static esp_err_t led_post_handler(httpd_req_t *req)
{
  if (!require_session(req))
    return ESP_OK;

  cJSON *body = recv_json_body(req);
  if (!body)
    return ESP_OK; /* error response already sent */

  const cJSON *mode = cJSON_GetObjectItemCaseSensitive(body, "mode");
  if (cJSON_IsString(mode) && strcmp(mode->valuestring, "auto") == 0)
  {
    notification_manager_clear_manual_color();
  }
  else
  {
    static const char *const keys[3] = {"r", "g", "b"};
    int rgb[3];
    for (int i = 0; i < 3; i++)
    {
      const cJSON *item = cJSON_GetObjectItemCaseSensitive(body, keys[i]);
      if (!cJSON_IsNumber(item) || item->valueint < 0 || item->valueint > 255)
      {
        cJSON_Delete(body);
        return send_error(req, "400 Bad Request", "Expected {\"r\",\"g\",\"b\"} in 0..255 or {\"mode\":\"auto\"}");
      }
      rgb[i] = item->valueint;
    }
    notification_manager_set_manual_color((uint8_t)rgb[0], (uint8_t)rgb[1], (uint8_t)rgb[2]);
  }
  cJSON_Delete(body);

  cJSON *root = cJSON_CreateObject();
  if (!root)
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");

  cJSON_AddBoolToObject(root, "ok", true);
  add_led_json(root);
  return send_json(req, root);
}

static esp_err_t wifi_status_get_handler(httpd_req_t *req)
{
  if (!require_session_unless_setup(req))
    return ESP_OK;

  rest_ctx_t *ctx = req->user_ctx;
  wifi_bridge_status_t st;
  wifi_bridge_get_status(&st);

  cJSON *root = cJSON_CreateObject();
  if (!root)
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");

  cJSON_AddStringToObject(root, "hostname", ctx->hostname);
  cJSON_AddBoolToObject(root, "online", st.online);
  if (st.has_saved)
    cJSON_AddStringToObject(root, "saved_ssid", st.saved_ssid);
  else
    cJSON_AddNullToObject(root, "saved_ssid");

  cJSON *ap = cJSON_AddObjectToObject(root, "setup_ap");
  if (ap)
  {
    cJSON_AddBoolToObject(ap, "active", st.setup_ap_active);
    if (st.setup_ap_active)
    {
      cJSON_AddStringToObject(ap, "ssid", st.ap_ssid);
      cJSON_AddStringToObject(ap, "ip", st.ap_ip);
      cJSON_AddBoolToObject(ap, "secured", st.ap_secured);
    }
  }

  cJSON *prov = cJSON_AddObjectToObject(root, "provision");
  if (prov)
  {
    cJSON_AddStringToObject(prov, "state", prov_state_name(st.prov_state));
    if (st.prov_state != WIFI_BRIDGE_PROV_IDLE)
      cJSON_AddStringToObject(prov, "ssid", st.prov_ssid);
    if (st.prov_state == WIFI_BRIDGE_PROV_FAILED)
      cJSON_AddStringToObject(prov, "reason", st.prov_reason);
  }
  return send_json(req, root);
}

/* Blocks for a few seconds while the radio scans. */
static esp_err_t wifi_scan_get_handler(httpd_req_t *req)
{
  if (!require_session_unless_setup(req))
    return ESP_OK;

  wifi_bridge_ap_t *networks = calloc(SCAN_RESULTS_MAX, sizeof(*networks));
  if (!networks)
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");

  size_t count = 0;
  esp_err_t err = wifi_bridge_scan(networks, SCAN_RESULTS_MAX, &count);
  if (err != ESP_OK)
  {
    free(networks);
    ESP_LOGW(TAG, "Scan failed: %s", esp_err_to_name(err));
    return send_error(req, "503 Service Unavailable", "Scan failed, try again in a moment");
  }

  cJSON *root = cJSON_CreateObject();
  cJSON *list = root ? cJSON_AddArrayToObject(root, "networks") : NULL;
  if (!list)
  {
    cJSON_Delete(root);
    free(networks);
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
  }
  for (size_t i = 0; i < count; i++)
  {
    cJSON *item = cJSON_CreateObject();
    if (!item)
      break;

    cJSON_AddStringToObject(item, "ssid", networks[i].ssid);
    cJSON_AddNumberToObject(item, "rssi", networks[i].rssi);
    cJSON_AddNumberToObject(item, "channel", networks[i].channel);
    cJSON_AddBoolToObject(item, "secure", networks[i].secure);
    cJSON_AddItemToArray(list, item);
  }
  free(networks);
  return send_json(req, root);
}

/* Body: {"ssid":"...","password":"..."}. Answers 202 at once; poll /wifi/status for the result. */
static esp_err_t wifi_provision_post_handler(httpd_req_t *req)
{
  if (!require_session_unless_setup(req))
    return ESP_OK;

  cJSON *body = recv_json_body(req);
  if (!body)
    return ESP_OK; /* error response already sent */

  const cJSON *ssid = cJSON_GetObjectItemCaseSensitive(body, "ssid");
  const cJSON *password = cJSON_GetObjectItemCaseSensitive(body, "password");
  const char *ssid_str = cJSON_IsString(ssid) ? ssid->valuestring : NULL;
  const char *pass_str = cJSON_IsString(password) ? password->valuestring : "";

  const char *problem = NULL;
  esp_err_t err = ESP_OK;
  if (!ssid_str || ssid_str[0] == '\0' || strlen(ssid_str) > 32)
    problem = "Network name must be 1 to 32 characters";
  else if (pass_str[0] != '\0' && (strlen(pass_str) < 8 || strlen(pass_str) > 63))
    problem = "Password must be empty or 8 to 63 characters";
  else
    err = wifi_bridge_provision(ssid_str, pass_str); /* copies both strings */
  cJSON_Delete(body);

  if (problem)
    return send_error(req, "400 Bad Request", problem);

  if (err == ESP_ERR_INVALID_STATE)
    return send_error(req, "409 Conflict", "The setup network is not open, or a connection attempt is already running");

  if (err != ESP_OK)
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not start the connection attempt");

  cJSON *root = cJSON_CreateObject();
  if (!root)
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");

  cJSON_AddBoolToObject(root, "ok", true);
  httpd_resp_set_status(req, "202 Accepted");
  return send_json(req, root);
}

/* Forgets the saved network and restarts into setup mode. */
static esp_err_t wifi_forget_post_handler(httpd_req_t *req)
{
  if (!require_session(req))
    return ESP_OK;

  if (wifi_bridge_forget_and_restart(RESTART_DELAY_MS) != ESP_OK)
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not erase the saved network");

  cJSON *root = cJSON_CreateObject();
  if (!root)
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");

  cJSON_AddBoolToObject(root, "ok", true);
  return send_json(req, root);
}

/* ---------- session ---------- */

/* Body: {"username":"...","password":"..."}. Answers {"token":"...","expires_in":seconds}: the page sends the token
 * back as "Authorization: Bearer <token>". Public, like the page that calls it. */
static esp_err_t session_post_handler(httpd_req_t *req)
{
  cJSON *body = recv_json_body(req);
  if (!body)
    return ESP_OK; /* error response already sent */

  const cJSON *user = cJSON_GetObjectItemCaseSensitive(body, "username");
  const cJSON *password = cJSON_GetObjectItemCaseSensitive(body, "password");
  char user_str[CREDENTIAL_MAX] = "";
  char password_str[CREDENTIAL_MAX] = "";
  const bool shaped = cJSON_IsString(user) && cJSON_IsString(password);
  if (shaped)
  {
    strlcpy(user_str, user->valuestring, sizeof(user_str));
    strlcpy(password_str, password->valuestring, sizeof(password_str));
  }
  cJSON_Delete(body);
  if (!shaped)
    return send_error(req, "400 Bad Request", "Expected {\"username\": \"...\", \"password\": \"...\"}");

  char token[REST_AUTH_TOKEN_LEN + 1];
  uint32_t retry_after_s = 0;
  const rest_auth_result_t result = rest_auth_login(user_str, password_str, token, &retry_after_s);
  memset(password_str, 0, sizeof(password_str));

  if (result == REST_AUTH_LOCKED)
  {
    char message[64];
    snprintf(message, sizeof(message), "Too many attempts, try again in %lu s", (unsigned long)retry_after_s);
    return send_error(req, "429 Too Many Requests", message);
  }
  if (result != REST_AUTH_OK)
    return send_error(req, "401 Unauthorized", "Wrong user name or password");

  cJSON *root = cJSON_CreateObject();
  if (!root)
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");

  cJSON_AddStringToObject(root, "token", token);
  cJSON_AddNumberToObject(root, "expires_in", rest_auth_ttl_s());
  return send_json(req, root);
}

/* Ends the session of the token in the request. Always answers ok: a token that is already gone is the goal. */
static esp_err_t session_delete_handler(httpd_req_t *req)
{
  char token[REST_AUTH_TOKEN_LEN + 1];
  if (bearer_token(req, token, sizeof(token)))
    rest_auth_logout(token);

  cJSON *root = cJSON_CreateObject();
  if (!root)
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");

  cJSON_AddBoolToObject(root, "ok", true);
  return send_json(req, root);
}

/* What the login page shows before anyone is signed in: the product, its version and its address. */
static esp_err_t about_get_handler(httpd_req_t *req)
{
  rest_ctx_t *ctx = req->user_ctx;
  const esp_app_desc_t *app = esp_app_get_description();

  cJSON *root = cJSON_CreateObject();
  if (!root)
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");

  cJSON_AddStringToObject(root, "name", app->project_name);
  cJSON_AddStringToObject(root, "version", app->version);
  cJSON_AddStringToObject(root, "hostname", ctx->hostname);
  cJSON_AddBoolToObject(root, "auth", rest_auth_enabled());
  return send_json(req, root);
}

/* ---------- memory ---------- */

/* heap_monitor_write_json() sink: one chunk of the document onto the socket. False once the client is gone. */
static bool memory_sink(const char *data, size_t len, void *ctx)
{
  return httpd_resp_send_chunk((httpd_req_t *)ctx, data, (ssize_t)len) == ESP_OK;
}

/* The latest heaptop sample as one JSON document, streamed in chunks so no copy of it is made on the heap this page
 * measures. The page is front/web/src/pages/MemoryPage.vue. */
static esp_err_t memory_get_handler(httpd_req_t *req)
{
  if (!require_session(req))
    return ESP_OK;

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");

  const esp_err_t err = heap_monitor_write_json(memory_sink, req);
  if (err == ESP_ERR_NOT_FOUND)
    return send_error(req, "503 Service Unavailable", "heaptop: no sample yet, try again in a moment");
  if (err == ESP_ERR_INVALID_STATE)
    return send_error(req, "503 Service Unavailable", "heaptop is not running: see the serial log at boot");
  if (err != ESP_OK)
    return ESP_FAIL; /* part of the document is on the wire: closing the connection is what tells the browser */

  return httpd_resp_send_chunk(req, NULL, 0); /* the empty chunk ends the response */
}

/* Starts a fresh measurement window, like `ht clear`. */
static esp_err_t memory_clear_post_handler(httpd_req_t *req)
{
  if (!require_session(req))
    return ESP_OK;

  const esp_err_t err = heap_monitor_clear();
  if (err == ESP_ERR_INVALID_STATE)
    return send_error(req, "503 Service Unavailable", "heaptop is not running: see the serial log at boot");
  if (err != ESP_OK && err != ESP_ERR_TIMEOUT)
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not clear the statistics");

  cJSON *root = cJSON_CreateObject();
  if (!root)
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");

  cJSON_AddBoolToObject(root, "ok", true);
  /* A late cleared sample is not a failure: the clear still applies to a later one. */
  cJSON_AddBoolToObject(root, "pending", err == ESP_ERR_TIMEOUT);
  return send_json(req, root);
}

/* ---------- router lab ---------- */

#if CONFIG_WIFI_BRIDGE_ROUTER_MODE

static uint32_t uptime_s(void)
{
  return (uint32_t)(esp_timer_get_time() / 1000000);
}

/* IPv4 address of the client. The server listens on IPv6, so IPv4 clients show up as ::ffff:a.b.c.d. */
static bool peer_ipv4(httpd_req_t *req, struct in_addr *out)
{
  struct sockaddr_storage addr;
  socklen_t len = sizeof(addr);
  if (getpeername(httpd_req_to_sockfd(req), (struct sockaddr *)&addr, &len) != 0)
    return false;

  if (addr.ss_family == AF_INET)
  {
    *out = ((const struct sockaddr_in *)&addr)->sin_addr;
    return true;
  }
  const struct sockaddr_in6 *addr6 = (const struct sockaddr_in6 *)&addr;
  if (addr.ss_family == AF_INET6 && IN6_IS_ADDR_V4MAPPED(&addr6->sin6_addr))
  {
    memcpy(&out->s_addr, &addr6->sin6_addr.s6_addr[12], sizeof(out->s_addr));
    return true;
  }
  return false;
}

/* On top of the login: only the uplink side (the instructor) turns the capture on or off and wipes it, even with
 * the admin password typed on a lab client. */
static bool from_lab_network(httpd_req_t *req)
{
  wifi_bridge_router_status_t st;
  wifi_bridge_router_get_status(&st);
  if (!st.active)
    return false;

  struct in_addr peer;
  struct in_addr lab;
  if (!peer_ipv4(req, &peer) || !inet_aton(st.ip, &lab))
    return true; /* cannot tell: treat it as a lab client */

  const uint32_t mask = htonl(0xffffff00UL); /* the lab network is a /24 */
  return (peer.s_addr & mask) == (lab.s_addr & mask);
}

/* Builds a response in the scratch buffer and sends it in chunks of up to its size, so a full flow
 * table never becomes one big cJSON tree (that would take tens of KB of internal RAM). */
typedef struct
{
  httpd_req_t *req;
  char *buf;
  size_t len;
  esp_err_t err;
} json_stream_t;

static void stream_flush(json_stream_t *s)
{
  if (s->err == ESP_OK && s->len > 0)
    s->err = httpd_resp_send_chunk(s->req, s->buf, s->len);
  s->len = 0;
}

/* Short literal text only (shorter than the buffer). */
static void stream_text(json_stream_t *s, const char *text)
{
  const size_t n = strlen(text);
  if (s->len + n >= SCRATCH_BUFSIZE)
    stream_flush(s);
  memcpy(s->buf + s->len, text, n);
  s->len += n;
}

/* Appends `item` and frees it; a comma first unless it opens its array. */
static void stream_item(json_stream_t *s, bool first, cJSON *item)
{
  if (!first)
    stream_text(s, ",");

  /* cJSON may need a few bytes more than it prints: when the rest of the buffer is too small,
   * send what is there and print again into the empty buffer. */
  if (!item)
    s->err = ESP_ERR_NO_MEM;
  else if (!cJSON_PrintPreallocated(item, s->buf + s->len, (int)(SCRATCH_BUFSIZE - s->len), false))
  {
    stream_flush(s);
    if (!cJSON_PrintPreallocated(item, s->buf, SCRATCH_BUFSIZE, false))
      s->err = ESP_ERR_NO_MEM;
  }
  if (s->err == ESP_OK)
    s->len += strlen(s->buf + s->len);
  cJSON_Delete(item);
}

static cJSON *client_json(const wifi_bridge_router_client_t *c)
{
  cJSON *item = cJSON_CreateObject();
  if (!item)
    return NULL;

  cJSON_AddStringToObject(item, "mac", c->mac);
  if (c->ip[0] != '\0')
    cJSON_AddStringToObject(item, "ip", c->ip);
  else
    cJSON_AddNullToObject(item, "ip");
  cJSON_AddNumberToObject(item, "rssi", c->rssi);
  return item;
}

static cJSON *flow_json(const wifi_bridge_router_flow_t *f)
{
  cJSON *item = cJSON_CreateObject();
  if (!item)
    return NULL;

  cJSON_AddStringToObject(item, "client", f->client);
  cJSON_AddStringToObject(item, "dst", f->dst);
  cJSON_AddNumberToObject(item, "port", f->port);
  cJSON_AddNumberToObject(item, "proto", f->proto);
  cJSON_AddNumberToObject(item, "packets", f->packets);
  cJSON_AddNumberToObject(item, "bytes", (double)f->bytes);
  cJSON_AddNumberToObject(item, "first_s", f->first_s);
  cJSON_AddNumberToObject(item, "last_s", f->last_s);
  return item;
}

static cJSON *dns_json(const wifi_bridge_router_dns_t *d)
{
  cJSON *item = cJSON_CreateObject();
  if (!item)
    return NULL;

  cJSON_AddStringToObject(item, "client", d->client);
  cJSON_AddStringToObject(item, "name", d->name);
  cJSON_AddNumberToObject(item, "at_s", d->at_s);
  return item;
}

static cJSON *lab_json(const lab_message_t *m)
{
  cJSON *item = cJSON_CreateObject();
  if (!item)
    return NULL;

  cJSON_AddStringToObject(item, "client", m->client);
  cJSON_AddNumberToObject(item, "at_s", m->at_s);
  cJSON_AddNumberToObject(item, "length", m->length);
  cJSON_AddStringToObject(item, "text", m->text);
  return item;
}

/* The lab network and its clients, the counters, and the records: flows, DNS names (newest first)
 * and lab service requests (newest first). Every list is bounded by its table, so this is also the
 * export. */
static esp_err_t router_get_handler(httpd_req_t *req)
{
  if (!require_session(req))
    return ESP_OK;

  rest_ctx_t *ctx = req->user_ctx;
  wifi_bridge_router_status_t st;
  wifi_bridge_router_get_status(&st);

  cJSON *head = cJSON_CreateObject();
  if (!head)
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");

  cJSON_AddBoolToObject(head, "enabled", true);
  cJSON_AddBoolToObject(head, "active", st.active);
  cJSON_AddStringToObject(head, "ssid", st.ssid);
  cJSON_AddStringToObject(head, "ip", st.ip);
  cJSON_AddNumberToObject(head, "channel", st.channel);
  cJSON_AddNumberToObject(head, "max_clients", st.max_clients);
  if (st.problem[0] != '\0')
    cJSON_AddStringToObject(head, "problem", st.problem);
  else
    cJSON_AddNullToObject(head, "problem");
  cJSON_AddBoolToObject(head, "capture", st.capture);
  cJSON_AddNumberToObject(head, "now_s", uptime_s());
  cJSON_AddNumberToObject(head, "packets", st.packets);
  cJSON_AddNumberToObject(head, "bytes", (double)st.bytes);
  cJSON_AddNumberToObject(head, "flows_dropped", st.flows_dropped);
  cJSON_AddNumberToObject(head, "flows_max", st.flows_max);

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");

  json_stream_t s = {.req = req, .buf = ctx->scratch};
  stream_item(&s, true, head);
  if (s.err == ESP_OK)
    s.len--; /* drop the closing brace: the lists follow */

  wifi_bridge_router_client_t clients[ROUTER_CLIENTS_MAX];
  const size_t client_count = wifi_bridge_router_get_clients(clients, ROUTER_CLIENTS_MAX);
  stream_text(&s, ",\"clients\":[");
  for (size_t i = 0; i < client_count; i++)
    stream_item(&s, i == 0, client_json(&clients[i]));

  stream_text(&s, "],\"flows\":[");
  size_t count = 0;
  for (size_t slot = 0; slot < st.flows_max && s.err == ESP_OK; slot++)
  {
    wifi_bridge_router_flow_t flow;
    if (wifi_bridge_router_get_flow(slot, &flow))
      stream_item(&s, count++ == 0, flow_json(&flow));
  }

  stream_text(&s, "],\"dns\":[");
  count = 0;
  for (size_t age = 0; age < st.dns_max && s.err == ESP_OK; age++)
  {
    wifi_bridge_router_dns_t dns;
    if (wifi_bridge_router_get_dns(age, &dns))
      stream_item(&s, count++ == 0, dns_json(&dns));
  }

  stream_text(&s, "],\"lab\":[");
  count = 0;
  for (size_t age = 0; age < LAB_MESSAGES_MAX && s.err == ESP_OK; age++)
  {
    const lab_message_t *m = &ctx->lab[(ctx->lab_next + LAB_MESSAGES_MAX - 1 - age) % LAB_MESSAGES_MAX];
    if (m->client[0] != '\0')
      stream_item(&s, count++ == 0, lab_json(m));
  }

  stream_text(&s, "]}");
  stream_flush(&s);
  if (s.err != ESP_OK)
  {
    ESP_LOGW(TAG, "Router status not sent: %s", esp_err_to_name(s.err));
    return ESP_FAIL; /* the response is cut: close the connection */
  }
  return httpd_resp_send_chunk(req, NULL, 0);
}

/* Body: {"enabled":true|false}. */
static esp_err_t router_capture_post_handler(httpd_req_t *req)
{
  if (!require_session(req))
    return ESP_OK;

  if (from_lab_network(req))
    return send_error(req, "403 Forbidden", "Capture is changed from the uplink network, not from the lab network");

  cJSON *body = recv_json_body(req);
  if (!body)
    return ESP_OK; /* error response already sent */

  const cJSON *enabled = cJSON_GetObjectItemCaseSensitive(body, "enabled");
  const bool valid = cJSON_IsBool(enabled);
  const bool on = cJSON_IsTrue(enabled);
  cJSON_Delete(body);
  if (!valid)
    return send_error(req, "400 Bad Request", "Expected {\"enabled\": true|false}");

  wifi_bridge_router_set_capture(on);

  cJSON *root = cJSON_CreateObject();
  if (!root)
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");

  cJSON_AddBoolToObject(root, "ok", true);
  cJSON_AddBoolToObject(root, "capture", on);
  return send_json(req, root);
}

/* Forgets the flows, DNS names, lab service requests and counters. */
static esp_err_t router_clear_post_handler(httpd_req_t *req)
{
  if (!require_session(req))
    return ESP_OK;

  rest_ctx_t *ctx = req->user_ctx;
  if (from_lab_network(req))
    return send_error(req, "403 Forbidden", "The capture is cleared from the uplink network, not from the lab network");

  wifi_bridge_router_clear();
  memset(ctx->lab, 0, sizeof(ctx->lab));
  ctx->lab_next = 0;

  cJSON *root = cJSON_CreateObject();
  if (!root)
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");

  cJSON_AddBoolToObject(root, "ok", true);
  return send_json(req, root);
}

/* Body: any text. The lab's own plain-HTTP service: the dashboard shows the start of what it
 * received, which is what every HTTP server (and anyone on the path, without TLS) can read.
 * Recorded only while capture is on. */
static esp_err_t router_lab_post_handler(httpd_req_t *req)
{
  rest_ctx_t *ctx = req->user_ctx;
  wifi_bridge_router_status_t st;
  wifi_bridge_router_get_status(&st);
  if (!st.capture)
    return send_error(req, "409 Conflict", "Capture is off: the lab service records nothing");

  if (!recv_body(req))
    return ESP_OK; /* error response already sent */

  lab_message_t *m = &ctx->lab[ctx->lab_next];
  ctx->lab_next = (ctx->lab_next + 1) % LAB_MESSAGES_MAX;
  memset(m, 0, sizeof(*m));

  struct in_addr peer;
  if (!peer_ipv4(req, &peer) || !inet_ntoa_r(peer, m->client, sizeof(m->client)))
    strlcpy(m->client, "?", sizeof(m->client));
  m->at_s = uptime_s();
  m->length = req->content_len;
  for (size_t i = 0; i < LAB_TEXT_MAX && ctx->scratch[i] != '\0'; i++)
  {
    const char c = ctx->scratch[i];
    m->text[i] = (c >= ' ' && c < 0x7f) ? c : '?';
  }

  cJSON *root = cJSON_CreateObject();
  if (!root)
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");

  cJSON_AddBoolToObject(root, "ok", true);
  cJSON_AddNumberToObject(root, "length", m->length);
  return send_json(req, root);
}

#else

static esp_err_t router_get_handler(httpd_req_t *req)
{
  if (!require_session(req))
    return ESP_OK;

  cJSON *root = cJSON_CreateObject();
  if (!root)
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");

  cJSON_AddBoolToObject(root, "enabled", false); /* built without router lab mode */
  return send_json(req, root);
}

#endif /* CONFIG_WIFI_BRIDGE_ROUTER_MODE */

/* ---------- setup portal ---------- */

static esp_err_t send_setup_page(httpd_req_t *req)
{
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, setup_html_start, setup_html_end - setup_html_start);
}

/* Phones probe a well-known URL to detect a captive portal; a redirect (with a body, which iOS
 * requires) makes them offer to open the setup page. */
static esp_err_t send_setup_redirect(httpd_req_t *req)
{
  wifi_bridge_status_t st;
  wifi_bridge_get_status(&st);

  char location[32];
  snprintf(location, sizeof(location), "http://%s/", st.ap_ip);
  httpd_resp_set_status(req, "303 See Other");
  httpd_resp_set_hdr(req, "Location", location);
  return httpd_resp_send(req, "Redirect to the setup page", HTTPD_RESP_USE_STRLEN);
}

/* ---------- static web UI ---------- */

static const struct
{
  const char *ext;
  const char *type;
} k_content_types[] = {
  {".html", "text/html"},
  {".js", "application/javascript"},
  {".mjs", "application/javascript"},
  {".css", "text/css"},
  {".json", "application/json"},
  {".svg", "image/svg+xml"},
  {".png", "image/png"},
  {".ico", "image/x-icon"},
  {".woff2", "font/woff2"},
  {".webmanifest", "application/manifest+json"},
  {".txt", "text/plain"},
};

static bool has_extension(const char *path, const char *ext)
{
  size_t path_len = strlen(path);
  size_t ext_len = strlen(ext);
  return path_len >= ext_len && strcasecmp(path + path_len - ext_len, ext) == 0;
}

static void set_file_headers(httpd_req_t *req, const char *filepath)
{
  const char *type = "application/octet-stream";
  for (size_t i = 0; i < sizeof(k_content_types) / sizeof(k_content_types[0]); i++)
  {
    if (has_extension(filepath, k_content_types[i].ext))
    {
      type = k_content_types[i].type;
      break;
    }
  }
  httpd_resp_set_type(req, type);

  /* Vite fingerprints everything under /assets, so it can be cached forever;
   * index.html must be re-checked so a re-flash is picked up. */
  if (strstr(filepath, "/assets/"))
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=31536000, immutable");
  else
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
}

/* POST /api/v1/chat with a JSON body: placeholder that echoes the message back. */
static esp_err_t chat_post_handler(httpd_req_t *req)
{
  if (!require_session(req))
    return ESP_OK;

  cJSON *msg = recv_json_body(req);
  if (!msg)
    return ESP_OK; /* the error response was already sent */

  const cJSON *text = cJSON_GetObjectItemCaseSensitive(msg, "message");
  if (!cJSON_IsString(text))
  {
    cJSON_Delete(msg);
    return send_error(req, "400 Bad Request", "Expected {\"message\": \"...\"}");
  }
  ESP_LOGI(TAG, "Chat: %s", text->valuestring);

  return send_json(req, msg); /* send_json frees msg */
}

/* True when the client says it can decode gzip (every browser does). */
static bool accepts_gzip(httpd_req_t *req)
{
  char value[ENCODING_MAX];
  return httpd_req_get_hdr_value_str(req, "Accept-Encoding", value, sizeof(value)) == ESP_OK && strstr(value, "gzip");
}

/* Every GET that is not an API route. */
static esp_err_t static_get_handler(httpd_req_t *req)
{
  rest_ctx_t *ctx = req->user_ctx;

  /* While the setup network is open this is a captive portal: the setup page at "/", and a
   * redirect to it for everything else (phone connectivity probes, mistyped addresses). */
  if (wifi_bridge_setup_ap_active())
  {
    bool is_root = (strcmp(req->uri, "/") == 0) || (strncmp(req->uri, "/?", 2) == 0);
    return is_root ? send_setup_page(req) : send_setup_redirect(req);
  }

  if (ctx->base_path[0] == '\0')
    return httpd_resp_send_404(req);

  /* Ignore the query string and refuse anything that could climb out of the web root. */
  size_t path_len = strcspn(req->uri, "?#");
  if (path_len == 0 || strstr(req->uri, ".."))
    return httpd_resp_send_404(req);

  /* An API path nobody registered is a 404, not the web page: the page would answer 200 to a typo in a fetch(). */
  if (strncmp(req->uri, "/api/", 5) == 0)
    return httpd_resp_send_404(req);

  char filepath[FILE_PATH_MAX];
  const char *index_name = (req->uri[path_len - 1] == '/') ? "index.html" : "";
  int written = snprintf(filepath, sizeof(filepath), "%s%.*s%s", ctx->base_path, (int)path_len, req->uri, index_name);
  if (written < 0 || written >= (int)sizeof(filepath))
    return httpd_resp_send_404(req);

  /* The web UI is built with every file gzipped (front/web/vite.config.ts), so the .gz is the file that exists.
   * The type is still that of the name without it. */
  int fd = -1;
  bool gzipped = false;
  if (accepts_gzip(req))
  {
    char gz_path[FILE_PATH_MAX];
    if (snprintf(gz_path, sizeof(gz_path), "%s.gz", filepath) < (int)sizeof(gz_path))
    {
      fd = open(gz_path, O_RDONLY, 0);
      gzipped = fd >= 0;
    }
  }
  if (fd < 0)
    fd = open(filepath, O_RDONLY, 0);
  if (fd < 0 && !strchr(strrchr(filepath, '/'), '.'))
  {
    /* Client-side route such as /memory (no file extension): the web UI picks the view. */
    snprintf(filepath, sizeof(filepath), "%s/index.html", ctx->base_path);
    fd = open(filepath, O_RDONLY, 0);
    gzipped = false;
  }
  if (fd < 0)
    return httpd_resp_send_404(req);

  set_file_headers(req, filepath);
  if (gzipped)
  {
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Vary", "Accept-Encoding");
  }

  ssize_t n;
  do
  {
    n = read(fd, ctx->scratch, sizeof(ctx->scratch));
    if (n < 0)
    {
      ESP_LOGE(TAG, "Read failed: %s", filepath);
      break;
    }
    if (n > 0 && httpd_resp_send_chunk(req, ctx->scratch, n) != ESP_OK)
    {
      close(fd);
      ESP_LOGW(TAG, "Client dropped while sending %s", filepath);
      return ESP_FAIL;
    }
  } while (n > 0);
  close(fd);

  /* An empty chunk terminates the response. */
  return httpd_resp_send_chunk(req, NULL, 0);
}

/* ---------- server ---------- */

static httpd_handle_t s_server;

void rest_server_close_clients(void)
{
  int fds[CONFIG_LWIP_MAX_SOCKETS];
  size_t count = sizeof(fds) / sizeof(fds[0]);
  if (!s_server || httpd_get_client_list(s_server, &count, fds) != ESP_OK)
    return;

  for (size_t i = 0; i < count; i++)
    httpd_sess_trigger_close(s_server, fds[i]);
}

/* Diagnostic: dumps data that does not start like an HTTP request (to find who sends the
 * "parser error = 16" garbage). Otherwise behaves like the default recv. */
static int diag_recv(httpd_handle_t hd, int sockfd, char *buf, size_t buf_len, int flags)
{
  (void)hd;
  int n = recv(sockfd, buf, buf_len, flags);
  if (n < 0)
    return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? HTTPD_SOCK_ERR_TIMEOUT : HTTPD_SOCK_ERR_FAIL;

  if (n > 0 && (buf[0] < 'A' || buf[0] > 'Z') && buf[0] != '{')
  {
    ESP_LOGW(TAG, "Non-HTTP data on fd %d, %d bytes:", sockfd, n);
    ESP_LOG_BUFFER_HEXDUMP(TAG, buf, n < 96 ? n : 96, ESP_LOG_WARN);
  }
  return n;
}

static esp_err_t diag_open(httpd_handle_t hd, int sockfd)
{
  return httpd_sess_set_recv_override(hd, sockfd, diag_recv);
}

esp_err_t rest_server_start(const rest_server_config_t *cfg)
{
  httpd_handle_t server = NULL;

  ESP_RETURN_ON_FALSE(cfg, ESP_ERR_INVALID_ARG, TAG, "cfg is NULL");
  /* Checked before allocating, so this failure has nothing to free. */
  ESP_RETURN_ON_FALSE(!cfg->web_base_path || strlen(cfg->web_base_path) < BASE_PATH_MAX,
                      ESP_ERR_INVALID_ARG,
                      TAG,
                      "Web base path too long");

  rest_ctx_t *ctx = calloc(1, sizeof(*ctx));
  ESP_RETURN_ON_FALSE(ctx, ESP_ERR_NO_MEM, TAG, "No memory for REST context");

  if (cfg->web_base_path)
    strlcpy(ctx->base_path, cfg->web_base_path, sizeof(ctx->base_path));
  strlcpy(ctx->hostname, cfg->hostname ? cfg->hostname : "netlab", sizeof(ctx->hostname));

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.uri_match_fn = httpd_uri_match_wildcard;
  config.max_uri_handlers = 20; /* 18 in use with router lab mode, and room for later milestones */
  config.stack_size = 6144;
  config.open_fn = diag_open;
  config.lru_purge_enable = true; /* browsers open several sockets; recycle the oldest when full */

  esp_err_t err = httpd_start(&server, &config);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
    free(ctx);
    return err;
  }

  const httpd_uri_t routes[] = {
    /* Public: the login page calls these before it has a session. */
    {.uri = "/api/v1/about", .method = HTTP_GET, .handler = about_get_handler, .user_ctx = ctx},
    {.uri = "/api/v1/session", .method = HTTP_POST, .handler = session_post_handler, .user_ctx = ctx},
    {.uri = "/api/v1/session", .method = HTTP_DELETE, .handler = session_delete_handler, .user_ctx = ctx},
    /* Everything below needs a session (the Wi-Fi provisioning routes are open while the setup network is). */
    {.uri = "/api/v1/system/info", .method = HTTP_GET, .handler = system_info_get_handler, .user_ctx = ctx},
    {.uri = "/api/v1/link", .method = HTTP_GET, .handler = link_get_handler, .user_ctx = ctx},
    {.uri = "/api/v1/led", .method = HTTP_POST, .handler = led_post_handler, .user_ctx = ctx},
    {.uri = "/api/v1/wifi/status", .method = HTTP_GET, .handler = wifi_status_get_handler, .user_ctx = ctx},
    {.uri = "/api/v1/wifi/scan", .method = HTTP_GET, .handler = wifi_scan_get_handler, .user_ctx = ctx},
    {.uri = "/api/v1/wifi/provision", .method = HTTP_POST, .handler = wifi_provision_post_handler, .user_ctx = ctx},
    {.uri = "/api/v1/wifi/forget", .method = HTTP_POST, .handler = wifi_forget_post_handler, .user_ctx = ctx},
    {.uri = "/api/v1/memory", .method = HTTP_GET, .handler = memory_get_handler, .user_ctx = ctx},
    {.uri = "/api/v1/memory/clear", .method = HTTP_POST, .handler = memory_clear_post_handler, .user_ctx = ctx},
    {.uri = "/api/v1/chat", .method = HTTP_POST, .handler = chat_post_handler, .user_ctx = ctx},
    {.uri = "/api/v1/router", .method = HTTP_GET, .handler = router_get_handler, .user_ctx = ctx},
#if CONFIG_WIFI_BRIDGE_ROUTER_MODE
    {.uri = "/api/v1/router/capture", .method = HTTP_POST, .handler = router_capture_post_handler, .user_ctx = ctx},
    {.uri = "/api/v1/router/clear", .method = HTTP_POST, .handler = router_clear_post_handler, .user_ctx = ctx},
    /* Public, like the login: the lab service is what students' devices call (it records only while capture is on). */
    {.uri = "/api/v1/router/lab", .method = HTTP_POST, .handler = router_lab_post_handler, .user_ctx = ctx},
#endif
    /* Last, so the API routes above win over the wildcard. It always exists: the setup
     * portal needs it even when no web UI is mounted. */
    {.uri = "/*", .method = HTTP_GET, .handler = static_get_handler, .user_ctx = ctx},
  };
  for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++)
  {
    err = httpd_register_uri_handler(server, &routes[i]);
    if (err != ESP_OK)
    {
      ESP_LOGE(TAG, "Failed to register %s: %s", routes[i].uri, esp_err_to_name(err));
      httpd_stop(server);
      free(ctx);
      return err;
    }
  }

  s_server = server;
  ESP_LOGI(TAG, "HTTP server started (web UI %s)", cfg->web_base_path ? cfg->web_base_path : "not served");
  return ESP_OK;
}
