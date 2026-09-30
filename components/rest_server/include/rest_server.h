#ifndef REST_SERVER_H
#define REST_SERVER_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

  typedef struct
  {
    /** Mounted directory holding the built web UI (e.g. "/www"), served for every GET that is
     *  not an API route. NULL serves the API only. */
    const char *web_base_path;
    /** mDNS name without ".local" (e.g. "netlab"); the setup page tells the user to open it. */
    const char *hostname;
  } rest_server_config_t;

  /**
   * @brief Start the HTTP server with the JSON API under /api/v1.
   *
   * While the Wi-Fi setup network is open, every GET that is not an API route is answered with
   * the built-in setup page (or a redirect to it), which makes phones open it as a captive
   * portal. The page is compiled into the firmware, so it works even without the web UI partition.
   */
  esp_err_t rest_server_start(const rest_server_config_t *cfg);

#ifdef __cplusplus
}
#endif

#endif  // REST_SERVER_H
