/**
 * @file notification_manager.h
 * @brief Owns the onboard RGB LED and turns the device's state into a color.
 *
 * A single internal worker task is the only writer of the LED. Producers never call into it to change
 * the connection color: they set exactly one NOTIF_EVT_CONN_* bit on the event group returned by
 * notification_manager_get_event_group(), and the worker re-renders. The manual color set from the web
 * UI goes through the setters, which ask the worker to re-render; the hardware is touched only when the
 * color actually changes.
 *
 * Colors:
 *   - booting    : blue (until the first connection bit arrives)
 *   - setup      : purple, the setup Wi-Fi is open
 *   - connecting : amber
 *   - online     : green
 *   - offline    : red
 * A manual color overrides these until notification_manager_clear_manual_color().
 *
 * Singleton: the private context is file-static, so only one instance exists.
 */

#ifndef NOTIFICATION_MANAGER_H
#define NOTIFICATION_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* Connection-state event bits. A producer clears NOTIF_EVT_CONN_ALL and then sets exactly one of
 * these, so the worker always sees a single, latest state. They must stay one-hot and must not
 * collide with the manager's private render bit (which occupies the next bit position). */
#define NOTIF_EVT_CONN_SETUP      ((EventBits_t)(1 << 0))
#define NOTIF_EVT_CONN_CONNECTING ((EventBits_t)(1 << 1))
#define NOTIF_EVT_CONN_ONLINE     ((EventBits_t)(1 << 2))
#define NOTIF_EVT_CONN_OFFLINE    ((EventBits_t)(1 << 3))
#define NOTIF_EVT_CONN_ALL                                                                            \
  (NOTIF_EVT_CONN_SETUP | NOTIF_EVT_CONN_CONNECTING | NOTIF_EVT_CONN_ONLINE | NOTIF_EVT_CONN_OFFLINE)

  typedef struct
  {
    bool manual;     /**< true while a manual color overrides the status color */
    uint8_t r, g, b; /**< requested color, before the brightness cap */
  } notification_output_t;

  /**
   * @brief Initialise the LED, the status event group and the worker task, and show the boot color.
   *
   * On failure nothing is left allocated: notification_manager_get_event_group() returns NULL and
   * the other functions do nothing, so the rest of the firmware runs without the LED.
   *
   * @return ESP_OK on success, ESP_ERR_INVALID_STATE if already initialised, ESP_ERR_NO_MEM on
   *         allocation failure, or any error from led_device_init().
   */
  esp_err_t notification_manager_init(void);

  /**
   * @brief The status event group, shared so producers can drive the LED by setting one
   *        NOTIF_EVT_CONN_* bit.
   *
   * @return The event group handle, or NULL if the manager is not initialised.
   */
  EventGroupHandle_t notification_manager_get_event_group(void);

  /**
   * @brief Force a color (manual override). Returns before the LED changes: the worker applies it.
   *
   * @return ESP_OK, or ESP_ERR_INVALID_STATE if the manager is not initialised.
   */
  esp_err_t notification_manager_set_manual_color(uint8_t r, uint8_t g, uint8_t b);

  /**
   * @brief Go back to showing the status color.
   *
   * @return ESP_OK, or ESP_ERR_INVALID_STATE if the manager is not initialised.
   */
  esp_err_t notification_manager_clear_manual_color(void);

  /** @brief Current desired output, for reporting in the API. All zero if not initialised. */
  void notification_manager_get_output(notification_output_t *out);

#ifdef __cplusplus
}
#endif

#endif  // NOTIFICATION_MANAGER_H
