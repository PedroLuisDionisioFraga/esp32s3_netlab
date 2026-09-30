#ifndef CHIP_HEALTH_H
#define CHIP_HEALTH_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

  typedef struct
  {
    bool temperature_valid; /**< false when the sensor is unavailable */
    float temperature_c;    /**< die temperature, NOT the room temperature */
    uint32_t uptime_s;
    uint32_t free_heap;
    uint32_t min_free_heap;
    uint8_t cores;
    const char *chip; /**< e.g. "esp32s3" */
    const char *idf_version;
    const char *reset_reason; /**< e.g. "poweron", "panic", "task_wdt" */
  } chip_health_snapshot_t;

  /**
   * @brief Install and enable the internal temperature sensor.
   *
   * On failure chip_health_read() still works and reports temperature_valid = false.
   */
  esp_err_t chip_health_init(void);

  /**
   * @brief Collect temperature, uptime, heap and reset information.
   */
  void chip_health_read(chip_health_snapshot_t *out);

#ifdef __cplusplus
}
#endif

#endif  // CHIP_HEALTH_H
