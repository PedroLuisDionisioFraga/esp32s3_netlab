#include "heap_monitor.h"

#include "esp_check.h"
#include "esp_log.h"
#include "heaptop.h"
#include "heaptop_json.h"

static const char *TAG = "heap_monitor";

typedef struct
{
  bool running;
  heaptop_config_t cfg; /* what heaptop runs with: its thresholds go out with every document */
  /* About 2 KB: too big for the HTTP server's stack, so it is kept here. One reader (the HTTP task), no lock. */
  heaptop_snapshot_t snap;
} heap_monitor_priv_t;

static heap_monitor_priv_t s_priv;

esp_err_t heap_monitor_init(void)
{
  if (s_priv.running)
    return ESP_OK;

  const heaptop_config_t cfg = HEAPTOP_CONFIG_DEFAULT();
  ESP_RETURN_ON_ERROR(heaptop_init(&cfg), TAG, "start heaptop");
  s_priv.cfg = cfg;
  s_priv.running = true;
  return ESP_OK;
}

esp_err_t heap_monitor_write_json(heap_monitor_sink_t sink, void *ctx)
{
  ESP_RETURN_ON_FALSE(sink, ESP_ERR_INVALID_ARG, TAG, "sink is NULL");
  if (!s_priv.running)
    return ESP_ERR_INVALID_STATE;

  esp_err_t err = heaptop_get_snapshot(&s_priv.snap);
  if (err != ESP_OK)
    return ESP_ERR_INVALID_STATE;
  if (s_priv.snap.seq == 0)
    return ESP_ERR_NOT_FOUND;

  return heaptop_json_snapshot(&s_priv.snap, &s_priv.cfg.thresholds, HEAPTOP_JSON_ALL, sink, ctx) ? ESP_OK : ESP_FAIL;
}

esp_err_t heap_monitor_clear(void)
{
  if (!s_priv.running)
    return ESP_ERR_INVALID_STATE;
  return heaptop_clear();
}
