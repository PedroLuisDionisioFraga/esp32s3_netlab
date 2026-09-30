#include "sdkconfig.h"

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "mdns.h"
#include "nvs_flash.h"

#include "chip_health.h"
#include "notification_manager.h"
#include "reset_button.h"
#include "rest_server.h"
#include "wifi_bridge.h"

#if CONFIG_NETLAB_DEPLOY_WEB_PAGES
#include "esp_littlefs.h"
#endif

#define WEB_MOUNT_POINT    "/www"
#define WEB_PARTITION_NAME "www"

static const char *TAG = "netlab";

static void init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

static void init_mdns(void)
{
    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set(CONFIG_NETLAB_MDNS_HOSTNAME));
    ESP_ERROR_CHECK(mdns_instance_name_set("ESP32-S3 network lab"));

    mdns_txt_item_t txt[] = {
        {"chip", CONFIG_IDF_TARGET},
        {"path", "/"},
    };
    ESP_ERROR_CHECK(mdns_service_add("netlab-web", "_http", "_tcp", 80, txt, sizeof(txt) / sizeof(txt[0])));
}

/* Returns the mount point, or NULL when the web UI is not deployed or fails to mount
 * (the REST API and the Wi-Fi setup page keep working without it). */
static const char *mount_web_fs(void)
{
#if CONFIG_NETLAB_DEPLOY_WEB_PAGES
    esp_vfs_littlefs_conf_t conf = {
        .base_path = WEB_MOUNT_POINT,
        .partition_label = WEB_PARTITION_NAME,
        .format_if_mount_failed = false,
    };
    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot mount '%s' partition (%s): serving the REST API only", WEB_PARTITION_NAME,
                 esp_err_to_name(err));
        return NULL;
    }

    size_t total = 0;
    size_t used = 0;
    if (esp_littlefs_info(WEB_PARTITION_NAME, &total, &used) == ESP_OK) {
        ESP_LOGI(TAG, "Web UI partition: %u of %u bytes used", (unsigned)used, (unsigned)total);
    }
    return WEB_MOUNT_POINT;
#else
    return NULL;
#endif
}

void app_main(void)
{
    init_nvs();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* The LED, the temperature sensor and the button are conveniences: the lab still runs without them. */
    esp_err_t err = notification_manager_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Status LED disabled (%s)", esp_err_to_name(err));
    }

    err = chip_health_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Chip temperature sensor disabled (%s)", esp_err_to_name(err));
    }

    err = reset_button_start();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Reset button disabled (%s)", esp_err_to_name(err));
    }

    init_mdns();
    const char *web_root = mount_web_fs();

    /* The router credentials come from NVS. With none saved (first boot, or after moving to
     * another home) the device opens its own setup Wi-Fi: no rebuild or reflash is ever needed. */
    const wifi_bridge_config_t wifi_cfg = {
        .status_events = notification_manager_get_event_group(), /* NULL when the LED is disabled */
    };
    ESP_ERROR_CHECK(wifi_bridge_start(&wifi_cfg));

    const rest_server_config_t rest_cfg = {
        .web_base_path = web_root,
        .hostname = CONFIG_NETLAB_MDNS_HOSTNAME,
    };
    ESP_ERROR_CHECK(rest_server_start(&rest_cfg));
    ESP_LOGI(TAG, "Web UI at http://%s.local/", CONFIG_NETLAB_MDNS_HOSTNAME);
}
