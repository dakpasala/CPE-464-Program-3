#ifndef LOSSY_UDP_H
#define LOSSY_UDP_H

#include <stdint.h>
#include <sys/socket.h>

typedef struct {
    int fd;
    double loss;   // 0.0..1.0 probability of dropping an outbound packet
    uint64_t rng;  // xorshift64* state
} lossy_link_t;

void lossy_init(lossy_link_t *l, int fd, double loss, uint64_t seed);

ssize_t lossy_send(lossy_link_t *l,
                   const void *buf, size_t len, int flags,
                   const struct sockaddr *dest_addr, socklen_t addrlen);

ssize_t lossy_recv(lossy_link_t *l,
                   void *buf, size_t len, int flags,
                   struct sockaddr *src_addr, socklen_t *addrlen);

#endif /* LOSSY_UDP_H */
