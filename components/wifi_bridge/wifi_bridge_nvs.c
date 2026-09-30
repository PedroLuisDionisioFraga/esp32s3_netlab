#include "esp_log.h"
#include "nvs.h"
#include "wifi_bridge_priv.h"

#define NVS_NAMESPACE "netlab"
#define KEY_SSID      "wifi_ssid"
#define KEY_PASSWORD  "wifi_pass"

static const char *TAG = "wifi_nvs";

esp_err_t wifi_bridge_nvs_load(char *ssid, size_t ssid_size, char *password, size_t password_size)
{
  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
  if (err == ESP_ERR_NVS_NOT_FOUND)
    return ESP_ERR_NOT_FOUND; /* the namespace does not exist yet: nothing was ever saved */

  if (err != ESP_OK)
    return err;

  size_t size = ssid_size;
  err = nvs_get_str(handle, KEY_SSID, ssid, &size);
  if (err == ESP_ERR_NVS_NOT_FOUND || (err == ESP_OK && ssid[0] == '\0'))
  {
    nvs_close(handle);
    return ESP_ERR_NOT_FOUND;
  }

  if (err == ESP_OK)
  {
    size = password_size;
    err = nvs_get_str(handle, KEY_PASSWORD, password, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
      password[0] = '\0'; /* open network */
      err = ESP_OK;
    }
  }
  nvs_close(handle);
  return err;
}

esp_err_t wifi_bridge_nvs_save(const char *ssid, const char *password)
{
  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK)
    return err;


  err = nvs_set_str(handle, KEY_SSID, ssid);
  if (err == ESP_OK)
  {
    err = nvs_set_str(handle, KEY_PASSWORD, password);
  }
  if (err == ESP_OK)
  {
    err = nvs_commit(handle);
  }
  nvs_close(handle);

  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "Saving the Wi-Fi network failed: %s", esp_err_to_name(err));
  }
  return err;
}

esp_err_t wifi_bridge_nvs_erase(void)
{
  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK)
    return err;


  esp_err_t err_ssid = nvs_erase_key(handle, KEY_SSID);
  esp_err_t err_pass = nvs_erase_key(handle, KEY_PASSWORD);
  if (err_ssid == ESP_ERR_NVS_NOT_FOUND)
    err_ssid = ESP_OK;

  if (err_pass == ESP_ERR_NVS_NOT_FOUND)
    err_pass = ESP_OK;

  err = (err_ssid != ESP_OK) ? err_ssid : err_pass;
  if (err == ESP_OK)
    err = nvs_commit(handle);

  nvs_close(handle);
  return err;
}
