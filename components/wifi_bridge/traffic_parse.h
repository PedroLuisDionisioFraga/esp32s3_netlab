#ifndef TRAFFIC_PARSE_H
#define TRAFFIC_PARSE_H

/* Packet parsing for traffic_observer.c. No lwIP here, so host_test/ runs it on a PC. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TRAFFIC_PROTO_ICMP 1
#define TRAFFIC_PROTO_TCP  6
#define TRAFFIC_PROTO_UDP  17

typedef struct
{
  uint32_t src; /* network byte order, as lwIP keeps addresses */
  uint32_t dst;
  uint16_t dst_port; /* 0 without ports (ICMP) or in a fragment that is not the first */
  uint8_t proto;
  uint16_t length; /* IP total length */
  const uint8_t *udp_payload; /* inside the buffer given to traffic_parse_ipv4(), or NULL */
  size_t udp_payload_len;     /* bytes of it inside that buffer */
} traffic_packet_t;

/**
 * Reads the IPv4 header at the start of @p buf, plus the destination port of TCP and UDP.
 * @p len is how many bytes of the packet are in @p buf (it may be cut short).
 * @return false when it is not a well-formed IPv4 header.
 */
bool traffic_parse_ipv4(const uint8_t *buf, size_t len, traffic_packet_t *out);

/**
 * Copies the name of the first question of a DNS query ("www.example.com") into @p name, as much
 * as fits in @p name_size and in the @p len bytes given. Unprintable characters become '?'.
 * @return false when @p dns is not a query or holds no name.
 */
bool traffic_parse_dns_query(const uint8_t *dns, size_t len, char *name, size_t name_size);

#endif  // TRAFFIC_PARSE_H
