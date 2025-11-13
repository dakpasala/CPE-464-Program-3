/*
 * CPE 464: P2P File Sharing - Client
 *
 * You need to implement the complete client from scratch.
 * This stub file is provided so the Makefile works, but you should
 * replace this entire file with your implementation.
 *
 * The client needs to:
 * - Connect to directory server (TCP)
 * - Implement user commands (share, list, get, quit)
 * - Send/receive files using selective repeat over UDP
 * - Use poll() for event-driven I/O (stdin, UDP socket)
 */

#include "protocol.h"
#include "common.h"
#include "selective_repeat.h"
#include "lossy_udp.h"
#include <poll.h>
#include <signal.h>
#include <time.h>

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <server_ip> <server_port> [udp_port] [loss_rate]\n", argv[0]);
        fprintf(stderr, "  udp_port:   UDP port to bind (default: random)\n");
        fprintf(stderr, "  loss_rate:  Packet loss rate 0.0-1.0 (default: 0.0)\n");
        return 1;
    }

    // TODO: Implement client
    // You need to design and implement the entire client from scratch.
    // Refer to the assignment document and protocol.h for requirements.

    const char *server_ip = argv[1];
    uint16_t server_port = (uint16_t)atoi(argv[2]);

    int tcp_fd = create_tcp_socket();
    if (tcp_fd < 0) {
        fprintf(stderr, "TCP socket failed lol\n");
        return 1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);

    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) != 1) {
        fprintf(stderr, "ip address isn't valid: %s\n", server_ip);
        return 1;
    }

    if (connect(tcp_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        fprintf(stderr, "couldn't connect to dir server: %s\n", strerror(errno));
        close(tcp_fd);
        return 1;
    }

    printf("conntect do dir server at %s:%u\n", server_ip, server_port);

    return 0;
}
