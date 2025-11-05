#define _POSIX_C_SOURCE 200809L
#include "lossy_udp.h"
#include <stdio.h>
#include <unistd.h>

static uint64_t xs64(uint64_t *s){
    uint64_t x=*s; x^=x>>12; x^=x<<25; x^=x>>27; *s=x; return x*2685821657736338717ULL;
}

void lossy_init(lossy_link_t *l, int fd, double loss, uint64_t seed){
    l->fd = fd; l->loss = loss; l->rng = seed ? seed : 88172645463393265ULL;
    if (l->loss < 0.0) l->loss = 0.0; if (l->loss > 1.0) l->loss = 1.0;
}

ssize_t lossy_send(lossy_link_t *l,
                   const void *buf, size_t len, int flags,
                   const struct sockaddr *dest_addr, socklen_t addrlen){
    uint64_t r = xs64(&l->rng);
    double u = (double)(r >> 11) / (double)(1ULL<<53);
    if (u < l->loss) {
        fprintf(stderr, "[LOSSY] DROPPED packet (u=%.4f < loss=%.4f)\n", u, l->loss);
        return (ssize_t)len; // simulate silent drop
    }
    return sendto(l->fd, buf, len, flags, dest_addr, addrlen);
}

ssize_t lossy_recv(lossy_link_t *l,
                   void *buf, size_t len, int flags,
                   struct sockaddr *src_addr, socklen_t *addrlen){
    (void)l;
    return recvfrom(l->fd, buf, len, flags, src_addr, addrlen);
}
