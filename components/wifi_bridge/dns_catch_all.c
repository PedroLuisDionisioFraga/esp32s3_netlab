#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "wifi_bridge_priv.h"

/*
 * Minimal DNS responder for the setup network: every IPv4 lookup resolves to the device
 * itself, so a phone's "is there internet?" probe lands on the setup page and the phone
 * offers to open it. Other record types get an empty answer so clients do not wait on them.
 *
 * Unlike a task that is simply deleted, stop() lets the task close its socket, so the
 * server can be started again later (the setup network opens and closes repeatedly).
 */

#define DNS_PORT        53
#define DNS_MAX_PACKET  512
#define DNS_HEADER_LEN  12
#define DNS_ANSWER_LEN  16
#define DNS_TTL_S       30
#define DNS_TYPE_A      1
#define DNS_CLASS_IN    1
#define RECV_TIMEOUT_MS 250
#define STOP_WAIT_MS    2000

static const char *TAG = "dns_catch_all";

static TaskHandle_t s_task;
static SemaphoreHandle_t s_done;
static volatile bool s_run;
static uint32_t s_ip;

/* Bytes taken by the question name starting at `offset`, terminating zero included; 0 if malformed. */
static size_t question_name_len(const uint8_t *pkt, size_t len, size_t offset)
{
  size_t pos = offset;
  while (pos < len)
  {
    uint8_t label = pkt[pos];
    if (label == 0)
      return pos - offset + 1;

    if (label & 0xC0)
      return 0; /* compression pointers do not appear in a question */

    pos += (size_t)label + 1;
  }
  return 0;
}

/* Builds the reply for a single-question query. Returns the reply length, or 0 to stay silent. */
static size_t build_reply(const uint8_t *req, size_t req_len, uint8_t *reply, size_t reply_max, uint32_t ip)
{
  if (req_len < DNS_HEADER_LEN + 5)
    return 0;

  if ((req[2] & 0x80) != 0 || (req[2] & 0x78) != 0)
    return 0; /* a response, or an opcode other than QUERY */

  if (req[4] != 0 || req[5] != 1)
    return 0; /* exactly one question */

  size_t name_len = question_name_len(req, req_len, DNS_HEADER_LEN);
  size_t question_end = DNS_HEADER_LEN + name_len + 4;
  if (name_len == 0 || question_end > req_len)
    return 0;

  uint16_t qtype = (uint16_t)((req[question_end - 4] << 8) | req[question_end - 3]);
  uint16_t qclass = (uint16_t)((req[question_end - 2] << 8) | req[question_end - 1]);
  bool answer = (qtype == DNS_TYPE_A && qclass == DNS_CLASS_IN);

  size_t reply_len = question_end + (answer ? DNS_ANSWER_LEN : 0);
  if (reply_len > reply_max)
    return 0;

  memset(reply, 0, DNS_HEADER_LEN);
  reply[0] = req[0]; /* transaction id */
  reply[1] = req[1];
  reply[2] = 0x80 | (req[2] & 0x01); /* response, keep the client's recursion-desired bit */
  reply[3] = 0x80;                   /* recursion available, no error */
  reply[5] = 1;                      /* one question */
  reply[7] = answer ? 1 : 0;         /* and one answer for A queries */
  memcpy(reply + DNS_HEADER_LEN, req + DNS_HEADER_LEN, name_len + 4);

  if (answer)
  {
    uint8_t *rr = reply + question_end;
    rr[0] = 0xC0; /* name: pointer back to the question */
    rr[1] = DNS_HEADER_LEN;
    rr[2] = 0;
    rr[3] = DNS_TYPE_A;
    rr[4] = 0;
    rr[5] = DNS_CLASS_IN;
    rr[6] = 0; /* TTL */
    rr[7] = 0;
    rr[8] = 0;
    rr[9] = DNS_TTL_S;
    rr[10] = 0; /* data length */
    rr[11] = 4;
    memcpy(rr + 12, &ip, 4); /* already in network byte order */
  }
  return reply_len;
}

/* UDP socket bound to the setup network's DNS port. The receive timeout lets the serve loop notice
 * that s_run went false. Returns -1 after logging if it cannot be opened. */
static int dns_socket_open(void)
{
  int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
  if (sock < 0)
  {
    ESP_LOGE(TAG, "socket() failed: errno %d", errno);
    return -1;
  }

  struct sockaddr_in addr = {
    .sin_family = AF_INET,
    .sin_port = htons(DNS_PORT),
    .sin_addr.s_addr = s_ip, /* only the setup network, never the router side */
  };
  struct timeval timeout = {.tv_sec = 0, .tv_usec = RECV_TIMEOUT_MS * 1000};
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

  if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0)
  {
    ESP_LOGE(TAG, "bind() to port %d failed: errno %d", DNS_PORT, errno);
    close(sock);
    return -1;
  }
  return sock;
}

/* Answers queries until s_run is cleared. */
static void dns_serve(int sock)
{
  uint8_t req[DNS_MAX_PACKET];
  uint8_t reply[DNS_MAX_PACKET];

  while (s_run)
  {
    struct sockaddr_in source;
    socklen_t source_len = sizeof(source);
    int n = recvfrom(sock, req, sizeof(req), 0, (struct sockaddr *)&source, &source_len);
    if (n < 0)
    {
      if (errno != EAGAIN && errno != EWOULDBLOCK)
        vTaskDelay(pdMS_TO_TICKS(50)); /* do not spin on a persistent error */
      continue;                        /* timeout: look at s_run again */
    }

    size_t reply_len = build_reply(req, (size_t)n, reply, sizeof(reply), s_ip);
    if (reply_len > 0)
      sendto(sock, reply, reply_len, 0, (struct sockaddr *)&source, source_len);
  }
}

static void dns_task(void *arg)
{
  (void)arg;

  int sock = dns_socket_open();
  if (sock >= 0)
  {
    dns_serve(sock);
    close(sock);
  }

  xSemaphoreGive(s_done); /* dns_catch_all_stop() waits for this: the socket is closed by now */
  vTaskDelete(NULL);
}

esp_err_t dns_catch_all_start(uint32_t ip_addr)
{
  if (s_task)
    return ESP_OK;

  if (!s_done)
  {
    s_done = xSemaphoreCreateBinary();
    if (!s_done)
      return ESP_ERR_NO_MEM;
  }

  s_ip = ip_addr;
  s_run = true;
  if (xTaskCreate(dns_task, "dns_catch_all", 4096, NULL, 5, &s_task) != pdPASS)
  {
    s_task = NULL;
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}

void dns_catch_all_stop(void)
{
  if (!s_task)
    return;

  s_run = false;
  xSemaphoreTake(s_done, pdMS_TO_TICKS(STOP_WAIT_MS));
  s_task = NULL;
}
