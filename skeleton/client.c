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

    printf("Client stub - not yet implemented\n");
    printf("TODO: Implement the client functionality\n");

    return 0;
}
