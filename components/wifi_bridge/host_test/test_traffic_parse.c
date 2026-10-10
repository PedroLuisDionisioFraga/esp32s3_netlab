/* Host check for traffic_parse.c. From the repo root:
 *   gcc -Wall -Wextra -I components/wifi_bridge components/wifi_bridge/host_test/test_traffic_parse.c \
 *       components/wifi_bridge/traffic_parse.c -o test_traffic_parse && ./test_traffic_parse
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "traffic_parse.h"

/* 192.168.4.2:5353 -> 8.8.8.8:53, UDP, DNS query for www.example.com */
static const uint8_t k_dns_packet[] = {
  0x45, 0x00, 0x00, 0x3d, 0x12, 0x34, 0x00, 0x00, 0x40, 0x11, 0x00, 0x00, /* IPv4, total 61 */
  192,  168,  4,    2,    8,    8,    8,    8,                            /* src, dst */
  0x14, 0xe9, 0x00, 0x35, 0x00, 0x29, 0x00, 0x00,                         /* UDP 5353 -> 53 */
  0xab, 0xcd, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* query, 1 question */
  3,    'w',  'w',  'w',  7,    'e',  'x',  'a',  'm',  'p',  'l',  'e',  3, 'c', 'o', 'm', 0,
  0x00, 0x01, 0x00, 0x01, /* type A, class IN */
};

/* 192.168.4.3 -> 93.184.216.34:443, TCP SYN with a 4-byte IP option (IHL 6) */
static const uint8_t k_tcp_packet[] = {
  0x46, 0x00, 0x00, 0x2c, 0x00, 0x01, 0x40, 0x00, 0x40, 0x06, 0x00, 0x00, 192, 168, 4, 3, 93, 184, 216, 34,
  0x01, 0x01, 0x01, 0x00, /* options: NOP NOP NOP EOL */
  0xc0, 0x00, 0x01, 0xbb, /* ports 49152 -> 443 */
  0,    0,    0,    0,    0, 0, 0, 0, 0x50, 0x02, 0xff, 0xff, 0, 0, 0, 0,
};

static void test_dns_query_packet(void)
{
  traffic_packet_t pkt;
  assert(traffic_parse_ipv4(k_dns_packet, sizeof(k_dns_packet), &pkt));
  const uint8_t src[4] = {192, 168, 4, 2};
  const uint8_t dst[4] = {8, 8, 8, 8};
  assert(memcmp(&pkt.src, src, 4) == 0);
  assert(memcmp(&pkt.dst, dst, 4) == 0);
  assert(pkt.proto == TRAFFIC_PROTO_UDP);
  assert(pkt.dst_port == 53);
  assert(pkt.length == sizeof(k_dns_packet));
  assert(pkt.udp_payload == k_dns_packet + 28);
  assert(pkt.udp_payload_len == sizeof(k_dns_packet) - 28);

  char name[64];
  assert(traffic_parse_dns_query(pkt.udp_payload, pkt.udp_payload_len, name, sizeof(name)));
  assert(strcmp(name, "www.example.com") == 0);
}

static void test_tcp_with_options(void)
{
  traffic_packet_t pkt;
  assert(traffic_parse_ipv4(k_tcp_packet, sizeof(k_tcp_packet), &pkt));
  assert(pkt.proto == TRAFFIC_PROTO_TCP);
  assert(pkt.dst_port == 443);
  assert(pkt.udp_payload == NULL);
}

static void test_ports_need_first_fragment_and_bytes(void)
{
  uint8_t frag[sizeof(k_dns_packet)];
  memcpy(frag, k_dns_packet, sizeof(frag));
  frag[7] = 0x10; /* fragment offset 16 * 8 bytes: the UDP header is in another fragment */
  traffic_packet_t pkt;
  assert(traffic_parse_ipv4(frag, sizeof(frag), &pkt));
  assert(pkt.dst_port == 0 && pkt.udp_payload == NULL);

  /* Only the IP header was copied: still a valid packet, just no port. */
  assert(traffic_parse_ipv4(k_dns_packet, 20, &pkt));
  assert(pkt.dst_port == 0 && pkt.length == sizeof(k_dns_packet));

  /* Payload stops at the IP total length even when the buffer is longer (link-layer padding). */
  uint8_t padded[sizeof(k_dns_packet) + 6] = {0};
  memcpy(padded, k_dns_packet, sizeof(k_dns_packet));
  assert(traffic_parse_ipv4(padded, sizeof(padded), &pkt));
  assert(pkt.udp_payload_len == sizeof(k_dns_packet) - 28);
}

static void test_rejects_bad_headers(void)
{
  traffic_packet_t pkt;
  uint8_t bad[sizeof(k_dns_packet)];

  assert(!traffic_parse_ipv4(k_dns_packet, 19, &pkt)); /* shorter than a header */

  memcpy(bad, k_dns_packet, sizeof(bad));
  bad[0] = 0x65; /* IPv6 */
  assert(!traffic_parse_ipv4(bad, sizeof(bad), &pkt));

  bad[0] = 0x44; /* IHL 4: header shorter than 20 bytes */
  assert(!traffic_parse_ipv4(bad, sizeof(bad), &pkt));

  bad[0] = 0x45;
  bad[2] = 0;
  bad[3] = 10; /* total length smaller than the header */
  assert(!traffic_parse_ipv4(bad, sizeof(bad), &pkt));
}

static void test_dns_edge_cases(void)
{
  const uint8_t *dns = k_dns_packet + 28;
  const size_t dns_len = sizeof(k_dns_packet) - 28;
  char name[64];

  /* A response is not a query. */
  uint8_t response[sizeof(k_dns_packet) - 28];
  memcpy(response, dns, sizeof(response));
  response[2] |= 0x80;
  assert(!traffic_parse_dns_query(response, sizeof(response), name, sizeof(name)));

  /* Name longer than the buffer: cut, still terminated. */
  char small[8];
  assert(traffic_parse_dns_query(dns, dns_len, small, sizeof(small)));
  assert(strcmp(small, "www.exa") == 0);

  /* Bytes end in the middle of a label: what arrived is kept. */
  assert(traffic_parse_dns_query(dns, 12 + 1 + 3 + 1 + 2, name, sizeof(name)));
  assert(strcmp(name, "www.ex") == 0);

  /* No name at all. */
  assert(!traffic_parse_dns_query(dns, 12, name, sizeof(name)));

  /* Unprintable bytes are not copied as they are. */
  uint8_t odd[sizeof(response)];
  memcpy(odd, dns, sizeof(odd));
  odd[13] = '\n';
  assert(traffic_parse_dns_query(odd, sizeof(odd), name, sizeof(name)));
  assert(strcmp(name, "?ww.example.com") == 0);
}

int main(void)
{
  test_dns_query_packet();
  test_tcp_with_options();
  test_ports_need_first_fragment_and_bytes();
  test_rejects_bad_headers();
  test_dns_edge_cases();
  puts("traffic_parse: all checks passed");
  return 0;
}
