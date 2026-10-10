#include "auth.h"

#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#if CONFIG_NETLAB_AUTH_ENABLE

#define SESSIONS_MAX     4
#define LOCKOUT_FAILURES 5
#define LOCKOUT_US       (30LL * 1000 * 1000)
#define TTL_S            ((uint32_t)CONFIG_NETLAB_SESSION_TTL_MIN * 60u)

typedef struct
{
  char token[REST_AUTH_TOKEN_LEN + 1]; /* "" = free slot */
  int64_t expires_us;
} session_t;

/* Only the HTTP server task calls in here (esp_http_server runs every handler on one task), so there is no lock. */
typedef struct
{
  session_t sessions[SESSIONS_MAX];
  int failures;            /* wrong logins in a row */
  int64_t locked_until_us; /* login refused until then */
} auth_priv_t;

static auth_priv_t s_priv;

#else
#define TTL_S 0u /* no sessions: the option that sets it does not exist */
#endif

bool rest_auth_enabled(void)
{
#if CONFIG_NETLAB_AUTH_ENABLE
  return true;
#else
  return false;
#endif
}

uint32_t rest_auth_ttl_s(void)
{
  return TTL_S;
}

#if CONFIG_NETLAB_AUTH_ENABLE

/* Compares the whole of the shorter string whatever the first difference, so the time taken does not say how many
 * leading characters were right. The length still shows; the strings are short and not secret in length. */
static bool ct_equal(const char *a, const char *b)
{
  const size_t la = strlen(a);
  const size_t lb = strlen(b);
  const size_t n = la < lb ? la : lb;
  unsigned diff = (unsigned)(la ^ lb);
  for (size_t i = 0; i < n; i++)
    diff |= (unsigned)((unsigned char)a[i] ^ (unsigned char)b[i]);
  return diff == 0;
}

/* A free or expired slot, else the session that would expire first. */
static session_t *pick_slot(int64_t now)
{
  session_t *oldest = &s_priv.sessions[0];
  for (int i = 0; i < SESSIONS_MAX; i++)
  {
    session_t *s = &s_priv.sessions[i];
    if (s->token[0] == '\0' || s->expires_us <= now)
      return s;
    if (s->expires_us < oldest->expires_us)
      oldest = s;
  }
  return oldest;
}

rest_auth_result_t rest_auth_login(const char *user, const char *password, char token[REST_AUTH_TOKEN_LEN + 1],
                                   uint32_t *retry_after_s)
{
  const int64_t now = esp_timer_get_time();
  if (s_priv.locked_until_us > now)
  {
    if (retry_after_s)
      *retry_after_s = (uint32_t)((s_priv.locked_until_us - now + 999999) / 1000000);
    return REST_AUTH_LOCKED;
  }

  /* Both are always compared, so the time does not tell which of the two was wrong. */
  const bool user_ok = ct_equal(user, CONFIG_NETLAB_ADMIN_USER);
  const bool password_ok = ct_equal(password, CONFIG_NETLAB_ADMIN_PASSWORD);
  if (!user_ok || !password_ok)
  {
    if (++s_priv.failures >= LOCKOUT_FAILURES)
    {
      s_priv.failures = 0;
      s_priv.locked_until_us = now + LOCKOUT_US;
      ESP_LOGW("rest_auth", "Too many wrong logins: locked for %d s", (int)(LOCKOUT_US / 1000000));
    }
    return REST_AUTH_BAD_CREDENTIALS;
  }
  s_priv.failures = 0;

  static const char hex[] = "0123456789abcdef";
  uint8_t random[REST_AUTH_TOKEN_LEN / 2];
  esp_fill_random(random, sizeof(random));

  session_t *slot = pick_slot(now);
  for (size_t i = 0; i < sizeof(random); i++)
  {
    slot->token[2 * i] = hex[random[i] >> 4];
    slot->token[2 * i + 1] = hex[random[i] & 0x0f];
  }
  slot->token[REST_AUTH_TOKEN_LEN] = '\0';
  slot->expires_us = now + (int64_t)TTL_S * 1000000;
  memcpy(token, slot->token, REST_AUTH_TOKEN_LEN + 1);
  return REST_AUTH_OK;
}

bool rest_auth_check(const char *token)
{
  const int64_t now = esp_timer_get_time();
  session_t *match = NULL;
  /* Every slot is compared: no early exit on a hit. */
  for (int i = 0; i < SESSIONS_MAX; i++)
  {
    session_t *s = &s_priv.sessions[i];
    if (s->token[0] != '\0' && s->expires_us > now && ct_equal(s->token, token))
      match = s;
  }
  if (!match)
    return false;
  match->expires_us = now + (int64_t)TTL_S * 1000000; /* sliding expiry */
  return true;
}

void rest_auth_logout(const char *token)
{
  for (int i = 0; i < SESSIONS_MAX; i++)
  {
    session_t *s = &s_priv.sessions[i];
    if (s->token[0] != '\0' && ct_equal(s->token, token))
      memset(s, 0, sizeof(*s));
  }
}

#else /* !CONFIG_NETLAB_AUTH_ENABLE */

rest_auth_result_t rest_auth_login(const char *user, const char *password, char token[REST_AUTH_TOKEN_LEN + 1],
                                   uint32_t *retry_after_s)
{
  (void)user;
  (void)password;
  (void)retry_after_s;
  token[0] = '\0';
  return REST_AUTH_OK;
}

bool rest_auth_check(const char *token)
{
  (void)token;
  return true;
}

void rest_auth_logout(const char *token)
{
  (void)token;
}

#endif
