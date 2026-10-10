#ifndef WIFI_BRIDGE_PRIV_H
#define WIFI_BRIDGE_PRIV_H

/* Internal to the wifi_bridge component. */

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "wifi_bridge.h"

/* Saved router credentials (NVS namespace "netlab"). */

/** @return ESP_OK, or ESP_ERR_NOT_FOUND when no network is saved. */
esp_err_t wifi_bridge_nvs_load(char *ssid, size_t ssid_size, char *password, size_t password_size);
esp_err_t wifi_bridge_nvs_save(const char *ssid, const char *password);
/** Erasing when nothing is saved counts as success. */
esp_err_t wifi_bridge_nvs_erase(void);

/* Captive-portal DNS: answers every IPv4 lookup with the setup network's own address. */

/** @param ip_addr  The address to answer with and to listen on, in lwIP (network) byte order. */
esp_err_t dns_catch_all_start(uint32_t ip_addr);
/** Blocks briefly until the server task has closed its socket. */
void dns_catch_all_stop(void);

/* Router lab mode: what lab clients send through the NAT (traffic_observer.c). */

/** @param lab_netif  The lwIP netif of the lab network, or NULL while it is closed. */
void traffic_observer_attach(void *lab_netif);
/** Fills the capture flag and the counters of @p out. */
void traffic_observer_get_totals(wifi_bridge_router_status_t *out);

#endif  // WIFI_BRIDGE_PRIV_H
