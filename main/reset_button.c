#include "reset_button.h"

#include "sdkconfig.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "notification_manager.h"
#include "wifi_bridge.h"

#define POLL_MS       100
#define RESTART_DELAY 300

static const char *TAG = "reset_button";

static void reset_button_task(void *arg)
{
    (void)arg;
    uint32_t held_ms = 0;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));

        if (gpio_get_level(CONFIG_NETLAB_RESET_BUTTON_GPIO) != 0) {
            held_ms = 0; /* released (the button pulls the pin low) */
            continue;
        }

        held_ms += POLL_MS;
        if (held_ms >= CONFIG_NETLAB_RESET_HOLD_MS) {
            ESP_LOGW(TAG, "Button held for %u ms: forgetting the Wi-Fi network", (unsigned)held_ms);
            notification_manager_set_manual_color(255, 255, 255); /* visible confirmation before the restart */
            wifi_bridge_forget_and_restart(RESTART_DELAY);
            vTaskDelay(portMAX_DELAY); /* the restart timer takes over */
        }
    }
}

esp_err_t reset_button_start(void)
{
    const gpio_config_t io_cfg = {
        .pin_bit_mask = 1ULL << CONFIG_NETLAB_RESET_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io_cfg), TAG, "configure GPIO%d", CONFIG_NETLAB_RESET_BUTTON_GPIO);

    BaseType_t created = xTaskCreate(reset_button_task, "reset_button", 3072, NULL, 3, NULL);
    ESP_RETURN_ON_FALSE(created == pdPASS, ESP_ERR_NO_MEM, TAG, "create button task");
    return ESP_OK;
}
