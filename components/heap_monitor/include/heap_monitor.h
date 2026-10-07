#ifndef HEAP_MONITOR_H
#define HEAP_MONITOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** Text buffer for one frame: the size the `ht` console command uses. */
#define HEAP_MONITOR_TEXT_MAX 6144

/** Refresh default and range of `ht top`; it only changes the "refresh" field of the top header. */
#define HEAP_MONITOR_REFRESH_DEFAULT_MS 1000
#define HEAP_MONITOR_REFRESH_MIN_MS     100
#define HEAP_MONITOR_REFRESH_MAX_MS     10000

  /** Which `ht` subcommand to render. */
  typedef enum
  {
    HEAP_MONITOR_VIEW_TOP = 0, /**< `ht top`: one frame of the live view */
    HEAP_MONITOR_VIEW_HEAP,    /**< `ht heap` */
    HEAP_MONITOR_VIEW_TASKS,   /**< `ht tasks <sort>` */
    HEAP_MONITOR_VIEW_HEALTH,  /**< `ht health` */
  } heap_monitor_view_t;

  /** Task table order, as the c/m/s/n keys of `ht top`. */
  typedef enum
  {
    HEAP_MONITOR_SORT_CPU = 0, /**< highest CPU first */
    HEAP_MONITOR_SORT_HEAP,    /**< most heap held first */
    HEAP_MONITOR_SORT_STACK,   /**< lowest stack high-water mark first */
    HEAP_MONITOR_SORT_NAME,    /**< alphabetical */
  } heap_monitor_sort_t;

  typedef struct
  {
    heap_monitor_view_t view;
    heap_monitor_sort_t sort; /**< top and tasks only */
    uint32_t refresh_ms;      /**< top only: shown in its header */
    bool paused;              /**< render the sample of the previous call again, like the p key of `ht top`
                                   (top also shows "PAUSED") */
  } heap_monitor_opts_t;

  /**
   * @brief Start heaptop with its menuconfig defaults (Component config > Heaptop).
   *
   * Heaptop's buffers go to PSRAM when the chip has it, else to internal RAM.
   */
  esp_err_t heap_monitor_init(void);

  /**
   * @brief Render the latest sample (or, paused, the previous one) exactly as `ht` prints it.
   *
   * A frame that does not fit is cut and ends with "... (output truncated)", like on the console.
   *
   * @param out NUL-terminated text; HEAP_MONITOR_TEXT_MAX bytes hold any frame of up to 32 tasks.
   * @return ESP_OK; ESP_ERR_INVALID_STATE when heaptop is not running; ESP_ERR_NOT_FOUND before
   *         the first sample; ESP_ERR_INVALID_ARG for a NULL or too small buffer.
   */
  esp_err_t heap_monitor_render(const heap_monitor_opts_t *opts, char *out, size_t len);

  /**
   * @brief Start a fresh measurement window, like `ht clear`.
   *
   * Blocks until heaptop has published the cleared sample (normally a few ms). The next render
   * takes that sample, even when paused.
   *
   * @return ESP_OK; ESP_ERR_INVALID_STATE when heaptop is not running; ESP_ERR_TIMEOUT when the
   *         cleared sample came late (the clear still applies to a later sample).
   */
  esp_err_t heap_monitor_clear(void);

#ifdef __cplusplus
}
#endif

#endif  // HEAP_MONITOR_H
