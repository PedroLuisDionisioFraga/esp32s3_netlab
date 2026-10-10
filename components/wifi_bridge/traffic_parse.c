#include "traffic_parse.h"

#include <string.h>

#define IPV4_HEADER_MIN 20
#define UDP_HEADER_LEN  8
#define DNS_HEADER_LEN  12
#define DNS_LABEL_MAX   63

static uint16_t read_u16(const uint8_t *p)
{
  return (uint16_t)(p[0] << 8 | p[1]);
}

bool traffic_parse_ipv4(const uint8_t *buf, size_t len, traffic_packet_t *out)
{
  if (len < IPV4_HEADER_MIN || (buf[0] >> 4) != 4)
    return false;

  const size_t header_len = (size_t)(buf[0] & 0x0f) * 4;
  const uint16_t total = read_u16(buf + 2);
  if (header_len < IPV4_HEADER_MIN || total < header_len)
    return false;

  memset(out, 0, sizeof(*out));
  out->proto = buf[9];
  out->length = total;
  memcpy(&out->src, buf + 12, sizeof(out->src));
  memcpy(&out->dst, buf + 16, sizeof(out->dst));

  /* Ports are only in the first fragment, after the options. Link-layer padding past the IP total
   * length is not payload. */
  const bool first_fragment = ((buf[6] & 0x1f) | buf[7]) == 0;
  const size_t end = len < total ? len : total;
  if (!first_fragment || (out->proto != TRAFFIC_PROTO_TCP && out->proto != TRAFFIC_PROTO_UDP) ||
      end < header_len + 4)
    return true;

  out->dst_port = read_u16(buf + header_len + 2);
  if (out->proto == TRAFFIC_PROTO_UDP && end >= header_len + UDP_HEADER_LEN)
  {
    out->udp_payload = buf + header_len + UDP_HEADER_LEN;
    out->udp_payload_len = end - header_len - UDP_HEADER_LEN;
  }
  return true;
}

bool traffic_parse_dns_query(const uint8_t *dns, size_t len, char *name, size_t name_size)
{
  /* QR bit clear and opcode 0 (a standard query), at least one question. */
  if (name_size == 0 || len < DNS_HEADER_LEN || (dns[2] & 0xf8) != 0 || read_u16(dns + 4) == 0)
    return false;

  size_t out = 0;
  size_t pos = DNS_HEADER_LEN;
  /* A zero length ends the name; anything above 63 would be a compression pointer, which the first
   * question of a query never has. */
  while (pos < len && dns[pos] != 0 && dns[pos] <= DNS_LABEL_MAX)
  {
    const size_t label_end = pos + 1 + dns[pos];
    if (out > 0 && out + 1 < name_size)
      name[out++] = '.';

    for (pos++; pos < label_end && pos < len; pos++)
    {
      if (out + 1 < name_size)
        name[out++] = (dns[pos] > ' ' && dns[pos] < 0x7f) ? (char)dns[pos] : '?';
    }
  }
  name[out] = '\0';
  return out > 0;
}
