#ifndef REST_AUTH_H
#define REST_AUTH_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/** Characters of a session token (128 random bits, hex). */
#define REST_AUTH_TOKEN_LEN 32

typedef enum
{
  REST_AUTH_OK = 0,
  REST_AUTH_BAD_CREDENTIALS,
  REST_AUTH_LOCKED, /**< too many wrong attempts in a row: wait */
} rest_auth_result_t;

/** False when CONFIG_NETLAB_AUTH_ENABLE is off: every check passes and there is no login. */
bool rest_auth_enabled(void);

/**
 * @brief Check a user name and password and open a session.
 *
 * Five wrong attempts in a row lock the login for 30 seconds. Up to four sessions live at once (a phone and a
 * PC); a fifth login replaces the one that expires first.
 *
 * @param token Receives REST_AUTH_TOKEN_LEN characters and a NUL on success.
 * @param retry_after_s Receives the seconds left of the lock when the result is REST_AUTH_LOCKED (may be NULL).
 */
rest_auth_result_t rest_auth_login(const char *user, const char *password, char token[REST_AUTH_TOKEN_LEN + 1],
                                   uint32_t *retry_after_s);

/** True when @p token belongs to a live session. A hit extends the session (the expiry slides with use). */
bool rest_auth_check(const char *token);

/** End the session of @p token; an unknown token is ignored. */
void rest_auth_logout(const char *token);

/** How long a session lasts without use, in seconds. */
uint32_t rest_auth_ttl_s(void);

#ifdef __cplusplus
}
#endif

#endif  // REST_AUTH_H
