#include "chip_health.h"

#include <string.h>

#include "driver/temperature_sensor.h"
#include "esp_check.h"
#include "esp_chip_info.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "sdkconfig.h"

/* The sensor picks the most accurate hardware range that covers this window. Idle
 * die temperature with Wi-Fi is well inside it; heavy NAT traffic still fits. */
#define TEMP_RANGE_MIN_C 10
#define TEMP_RANGE_MAX_C 80

static const char *TAG = "chip_health";

static temperature_sensor_handle_t s_sensor;

static const char *reset_reason_name(esp_reset_reason_t reason)
{
  switch (reason)
  {
    case ESP_RST_POWERON:
      return "poweron";
    case ESP_RST_EXT:
      return "external";
    case ESP_RST_SW:
      return "software";
    case ESP_RST_PANIC:
      return "panic";
    case ESP_RST_INT_WDT:
      return "interrupt_wdt";
    case ESP_RST_TASK_WDT:
      return "task_wdt";
    case ESP_RST_WDT:
      return "wdt";
    case ESP_RST_DEEPSLEEP:
      return "deepsleep";
    case ESP_RST_BROWNOUT:
      return "brownout";
    default:
      return "unknown";
  }
}

esp_err_t chip_health_init(void)
{
  temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(TEMP_RANGE_MIN_C, TEMP_RANGE_MAX_C);
  ESP_RETURN_ON_ERROR(temperature_sensor_install(&cfg, &s_sensor), TAG, "install temperature sensor");

  esp_err_t err = temperature_sensor_enable(s_sensor);
  if (err != ESP_OK)
  {
    temperature_sensor_uninstall(s_sensor);
    s_sensor = NULL;
    ESP_LOGE(TAG, "enable temperature sensor: %s", esp_err_to_name(err));
    return err;
  }
  return ESP_OK;
}

void chip_health_read(chip_health_snapshot_t *out)
{
  memset(out, 0, sizeof(*out));

  if (s_sensor)
  {
    float celsius = 0;
    if (temperature_sensor_get_celsius(s_sensor, &celsius) == ESP_OK)
    {
      out->temperature_valid = true;
      out->temperature_c = celsius;
    }
  }

  esp_chip_info_t info;
  esp_chip_info(&info);

  out->uptime_s = (uint32_t)(esp_timer_get_time() / 1000000);
  out->free_heap = esp_get_free_heap_size();
  out->min_free_heap = esp_get_minimum_free_heap_size();
  out->cores = info.cores;
  out->chip = CONFIG_IDF_TARGET;
  out->idf_version = esp_get_idf_version();
  out->reset_reason = reset_reason_name(esp_reset_reason());
}
