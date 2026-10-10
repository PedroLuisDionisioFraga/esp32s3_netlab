/* Router lab mode: counts what lab clients send through the NAT. Runs inside lwIP (the IPv4 input
 * hook, on the lwIP task) for every packet, so it only copies headers and never blocks. Flows and
 * DNS names are recorded only while capture is on; payloads never are. */

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip_hook/traffic_hook.h"
#include "sdkconfig.h"
#include "traffic_parse.h"
#include "wifi_bridge_priv.h"

/* IP header, UDP header and a DNS name of about 80 characters; longer names are cut anyway. */
#define PEEK_BYTES 128
#define DNS_PORT   53

#ifdef CONFIG_WIFI_BRIDGE_ROUTER_CAPTURE_DEFAULT
#define CAPTURE_AT_BOOT true
#else
#define CAPTURE_AT_BOOT false
#endif

#define FLOWS_MAX CONFIG_WIFI_BRIDGE_ROUTER_FLOWS_MAX
#define DNS_MAX   CONFIG_WIFI_BRIDGE_ROUTER_DNS_MAX

typedef struct
{
  bool used;
  uint8_t proto;
  uint16_t port;
  uint32_t client; /* lwIP (network) byte order */
  uint32_t dst;
  uint32_t packets;
  uint64_t bytes;
  uint32_t first_s;
  uint32_t last_s;
} flow_t;

typedef struct
{
  uint32_t client;
  uint32_t at_s;
  char name[sizeof(((wifi_bridge_router_dns_t *)0)->name)];
} dns_entry_t;

static const char *TAG = "traffic";

/* The hook writes on the lwIP task while HTTP handlers read: every access to the state below
 * holds this spinlock, for a few microseconds at most. */
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static struct netif *volatile s_lab_netif; /* NULL while the lab network is closed */
static volatile bool s_capture = CAPTURE_AT_BOOT;

static uint32_t s_packets;
static uint64_t s_bytes;
static uint32_t s_flows_dropped;
static flow_t s_flows[FLOWS_MAX];
static dns_entry_t s_dns[DNS_MAX];
static size_t s_dns_next; /* the ring's next slot to write */

static uint32_t uptime_s(void)
{
  return (uint32_t)(esp_timer_get_time() / 1000000);
}

/* ponytail: linear scan per packet, fine for a few hundred flows; hash the key if FLOWS_MAX grows. */
static void count_flow_locked(const traffic_packet_t *pkt, uint32_t now)
{
  flow_t *victim = &s_flows[0]; /* a free slot, else the flow seen least recently */
  for (size_t i = 0; i < FLOWS_MAX; i++)
  {
    flow_t *f = &s_flows[i];
    if (!f->used)
    {
      if (victim->used)
        victim = f;
      continue;
    }
    if (f->client == pkt->src && f->dst == pkt->dst && f->port == pkt->dst_port && f->proto == pkt->proto)
    {
      f->packets++;
      f->bytes += pkt->length;
      f->last_s = now;
      return;
    }
    if (victim->used && f->last_s < victim->last_s)
      victim = f;
  }

  if (victim->used)
    s_flows_dropped++;
  *victim = (flow_t){
    .used = true,
    .proto = pkt->proto,
    .port = pkt->dst_port,
    .client = pkt->src,
    .dst = pkt->dst,
    .packets = 1,
    .bytes = pkt->length,
    .first_s = now,
    .last_s = now,
  };
}

int traffic_observer_ip4_input(struct pbuf *p, struct netif *inp)
{
  if (inp == NULL || inp != s_lab_netif)
    return 0; /* uplink and loopback traffic: not a lab client */

  uint8_t head[PEEK_BYTES];
  traffic_packet_t pkt;
  if (!traffic_parse_ipv4(head, pbuf_copy_partial(p, head, sizeof(head), 0), &pkt))
    return 0; /* lwIP drops it next */

  /* Only what leaves through the NAT: not the device itself (dashboard, DHCP), other lab clients,
   * broadcast or multicast. */
  const ip4_addr_t dst = {.addr = pkt.dst};
  if (ip4_addr_net_eq(&dst, netif_ip4_addr(inp), netif_ip4_netmask(inp)) || ip4_addr_isbroadcast(&dst, inp) ||
      ip4_addr_ismulticast(&dst))
    return 0;

  const bool capture = s_capture;
  char name[sizeof(((dns_entry_t *)0)->name)];
  const bool dns = capture && pkt.proto == TRAFFIC_PROTO_UDP && pkt.dst_port == DNS_PORT &&
                   traffic_parse_dns_query(pkt.udp_payload, pkt.udp_payload_len, name, sizeof(name));
  const uint32_t now = uptime_s();

  portENTER_CRITICAL(&s_mux);
  s_packets++;
  s_bytes += pkt.length;
  if (capture)
    count_flow_locked(&pkt, now);
  if (dns)
  {
    dns_entry_t *entry = &s_dns[s_dns_next];
    s_dns_next = (s_dns_next + 1) % DNS_MAX;
    entry->client = pkt.src;
    entry->at_s = now;
    memcpy(entry->name, name, sizeof(entry->name));
  }
  portEXIT_CRITICAL(&s_mux);
  return 0;
}

static void format_ip(uint32_t addr, char *out, size_t size)
{
  const ip4_addr_t ip = {.addr = addr};
  ip4addr_ntoa_r(&ip, out, (int)size);
}

void traffic_observer_attach(void *lab_netif)
{
  s_lab_netif = lab_netif;
}

void traffic_observer_get_totals(wifi_bridge_router_status_t *out)
{
  out->capture = s_capture;
  out->flows_max = FLOWS_MAX;
  out->dns_max = DNS_MAX;
  portENTER_CRITICAL(&s_mux);
  out->packets = s_packets;
  out->bytes = s_bytes;
  out->flows_dropped = s_flows_dropped;
  portEXIT_CRITICAL(&s_mux);
}

/* One entry per call, so the hook never waits on a reader for long. */
bool wifi_bridge_router_get_flow(size_t slot, wifi_bridge_router_flow_t *out)
{
  if (slot >= FLOWS_MAX || !out)
    return false;

  portENTER_CRITICAL(&s_mux);
  const flow_t f = s_flows[slot];
  portEXIT_CRITICAL(&s_mux);
  if (!f.used)
    return false;

  format_ip(f.client, out->client, sizeof(out->client));
  format_ip(f.dst, out->dst, sizeof(out->dst));
  out->port = f.port;
  out->proto = f.proto;
  out->packets = f.packets;
  out->bytes = f.bytes;
  out->first_s = f.first_s;
  out->last_s = f.last_s;
  return true;
}

bool wifi_bridge_router_get_dns(size_t age, wifi_bridge_router_dns_t *out)
{
  if (age >= DNS_MAX || !out)
    return false;

  portENTER_CRITICAL(&s_mux);
  const dns_entry_t d = s_dns[(s_dns_next + DNS_MAX - 1 - age) % DNS_MAX];
  portEXIT_CRITICAL(&s_mux);
  if (d.name[0] == '\0')
    return false; /* not written yet */

  format_ip(d.client, out->client, sizeof(out->client));
  memcpy(out->name, d.name, sizeof(out->name));
  out->at_s = d.at_s;
  return true;
}

void wifi_bridge_router_set_capture(bool on)
{
  s_capture = on;
  ESP_LOGW(TAG, "Capture %s", on ? "on: recording flows and DNS names of lab clients" : "off");
}

void wifi_bridge_router_clear(void)
{
  portENTER_CRITICAL(&s_mux);
  memset(s_flows, 0, sizeof(s_flows));
  memset(s_dns, 0, sizeof(s_dns));
  s_dns_next = 0;
  s_packets = 0;
  s_bytes = 0;
  s_flows_dropped = 0;
  portEXIT_CRITICAL(&s_mux);
  ESP_LOGI(TAG, "Flows, DNS names and counters cleared");
}
