#include "heap_monitor.h"

#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "heaptop.h"
/* Private to heaptop (see CMakeLists.txt): the renderers, failure log and alert limits of `ht`. */
#include "heaptop_priv.h"
#include "heaptop_render.h"

/* What the `ht` console prints after a frame that did not fit. */
#define TRUNCATED_NOTE "... (output truncated)\n"

static const char *TAG = "heap_monitor";

typedef struct
{
  SemaphoreHandle_t lock;   /* guards everything below; NULL until init succeeded */
  heaptop_snapshot_t *snap; /* the sample rendered last; seq 0 = none */
  heaptop_fail_t fails[HEAPTOP_FAIL_LEN];
  uint16_t fail_count;
} heap_monitor_priv_t;

static heap_monitor_priv_t s_priv;

static heaptop_sort_t to_heaptop_sort(heap_monitor_sort_t sort)
{
  switch (sort)
  {
    case HEAP_MONITOR_SORT_HEAP:
      return HEAPTOP_SORT_HEAP;
    case HEAP_MONITOR_SORT_STACK:
      return HEAPTOP_SORT_STACK;
    case HEAP_MONITOR_SORT_NAME:
      return HEAPTOP_SORT_NAME;
    default:
      return HEAPTOP_SORT_CPU;
  }
}

esp_err_t heap_monitor_init(void)
{
  if (s_priv.lock)
    return ESP_OK;

  ESP_RETURN_ON_ERROR(heaptop_init(NULL), TAG, "start heaptop");

  /* About 2 KB: too big for the HTTP server's stack, so it is copied here. */
  s_priv.snap = calloc(1, sizeof(*s_priv.snap));
  SemaphoreHandle_t lock = xSemaphoreCreateMutex();
  if (!s_priv.snap || !lock)
  {
    free(s_priv.snap);
    s_priv.snap = NULL;
    if (lock)
      vSemaphoreDelete(lock);
    heaptop_deinit();
    ESP_LOGE(TAG, "No memory for the snapshot copy");
    return ESP_ERR_NO_MEM;
  }
  s_priv.lock = lock;
  return ESP_OK;
}

/* Same calls as the matching `ht` subcommand in heaptop_console.c. */
static void render_view(heaptop_buf_t *b, const heap_monitor_opts_t *opts)
{
  const heaptop_sort_t sort = to_heaptop_sort(opts->sort);
  switch (opts->view)
  {
    case HEAP_MONITOR_VIEW_HEAP:
      heaptop_render_heap(b, s_priv.snap);
      break;
    case HEAP_MONITOR_VIEW_TASKS:
      heaptop_render_tasks(b, s_priv.snap, sort);
      break;
    case HEAP_MONITOR_VIEW_HEALTH:
    {
      heaptop_thresholds_t th;
      heaptop_alerts_thresholds(&th);
      heaptop_render_health(b, s_priv.snap, &th, s_priv.fails, s_priv.fail_count);
      break;
    }
    default:
    {
      const heaptop_top_view_t view = {.sort = sort, .paused = opts->paused, .refresh_ms = opts->refresh_ms};
      heaptop_render_top(b, s_priv.snap, &view);
      break;
    }
  }
}

esp_err_t heap_monitor_render(const heap_monitor_opts_t *opts, char *out, size_t len)
{
  ESP_RETURN_ON_FALSE(opts && out && len > sizeof(TRUNCATED_NOTE), ESP_ERR_INVALID_ARG, TAG, "bad buffer");
  out[0] = '\0';
  if (!s_priv.lock)
    return ESP_ERR_INVALID_STATE;

  xSemaphoreTake(s_priv.lock, portMAX_DELAY);
  /* Paused, like the p key of `ht top`: render the sample shown last again, so a new sort or view
   * does not swap the data. The copy is shared, so another, unpaused browser tab moves it on. */
  esp_err_t err = ESP_OK;
  if (!opts->paused || s_priv.snap->seq == 0)
  {
    err = heaptop_get_snapshot(s_priv.snap);
    if (err == ESP_OK)
      s_priv.fail_count = heaptop_fails_copy(s_priv.fails, HEAPTOP_FAIL_LEN);
  }
  if (err == ESP_OK && s_priv.snap->seq == 0)
    err = ESP_ERR_NOT_FOUND;

  if (err == ESP_OK)
  {
    /* The note's room is kept back, so a cut frame still ends like the console's. */
    heaptop_buf_t b;
    heaptop_buf_init(&b, out, len - (sizeof(TRUNCATED_NOTE) - 1));
    render_view(&b, opts);
    if (b.truncated)
      memcpy(out + b.len, TRUNCATED_NOTE, sizeof(TRUNCATED_NOTE)); /* with its NUL */
  }
  xSemaphoreGive(s_priv.lock);
  return err;
}

esp_err_t heap_monitor_clear(void)
{
  if (!s_priv.lock)
    return ESP_ERR_INVALID_STATE;

  esp_err_t err = heaptop_clear();

  /* Drop the frozen sample: even a paused view shows the fresh window next. */
  xSemaphoreTake(s_priv.lock, portMAX_DELAY);
  s_priv.snap->seq = 0;
  xSemaphoreGive(s_priv.lock);
  return err;
}
