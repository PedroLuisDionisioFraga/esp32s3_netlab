#ifndef WIFI_BRIDGE_H
#define WIFI_BRIDGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#ifdef __cplusplus
extern "C"
{
#endif

  /*
   * Wi-Fi manager. The router credentials live in NVS, never in the firmware image:
   *
   *  - No saved network: the device opens its own "setup network" and waits for someone to
   *    pick a router (captive portal + provisioning API).
   *  - Saved network: it joins it and keeps retrying. If it stays offline for
   *    CONFIG_WIFI_BRIDGE_FALLBACK_SECONDS (for example after moving to another house) the
   *    setup network opens again, alongside the retries.
   *  - wifi_bridge_forget_and_restart() erases the saved network.
   */

  typedef enum
  {
    WIFI_BRIDGE_STATE_SETUP, /**< setup network is open, waiting for a router to be chosen */
    WIFI_BRIDGE_STATE_CONNECTING,
    WIFI_BRIDGE_STATE_ONLINE,  /**< associated and holding an IP address */
    WIFI_BRIDGE_STATE_OFFLINE, /**< link or IP lost; reconnecting */
  } wifi_bridge_state_t;

  typedef struct
  {
    /**
     * Optional. On every state change wifi_bridge clears NOTIF_EVT_CONN_ALL on this event group
     * and sets the bit of the new state (see notification_manager.h). NULL disables the signalling.
     */
    EventGroupHandle_t status_events;
    /**
     * Optional. Called just before the setup network is torn down, so HTTP sessions that ride on
     * it can be closed cleanly instead of being aborted underneath the server.
     */
    void (*on_setup_closing)(void);
  } wifi_bridge_config_t;

  typedef struct
  {
    bool associated; /**< joined the router */
    bool online;     /**< associated and has an IP address */
    char ssid[33];
    char bssid[18]; /**< "aa:bb:cc:dd:ee:ff" */
    uint8_t channel;
    int8_t rssi; /**< dBm */
    char ip[16];
    char gateway[16];
    char netmask[16];
  } wifi_bridge_link_t;

  typedef enum
  {
    WIFI_BRIDGE_PROV_IDLE,
    WIFI_BRIDGE_PROV_CONNECTING,
    WIFI_BRIDGE_PROV_SUCCESS,
    WIFI_BRIDGE_PROV_FAILED,
  } wifi_bridge_prov_state_t;

  typedef struct
  {
    bool setup_ap_active;
    char ap_ssid[33];
    char ap_ip[16];
    bool ap_secured;
    bool has_saved; /**< a network is stored in NVS */
    char saved_ssid[33];
    bool online;
    wifi_bridge_prov_state_t prov_state;
    char prov_ssid[33];   /**< network of the last provisioning attempt */
    char prov_reason[16]; /**< when failed: "not_found", "wrong_password", "timeout" or "failed" */
  } wifi_bridge_status_t;

  typedef struct
  {
    char ssid[33];
    int8_t rssi; /**< dBm */
    uint8_t channel;
    bool secure;
  } wifi_bridge_ap_t;

  /**
   * @brief Start Wi-Fi. Requires NVS, esp_netif_init() and the default event loop.
   *
   * Returns once the driver has started; it does not wait for a connection. Modem power save
   * is disabled so latency measurements are not skewed.
   */
  esp_err_t wifi_bridge_start(const wifi_bridge_config_t *cfg);

  /** @brief True while associated with an IP address. */
  bool wifi_bridge_is_online(void);

  /** @brief True while the setup network is open. */
  bool wifi_bridge_setup_ap_active(void);

  /** @brief Snapshot of the current link (fields are empty while not associated). */
  esp_err_t wifi_bridge_get_link(wifi_bridge_link_t *out);

  /** @brief Setup network, saved network and the state of the last provisioning attempt. */
  esp_err_t wifi_bridge_get_status(wifi_bridge_status_t *out);

  /**
   * @brief Blocking scan for nearby networks: unique names, strongest first, hidden ones skipped.
   *
   * Takes a few seconds. Fails with ESP_ERR_WIFI_STATE while a connection attempt is running.
   *
   * @param[out] out    Array to fill.
   * @param[in]  max    Capacity of @p out.
   * @param[out] count  Number of entries written.
   */
  esp_err_t wifi_bridge_scan(wifi_bridge_ap_t *out, size_t max, size_t *count);

  /**
   * @brief Try a router and, only if it works, save it to NVS. Non-blocking: poll
   *        wifi_bridge_get_status() for the result.
   *
   * @param ssid      1 to 32 bytes.
   * @param password  Empty for an open network, otherwise 8 to 63 characters.
   * @return ESP_ERR_INVALID_ARG for bad input, ESP_ERR_INVALID_STATE if the setup network is
   *         not open or an attempt is already running.
   */
  esp_err_t wifi_bridge_provision(const char *ssid, const char *password);

  /**
   * @brief Erase the saved network and restart after @p delay_ms, so the device comes back
   *        in setup mode. The delay lets an HTTP response finish first.
   */
  esp_err_t wifi_bridge_forget_and_restart(uint32_t delay_ms);

  /*
   * Router lab mode (CONFIG_WIFI_BRIDGE_ROUTER_MODE; the functions below only exist with it).
   *
   * While online, a separate WPA2 "lab network" routes its clients to the saved network through
   * IPv4 NAT. With capture on, every flow a lab client opens through the NAT (client, destination
   * address, port, protocol, counts) and the DNS names it asks are kept in RAM, in bounded tables.
   * No payload is ever stored. When the uplink goes, the lab network closes, and the usual retry and
   * setup network take over.
   */

  typedef struct
  {
    bool active; /**< lab network open and routing */
    char ssid[33];
    char ip[16];      /**< the device on the lab network */
    uint8_t channel;  /**< always the uplink's: there is one radio */
    uint8_t clients;  /**< devices joined */
    uint8_t max_clients;
    char problem[80]; /**< why the lab network did not open although online, or "" */
    bool capture;
    uint32_t packets; /**< sent by lab clients through the NAT, since boot or the last clear */
    uint64_t bytes;
    uint32_t flows_dropped; /**< pushed out of the full flow table */
    uint16_t flows_max;     /**< flow table slots */
    uint16_t dns_max;       /**< DNS names kept */
  } wifi_bridge_router_status_t;

  typedef struct
  {
    char mac[18];
    char ip[16]; /**< "" until it has a DHCP lease */
    int8_t rssi;
  } wifi_bridge_router_client_t;

  typedef struct
  {
    char client[16];
    char dst[16];
    uint16_t port; /**< destination port, 0 for ICMP */
    uint8_t proto; /**< IP protocol: 1 ICMP, 6 TCP, 17 UDP */
    uint32_t packets;
    uint64_t bytes; /**< client to destination only */
    uint32_t first_s; /**< seconds since boot */
    uint32_t last_s;
  } wifi_bridge_router_flow_t;

  typedef struct
  {
    char client[16];
    char name[64]; /**< longer names are cut */
    uint32_t at_s; /**< seconds since boot */
  } wifi_bridge_router_dns_t;

  esp_err_t wifi_bridge_router_get_status(wifi_bridge_router_status_t *out);
  /** @return number of entries written: the devices on the lab network. */
  size_t wifi_bridge_router_get_clients(wifi_bridge_router_client_t *out, size_t max);
  /** @brief The flow in table slot @p slot (0 to flows_max - 1). @return false for an empty slot. */
  bool wifi_bridge_router_get_flow(size_t slot, wifi_bridge_router_flow_t *out);
  /** @brief The @p age-th most recent DNS name (0 = newest, up to dns_max - 1). @return false if none. */
  bool wifi_bridge_router_get_dns(size_t age, wifi_bridge_router_dns_t *out);
  /** @brief Start or stop recording flows and DNS names. Takes effect on the next packet. */
  void wifi_bridge_router_set_capture(bool on);
  /** @brief Forget every flow and DNS name and zero the counters. */
  void wifi_bridge_router_clear(void);

#ifdef __cplusplus
}
#endif

#endif  // WIFI_BRIDGE_H
