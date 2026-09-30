#include "notification_manager.h"

#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "led_device.h"
#include "sdkconfig.h"

static const char *TAG = "notification";

#define LED_TASK_STACK 4096
#define LED_TASK_PRIO  3

/* Internal "re-render" request. It takes the bit right after the public NOTIF_EVT_CONN_* bits so the
 * two never collide. */
#define EVT_RENDER ((EventBits_t)(1 << 4))

typedef enum
{
  STATE_BOOTING,
  STATE_SETUP,
  STATE_CONNECTING,
  STATE_ONLINE,
  STATE_OFFLINE,
} notification_state_t;

static const uint8_t k_state_rgb[][3] = {
  [STATE_BOOTING] = {0, 0, 255},
  [STATE_SETUP] = {150, 0, 255},
  [STATE_CONNECTING] = {255, 160, 0},
  [STATE_ONLINE] = {0, 255, 0},
  [STATE_OFFLINE] = {255, 0, 0},
};

typedef struct
{
  led_device_t led;
  EventGroupHandle_t events; /* connection bits + EVT_RENDER */
  SemaphoreHandle_t lock;    /* guards the desired state: the worker, the HTTP server and the button task touch it */
  TaskHandle_t task;

  /* Desired state (guarded by lock). */
  notification_state_t state;
  bool manual;
  uint8_t manual_rgb[3];

  /* Last color written to the LED (worker only). */
  uint8_t applied_rgb[3];
  bool applied_valid;
} notification_manager_priv_t;

static notification_manager_priv_t s_priv; /* zero-init; program lifetime */

static inline void lock(void)
{
  xSemaphoreTake(s_priv.lock, portMAX_DELAY);
}

static inline void unlock(void)
{
  xSemaphoreGive(s_priv.lock);
}

/* Caller holds the lock. */
static void current_rgb_locked(uint8_t rgb[3])
{
  memcpy(rgb, s_priv.manual ? s_priv.manual_rgb : k_state_rgb[s_priv.state], 3);
}

/* The only place that writes the LED. A render that changes nothing never touches the hardware. */
static void render(const uint8_t rgb[3])
{
  if (s_priv.applied_valid && memcmp(s_priv.applied_rgb, rgb, sizeof(s_priv.applied_rgb)) == 0)
    return;

  esp_err_t err = led_device_set_rgb(&s_priv.led, rgb[0], rgb[1], rgb[2]);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "LED write failed (%s)", esp_err_to_name(err));
    return; /* not remembered, so the next render tries again */
  }
  memcpy(s_priv.applied_rgb, rgb, sizeof(s_priv.applied_rgb));
  s_priv.applied_valid = true;
}

static void led_task(void *arg)
{
  (void)arg;
  for (;;)
  {
    EventBits_t bits =
      xEventGroupWaitBits(s_priv.events, NOTIF_EVT_CONN_ALL | EVT_RENDER, pdTRUE, pdFALSE, portMAX_DELAY);

    uint8_t rgb[3];
    lock();
    if (bits & NOTIF_EVT_CONN_SETUP)
      s_priv.state = STATE_SETUP;
    else if (bits & NOTIF_EVT_CONN_CONNECTING)
      s_priv.state = STATE_CONNECTING;
    else if (bits & NOTIF_EVT_CONN_ONLINE)
      s_priv.state = STATE_ONLINE;
    else if (bits & NOTIF_EVT_CONN_OFFLINE)
      s_priv.state = STATE_OFFLINE;
    current_rgb_locked(rgb);
    unlock();

    render(rgb);
  }
}

/* Releases what notification_manager_init() created, so a failed init leaves nothing behind. */
static void release_resources(void)
{
  if (s_priv.events)
    vEventGroupDelete(s_priv.events);
  if (s_priv.lock)
    vSemaphoreDelete(s_priv.lock);
  s_priv.events = NULL;
  s_priv.lock = NULL;
  s_priv.task = NULL;
}

esp_err_t notification_manager_init(void)
{
  ESP_RETURN_ON_FALSE(s_priv.task == NULL, ESP_ERR_INVALID_STATE, TAG, "already initialised");

  s_priv.state = STATE_BOOTING;
  s_priv.manual = false;
  s_priv.applied_valid = false;
  s_priv.led.gpio = CONFIG_STATUS_LED_GPIO;
  s_priv.led.brightness_percent = CONFIG_STATUS_LED_BRIGHTNESS_PERCENT;

  s_priv.lock = xSemaphoreCreateMutex();
  s_priv.events = xEventGroupCreate();
  if (!s_priv.lock || !s_priv.events)
  {
    ESP_LOGE(TAG, "no memory for the LED lock and events");
    release_resources();
    return ESP_ERR_NO_MEM;
  }

  esp_err_t err = led_device_init(&s_priv.led);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "init LED on GPIO%d: %s", CONFIG_STATUS_LED_GPIO, esp_err_to_name(err));
    release_resources();
    return err;
  }

  if (xTaskCreate(led_task, "notif_led", LED_TASK_STACK, NULL, LED_TASK_PRIO, &s_priv.task) != pdPASS)
  {
    ESP_LOGE(TAG, "create LED task failed");
    release_resources();
    return ESP_ERR_NO_MEM;
  }

  xEventGroupSetBits(s_priv.events, EVT_RENDER); /* show the boot color */
  return ESP_OK;
}

EventGroupHandle_t notification_manager_get_event_group(void)
{
  return s_priv.events;
}

esp_err_t notification_manager_set_manual_color(uint8_t r, uint8_t g, uint8_t b)
{
  if (s_priv.task == NULL)
    return ESP_ERR_INVALID_STATE;

  lock();
  s_priv.manual = true;
  s_priv.manual_rgb[0] = r;
  s_priv.manual_rgb[1] = g;
  s_priv.manual_rgb[2] = b;
  unlock();

  xEventGroupSetBits(s_priv.events, EVT_RENDER);
  return ESP_OK;
}

esp_err_t notification_manager_clear_manual_color(void)
{
  if (s_priv.task == NULL)
    return ESP_ERR_INVALID_STATE;

  lock();
  s_priv.manual = false;
  unlock();

  xEventGroupSetBits(s_priv.events, EVT_RENDER);
  return ESP_OK;
}

void notification_manager_get_output(notification_output_t *out)
{
  uint8_t rgb[3] = {0};
  bool manual = false;
  if (s_priv.task != NULL)
  {
    lock();
    manual = s_priv.manual;
    current_rgb_locked(rgb);
    unlock();
  }
  out->manual = manual;
  out->r = rgb[0];
  out->g = rgb[1];
  out->b = rgb[2];
}
