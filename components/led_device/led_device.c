#include "led_device.h"

#include "esp_check.h"
#include "esp_log.h"
#include "led_strip.h"

static const char *TAG = "led_device";

static uint8_t scale(uint8_t value, uint8_t percent)
{
  return (uint8_t)((value * percent) / 100);
}

esp_err_t led_device_init(led_device_t *self)
{
  ESP_RETURN_ON_FALSE(self != NULL, ESP_ERR_INVALID_ARG, TAG, "self is NULL");
  ESP_RETURN_ON_FALSE(self->brightness_percent >= 1 && self->brightness_percent <= 100,
                      ESP_ERR_INVALID_ARG,
                      TAG,
                      "brightness must be 1 to 100 percent");

  led_strip_config_t strip_cfg = {
    .strip_gpio_num = self->gpio,
    .max_leds = 1,
  };
  led_strip_rmt_config_t rmt_cfg = {
    .resolution_hz = 10 * 1000 * 1000, /* 10 MHz */
    .flags.with_dma = false,
  };

  led_strip_handle_t strip = NULL;
  ESP_RETURN_ON_ERROR(led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &strip),
                      TAG,
                      "create LED strip on GPIO%d",
                      self->gpio);

  esp_err_t err = led_strip_clear(strip);
  if (err != ESP_OK)
  {
    led_strip_del(strip);
    return err;
  }

  self->_strip = strip;
  return ESP_OK;
}

esp_err_t led_device_set_rgb(led_device_t *self, uint8_t r, uint8_t g, uint8_t b)
{
  ESP_RETURN_ON_FALSE(self != NULL, ESP_ERR_INVALID_ARG, TAG, "self is NULL");
  ESP_RETURN_ON_FALSE(self->_strip != NULL, ESP_ERR_INVALID_STATE, TAG, "LED is not initialised");

  led_strip_handle_t strip = self->_strip;
  uint8_t percent = self->brightness_percent;
  ESP_RETURN_ON_ERROR(led_strip_set_pixel(strip, 0, scale(r, percent), scale(g, percent), scale(b, percent)),
                      TAG,
                      "set pixel");
  return led_strip_refresh(strip);
}

esp_err_t led_device_off(led_device_t *self)
{
  return led_device_set_rgb(self, 0, 0, 0);
}
