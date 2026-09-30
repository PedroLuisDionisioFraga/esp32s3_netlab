/**
 * @file led_device.h
 * @brief Hardware layer for the single onboard WS2812 (addressable RGB) LED.
 *
 * Wraps the led_strip driver behind a small set/off API and applies the brightness cap to every
 * color. No task or timer is created: the caller decides when the LED changes (notification_manager
 * is the only caller and writes from a single task).
 */

#ifndef LED_DEVICE_H
#define LED_DEVICE_H

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

  typedef struct led_device
  {
    int gpio;                   /**< Data pin. Set by the caller before led_device_init(). */
    uint8_t brightness_percent; /**< 1 to 100. Set by the caller before led_device_init(). */
    void *_strip;               /**< Private: the led_strip handle, created by led_device_init(). */
  } led_device_t;

  /**
   * @brief Create the LED on @c self->gpio and leave it off.
   *
   * @param self Device with @c gpio and @c brightness_percent populated and everything else zero.
   * @return ESP_OK on success, ESP_ERR_INVALID_ARG if @p self is NULL or the brightness is out of
   *         range, or any error from the led_strip driver.
   */
  esp_err_t led_device_init(led_device_t *self);

  /**
   * @brief Show a color, scaled by the brightness cap.
   *
   * @return ESP_OK on success, ESP_ERR_INVALID_ARG if @p self is NULL, ESP_ERR_INVALID_STATE if the
   *         device was not initialised, or any error from the led_strip driver.
   */
  esp_err_t led_device_set_rgb(led_device_t *self, uint8_t r, uint8_t g, uint8_t b);

  /** @brief Turn the LED off. Same errors as led_device_set_rgb(). */
  esp_err_t led_device_off(led_device_t *self);

#ifdef __cplusplus
}
#endif

#endif  // LED_DEVICE_H
