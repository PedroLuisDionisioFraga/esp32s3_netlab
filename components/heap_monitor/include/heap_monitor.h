#ifndef HEAP_MONITOR_H
#define HEAP_MONITOR_H

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

  /**
   * @brief Receives one chunk of the memory document, in order.
   *
   * @param data Not NUL-terminated, valid only during the call.
   * @return false to stop, for example when the HTTP client went away.
   */
  typedef bool (*heap_monitor_sink_t)(const char *data, size_t len, void *ctx);

  /**
   * @brief Start heaptop with its menuconfig defaults (Component config > Heaptop).
   *
   * Heaptop's buffers go to PSRAM when the chip has it, else to internal RAM. Idempotent.
   */
  esp_err_t heap_monitor_init(void);

  /**
   * @brief Write the latest sample as one JSON document (heaptop_json_snapshot(): regions, health, trends, tasks).
   *
   * Streams through @p sink, so nothing is allocated on the heap it reports on. The snapshot copy it reads is
   * shared: call this from one task only (the HTTP server's, which runs every handler).
   *
   * @return ESP_OK; ESP_ERR_INVALID_STATE when heaptop is not running; ESP_ERR_NOT_FOUND before the first
   *         sample (both before anything was written); ESP_FAIL when @p sink stopped the document.
   */
  esp_err_t heap_monitor_write_json(heap_monitor_sink_t sink, void *ctx);

  /**
   * @brief Start a fresh measurement window: minimum free, failures, trends, leak history.
   *
   * Blocks until heaptop has published the cleared sample (normally a few ms). Stack high-water marks keep
   * their since-boot minimum: FreeRTOS cannot reset them.
   *
   * @return ESP_OK; ESP_ERR_INVALID_STATE when heaptop is not running; ESP_ERR_TIMEOUT when the cleared sample
   *         came late (the clear still applies to a later sample).
   */
  esp_err_t heap_monitor_clear(void);

#ifdef __cplusplus
}
#endif

#endif  // HEAP_MONITOR_H
