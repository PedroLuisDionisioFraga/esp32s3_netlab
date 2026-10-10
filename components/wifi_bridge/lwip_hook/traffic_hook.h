#ifndef TRAFFIC_HOOK_H
#define TRAFFIC_HOOK_H

/* Added to lwIP's build by the project CMakeLists.txt (ESP_IDF_LWIP_HOOK_FILENAME) in router lab
 * mode, so lwIP shows every received IPv4 packet to traffic_observer.c. */

struct pbuf;
struct netif;

/** Looks at the packet and leaves it to lwIP: always returns 0 (not consumed). */
int traffic_observer_ip4_input(struct pbuf *p, struct netif *inp);

#define LWIP_HOOK_IP4_INPUT(p, inp) traffic_observer_ip4_input((p), (inp))

#endif  // TRAFFIC_HOOK_H
