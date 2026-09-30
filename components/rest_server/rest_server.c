#include "rest_server.h"

#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "cJSON.h"
#include "chip_health.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "notification_manager.h"
#include "wifi_bridge.h"

#define BASE_PATH_MAX     16
#define HOSTNAME_MAX      32
#define FILE_PATH_MAX     192
#define SCRATCH_BUFSIZE   4096
#define BODY_RECV_RETRIES 3
#define SCAN_RESULTS_MAX  20
#define RESTART_DELAY_MS  500

static const char *TAG = "rest_server";

/* The setup page is compiled into the firmware (see EMBED_FILES in CMakeLists.txt). */
extern const char setup_html_start[] asm("_binary_setup_html_start");
extern const char setup_html_end[] asm("_binary_setup_html_end");

typedef struct
{
  char base_path[BASE_PATH_MAX];
  char hostname[HOSTNAME_MAX];
  char scratch[SCRATCH_BUFSIZE]; /* shared by all handlers: esp_http_server runs them on one task */
} rest_ctx_t;

/* ---------- helpers ---------- */

static esp_err_t send_json(httpd_req_t *req, cJSON *root)
{
  char *body = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (!body)
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  esp_err_t err = httpd_resp_sendstr(req, body);
  cJSON_free(body);
  return err;
}

/* `status` must be a string literal: it is only read when the response is sent. */
static esp_err_t send_error(httpd_req_t *req, const char *status, const char *message)
{
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, "text/plain");
  return httpd_resp_sendstr(req, message);
}

/* Reads and parses the request body. Returns NULL after sending the error response itself. */
static cJSON *recv_json_body(httpd_req_t *req)
{
  rest_ctx_t *ctx = req->user_ctx;
  size_t total = req->content_len;

  if (total == 0 || total >= sizeof(ctx->scratch))
  {
    send_error(req, "400 Bad Request", "Missing or oversized JSON body");
    return NULL;
  }

  size_t received = 0;
  int retries = BODY_RECV_RETRIES;
  while (received < total)
  {
    int n = httpd_req_recv(req, ctx->scratch + received, total - received);
    if (n == HTTPD_SOCK_ERR_TIMEOUT && retries-- > 0)
      continue;

    if (n <= 0)
    {
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read request body");
      return NULL;
    }
    received += n;
  }
  ctx->scratch[total] = '\0';

  cJSON *root = cJSON_Parse(ctx->scratch);
  if (!root)
    send_error(req, "400 Bad Request", "Invalid JSON");

  return root;
}

static void add_led_json(cJSON *parent)
{
  notification_output_t led;
  notification_manager_get_output(&led);

  cJSON *obj = cJSON_AddObjectToObject(parent, "led");
  if (!obj)
    return;

  cJSON_AddStringToObject(obj, "mode", led.manual ? "manual" : "auto");
  cJSON_AddNumberToObject(obj, "r", led.r);
  cJSON_AddNumberToObject(obj, "g", led.g);
  cJSON_AddNumberToObject(obj, "b", led.b);
}

static const char *prov_state_name(wifi_bridge_prov_state_t state)
{
  switch (state)
  {
    case WIFI_BRIDGE_PROV_CONNECTING:
      return "connecting";
    case WIFI_BRIDGE_PROV_SUCCESS:
      return "success";
    case WIFI_BRIDGE_PROV_FAILED:
      return "failed";
    default:
      return "idle";
  }
}

/* ---------- API handlers ---------- */

static esp_err_t system_info_get_handler(httpd_req_t *req)
{
  chip_health_snapshot_t health;
  chip_health_read(&health);

  cJSON *root = cJSON_CreateObject();
  if (!root)
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");

  cJSON_AddStringToObject(root, "chip", health.chip);
  cJSON_AddStringToObject(root, "idf_version", health.idf_version);
  cJSON_AddNumberToObject(root, "cores", health.cores);
  if (health.temperature_valid)
    cJSON_AddNumberToObject(root, "temperature_c", round((double)health.temperature_c * 10.0) / 10.0);
  else
    cJSON_AddNullToObject(root, "temperature_c");

  cJSON_AddNumberToObject(root, "uptime_s", health.uptime_s);
  cJSON_AddNumberToObject(root, "free_heap", health.free_heap);
  cJSON_AddNumberToObject(root, "min_free_heap", health.min_free_heap);
  cJSON_AddStringToObject(root, "reset_reason", health.reset_reason);
  add_led_json(root);
  return send_json(req, root);
}

static esp_err_t link_get_handler(httpd_req_t *req)
{
  wifi_bridge_link_t link;
  wifi_bridge_get_link(&link);

  cJSON *root = cJSON_CreateObject();
  if (!root)
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");

  cJSON_AddBoolToObject(root, "associated", link.associated);
  cJSON_AddBoolToObject(root, "online", link.online);
  if (link.associated)
  {
    cJSON_AddStringToObject(root, "ssid", link.ssid);
    cJSON_AddStringToObject(root, "bssid", link.bssid);
    cJSON_AddNumberToObject(root, "channel", link.channel);
    cJSON_AddNumberToObject(root, "rssi", link.rssi);
  }
  if (link.online)
  {
    cJSON_AddStringToObject(root, "ip", link.ip);
    cJSON_AddStringToObject(root, "gateway", link.gateway);
    cJSON_AddStringToObject(root, "netmask", link.netmask);
  }
  return send_json(req, root);
}

/* Body: {"r":0-255,"g":0-255,"b":0-255} forces a color, {"mode":"auto"} returns to the status color. */
static esp_err_t led_post_handler(httpd_req_t *req)
{
  cJSON *body = recv_json_body(req);
  if (!body)
    return ESP_OK; /* error response already sent */

  const cJSON *mode = cJSON_GetObjectItemCaseSensitive(body, "mode");
  if (cJSON_IsString(mode) && strcmp(mode->valuestring, "auto") == 0)
  {
    notification_manager_clear_manual_color();
  }
  else
  {
    static const char *const keys[3] = {"r", "g", "b"};
    int rgb[3];
    for (int i = 0; i < 3; i++)
    {
      const cJSON *item = cJSON_GetObjectItemCaseSensitive(body, keys[i]);
      if (!cJSON_IsNumber(item) || item->valueint < 0 || item->valueint > 255)
      {
        cJSON_Delete(body);
        return send_error(req, "400 Bad Request", "Expected {\"r\",\"g\",\"b\"} in 0..255 or {\"mode\":\"auto\"}");
      }
      rgb[i] = item->valueint;
    }
    notification_manager_set_manual_color((uint8_t)rgb[0], (uint8_t)rgb[1], (uint8_t)rgb[2]);
  }
  cJSON_Delete(body);

  cJSON *root = cJSON_CreateObject();
  if (!root)
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");

  cJSON_AddBoolToObject(root, "ok", true);
  add_led_json(root);
  return send_json(req, root);
}

static esp_err_t wifi_status_get_handler(httpd_req_t *req)
{
  rest_ctx_t *ctx = req->user_ctx;
  wifi_bridge_status_t st;
  wifi_bridge_get_status(&st);

  cJSON *root = cJSON_CreateObject();
  if (!root)
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");

  cJSON_AddStringToObject(root, "hostname", ctx->hostname);
  cJSON_AddBoolToObject(root, "online", st.online);
  if (st.has_saved)
    cJSON_AddStringToObject(root, "saved_ssid", st.saved_ssid);
  else
    cJSON_AddNullToObject(root, "saved_ssid");

  cJSON *ap = cJSON_AddObjectToObject(root, "setup_ap");
  if (ap)
  {
    cJSON_AddBoolToObject(ap, "active", st.setup_ap_active);
    if (st.setup_ap_active)
    {
      cJSON_AddStringToObject(ap, "ssid", st.ap_ssid);
      cJSON_AddStringToObject(ap, "ip", st.ap_ip);
      cJSON_AddBoolToObject(ap, "secured", st.ap_secured);
    }
  }

  cJSON *prov = cJSON_AddObjectToObject(root, "provision");
  if (prov)
  {
    cJSON_AddStringToObject(prov, "state", prov_state_name(st.prov_state));
    if (st.prov_state != WIFI_BRIDGE_PROV_IDLE)
      cJSON_AddStringToObject(prov, "ssid", st.prov_ssid);
    if (st.prov_state == WIFI_BRIDGE_PROV_FAILED)
      cJSON_AddStringToObject(prov, "reason", st.prov_reason);
  }
  return send_json(req, root);
}

/* Blocks for a few seconds while the radio scans. */
static esp_err_t wifi_scan_get_handler(httpd_req_t *req)
{
  wifi_bridge_ap_t *networks = calloc(SCAN_RESULTS_MAX, sizeof(*networks));
  if (!networks)
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");

  size_t count = 0;
  esp_err_t err = wifi_bridge_scan(networks, SCAN_RESULTS_MAX, &count);
  if (err != ESP_OK)
  {
    free(networks);
    ESP_LOGW(TAG, "Scan failed: %s", esp_err_to_name(err));
    return send_error(req, "503 Service Unavailable", "Scan failed, try again in a moment");
  }

  cJSON *root = cJSON_CreateObject();
  cJSON *list = root ? cJSON_AddArrayToObject(root, "networks") : NULL;
  if (!list)
  {
    cJSON_Delete(root);
    free(networks);
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
  }
  for (size_t i = 0; i < count; i++)
  {
    cJSON *item = cJSON_CreateObject();
    if (!item)
      break;

    cJSON_AddStringToObject(item, "ssid", networks[i].ssid);
    cJSON_AddNumberToObject(item, "rssi", networks[i].rssi);
    cJSON_AddNumberToObject(item, "channel", networks[i].channel);
    cJSON_AddBoolToObject(item, "secure", networks[i].secure);
    cJSON_AddItemToArray(list, item);
  }
  free(networks);
  return send_json(req, root);
}

/* Body: {"ssid":"...","password":"..."}. Answers 202 at once; poll /wifi/status for the result. */
static esp_err_t wifi_provision_post_handler(httpd_req_t *req)
{
  cJSON *body = recv_json_body(req);
  if (!body)
    return ESP_OK; /* error response already sent */

  const cJSON *ssid = cJSON_GetObjectItemCaseSensitive(body, "ssid");
  const cJSON *password = cJSON_GetObjectItemCaseSensitive(body, "password");
  const char *ssid_str = cJSON_IsString(ssid) ? ssid->valuestring : NULL;
  const char *pass_str = cJSON_IsString(password) ? password->valuestring : "";

  const char *problem = NULL;
  esp_err_t err = ESP_OK;
  if (!ssid_str || ssid_str[0] == '\0' || strlen(ssid_str) > 32)
    problem = "Network name must be 1 to 32 characters";
  else if (pass_str[0] != '\0' && (strlen(pass_str) < 8 || strlen(pass_str) > 63))
    problem = "Password must be empty or 8 to 63 characters";
  else
    err = wifi_bridge_provision(ssid_str, pass_str); /* copies both strings */
  cJSON_Delete(body);

  if (problem)
    return send_error(req, "400 Bad Request", problem);

  if (err == ESP_ERR_INVALID_STATE)
    return send_error(req, "409 Conflict", "The setup network is not open, or a connection attempt is already running");

  if (err != ESP_OK)
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not start the connection attempt");

  cJSON *root = cJSON_CreateObject();
  if (!root)
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");

  cJSON_AddBoolToObject(root, "ok", true);
  httpd_resp_set_status(req, "202 Accepted");
  return send_json(req, root);
}

/* Forgets the saved network and restarts into setup mode. */
static esp_err_t wifi_forget_post_handler(httpd_req_t *req)
{
  if (wifi_bridge_forget_and_restart(RESTART_DELAY_MS) != ESP_OK)
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not erase the saved network");

  cJSON *root = cJSON_CreateObject();
  if (!root)
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");

  cJSON_AddBoolToObject(root, "ok", true);
  return send_json(req, root);
}

/* ---------- setup portal ---------- */

static esp_err_t send_setup_page(httpd_req_t *req)
{
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, setup_html_start, setup_html_end - setup_html_start);
}

/* Phones probe a well-known URL to detect a captive portal; a redirect (with a body, which iOS
 * requires) makes them offer to open the setup page. */
static esp_err_t send_setup_redirect(httpd_req_t *req)
{
  wifi_bridge_status_t st;
  wifi_bridge_get_status(&st);

  char location[32];
  snprintf(location, sizeof(location), "http://%s/", st.ap_ip);
  httpd_resp_set_status(req, "303 See Other");
  httpd_resp_set_hdr(req, "Location", location);
  return httpd_resp_send(req, "Redirect to the setup page", HTTPD_RESP_USE_STRLEN);
}

/* ---------- static web UI ---------- */

static const struct
{
  const char *ext;
  const char *type;
} k_content_types[] = {
  {".html", "text/html"},
  {".js", "application/javascript"},
  {".mjs", "application/javascript"},
  {".css", "text/css"},
  {".json", "application/json"},
  {".svg", "image/svg+xml"},
  {".png", "image/png"},
  {".ico", "image/x-icon"},
  {".woff2", "font/woff2"},
  {".webmanifest", "application/manifest+json"},
  {".txt", "text/plain"},
};

static bool has_extension(const char *path, const char *ext)
{
  size_t path_len = strlen(path);
  size_t ext_len = strlen(ext);
  return path_len >= ext_len && strcasecmp(path + path_len - ext_len, ext) == 0;
}

static void set_file_headers(httpd_req_t *req, const char *filepath)
{
  const char *type = "application/octet-stream";
  for (size_t i = 0; i < sizeof(k_content_types) / sizeof(k_content_types[0]); i++)
  {
    if (has_extension(filepath, k_content_types[i].ext))
    {
      type = k_content_types[i].type;
      break;
    }
  }
  httpd_resp_set_type(req, type);

  /* Vite fingerprints everything under /assets, so it can be cached forever;
   * index.html must be re-checked so a re-flash is picked up. */
  if (strstr(filepath, "/assets/"))
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=31536000, immutable");
  else
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
}

/* Every GET that is not an API route. */
static esp_err_t static_get_handler(httpd_req_t *req)
{
  rest_ctx_t *ctx = req->user_ctx;

  /* While the setup network is open this is a captive portal: the setup page at "/", and a
   * redirect to it for everything else (phone connectivity probes, mistyped addresses). */
  if (wifi_bridge_setup_ap_active())
  {
    bool is_root = (strcmp(req->uri, "/") == 0) || (strncmp(req->uri, "/?", 2) == 0);
    return is_root ? send_setup_page(req) : send_setup_redirect(req);
  }

  if (ctx->base_path[0] == '\0')
    return httpd_resp_send_404(req);

  /* Ignore the query string and refuse anything that could climb out of the web root. */
  size_t path_len = strcspn(req->uri, "?#");
  if (path_len == 0 || strstr(req->uri, ".."))
    return httpd_resp_send_404(req);


  char filepath[FILE_PATH_MAX];
  const char *index_name = (req->uri[path_len - 1] == '/') ? "index.html" : "";
  int written = snprintf(filepath, sizeof(filepath), "%s%.*s%s", ctx->base_path, (int)path_len, req->uri, index_name);
  if (written < 0 || written >= (int)sizeof(filepath))
    return httpd_resp_send_404(req);

  int fd = open(filepath, O_RDONLY, 0);
  if (fd < 0)
    return httpd_resp_send_404(req);

  set_file_headers(req, filepath);

  ssize_t n;
  do
  {
    n = read(fd, ctx->scratch, sizeof(ctx->scratch));
    if (n < 0)
    {
      ESP_LOGE(TAG, "Read failed: %s", filepath);
      break;
    }
    if (n > 0 && httpd_resp_send_chunk(req, ctx->scratch, n) != ESP_OK)
    {
      close(fd);
      ESP_LOGW(TAG, "Client dropped while sending %s", filepath);
      return ESP_FAIL;
    }
  } while (n > 0);
  close(fd);

  /* An empty chunk terminates the response. */
  return httpd_resp_send_chunk(req, NULL, 0);
}

/* ---------- server ---------- */

esp_err_t rest_server_start(const rest_server_config_t *cfg)
{
  httpd_handle_t server = NULL;

  ESP_RETURN_ON_FALSE(cfg, ESP_ERR_INVALID_ARG, TAG, "cfg is NULL");
  /* Checked before allocating, so this failure has nothing to free. */
  ESP_RETURN_ON_FALSE(!cfg->web_base_path || strlen(cfg->web_base_path) < BASE_PATH_MAX,
                      ESP_ERR_INVALID_ARG,
                      TAG,
                      "Web base path too long");

  rest_ctx_t *ctx = calloc(1, sizeof(*ctx));
  ESP_RETURN_ON_FALSE(ctx, ESP_ERR_NO_MEM, TAG, "No memory for REST context");

  if (cfg->web_base_path)
    strlcpy(ctx->base_path, cfg->web_base_path, sizeof(ctx->base_path));
  strlcpy(ctx->hostname, cfg->hostname ? cfg->hostname : "netlab", sizeof(ctx->hostname));

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.uri_match_fn = httpd_uri_match_wildcard;
  config.max_uri_handlers = 16; /* room for the endpoints added in later milestones */
  config.stack_size = 6144;
  config.lru_purge_enable = true; /* browsers open several sockets; recycle the oldest when full */

  esp_err_t err = httpd_start(&server, &config);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
    free(ctx);
    return err;
  }

  const httpd_uri_t routes[] = {
    {.uri = "/api/v1/system/info", .method = HTTP_GET, .handler = system_info_get_handler, .user_ctx = ctx},
    {.uri = "/api/v1/link", .method = HTTP_GET, .handler = link_get_handler, .user_ctx = ctx},
    {.uri = "/api/v1/led", .method = HTTP_POST, .handler = led_post_handler, .user_ctx = ctx},
    {.uri = "/api/v1/wifi/status", .method = HTTP_GET, .handler = wifi_status_get_handler, .user_ctx = ctx},
    {.uri = "/api/v1/wifi/scan", .method = HTTP_GET, .handler = wifi_scan_get_handler, .user_ctx = ctx},
    {.uri = "/api/v1/wifi/provision", .method = HTTP_POST, .handler = wifi_provision_post_handler, .user_ctx = ctx},
    {.uri = "/api/v1/wifi/forget", .method = HTTP_POST, .handler = wifi_forget_post_handler, .user_ctx = ctx},
    /* Last, so the API routes above win over the wildcard. It always exists: the setup
     * portal needs it even when no web UI is mounted. */
    {.uri = "/*", .method = HTTP_GET, .handler = static_get_handler, .user_ctx = ctx},
  };
  for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++)
  {
    err = httpd_register_uri_handler(server, &routes[i]);
    if (err != ESP_OK)
    {
      ESP_LOGE(TAG, "Failed to register %s: %s", routes[i].uri, esp_err_to_name(err));
      httpd_stop(server);
      free(ctx);
      return err;
    }
  }

  ESP_LOGI(TAG, "HTTP server started (web UI %s)", cfg->web_base_path ? cfg->web_base_path : "not served");
  return ESP_OK;
}
