#ifndef RESET_BUTTON_H
#define RESET_BUTTON_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Watch the reset button. Holding it for CONFIG_NETLAB_RESET_HOLD_MS erases the saved
 *        Wi-Fi network and restarts the device into setup mode.
 */
esp_err_t reset_button_start(void);

#ifdef __cplusplus
}
#endif

#endif  // RESET_BUTTON_H
