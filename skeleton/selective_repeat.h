#ifndef SELECTIVE_REPEAT_H
#define SELECTIVE_REPEAT_H

#include "protocol.h"
#include "common.h"
#include "lossy_udp.h"
#include <sys/socket.h>

/*
 * Selective Repeat ARQ Implementation
 *
 * This module implements the selective repeat protocol for reliable
 * data transfer over UDP.
 */

/* Packet state in send window */
typedef enum {
    PKT_EMPTY = 0,      // Slot is empty
    PKT_PENDING,        // Packet sent, waiting for ACK
    PKT_ACKED           // Packet acknowledged
} packet_state_t;

/* Send window entry */
typedef struct {
    packet_state_t state;
    uint32_t seq_num;
    uint8_t data[CHUNK_SIZE];
    uint16_t data_len;
    uint64_t send_time_ms;
    int retries;
} send_window_entry_t;

/* Receive window entry */
typedef struct {
    int received;       // 1 if packet received
    uint32_t seq_num;
    uint8_t data[CHUNK_SIZE];
    uint16_t data_len;
} recv_window_entry_t;

/* Selective Repeat sender state */
typedef struct {
    int udp_fd;
    struct sockaddr_in peer_addr;

    uint32_t base;              // Base sequence number
    uint32_t next_seq;          // Next sequence number to send
    send_window_entry_t window[WINDOW_SIZE];

    uint8_t *file_data;         // File data buffer
    uint32_t file_size;
    uint32_t num_chunks;
    uint32_t chunks_acked;

    lossy_link_t *lossy_link;   // Lossy UDP link (NULL for no loss)
} sr_sender_t;

/* Selective Repeat receiver state */
typedef struct {
    int udp_fd;
    struct sockaddr_in peer_addr;

    uint32_t base;              // Base sequence number (next expected)
    recv_window_entry_t window[WINDOW_SIZE];

    uint8_t *file_buffer;       // Assembled file buffer
    uint32_t file_size;
    uint32_t num_chunks;
    uint32_t chunks_received;

    lossy_link_t *lossy_link;   // Lossy UDP link (NULL for no loss)
} sr_receiver_t;

/*
 * Initialize sender
 *
 * Parameters:
 *   sender: Sender state structure
 *   udp_fd: UDP socket file descriptor
 *   peer_addr: Peer's address
 *   file_data: Pointer to file data to send
 *   file_size: Size of file in bytes
 *   lossy_link: Lossy UDP link (NULL for no packet loss)
 */
int sr_sender_init(sr_sender_t *sender, int udp_fd,
                   const struct sockaddr_in *peer_addr,
                   const uint8_t *file_data, uint32_t file_size,
                   lossy_link_t *lossy_link);

/*
 * Initialize receiver
 *
 * Parameters:
 *   receiver: Receiver state structure
 *   udp_fd: UDP socket file descriptor
 *   peer_addr: Peer's address
 *   file_size: Expected file size in bytes
 *   num_chunks: Expected number of chunks
 *   lossy_link: Lossy UDP link (NULL for no packet loss)
 */
int sr_receiver_init(sr_receiver_t *receiver, int udp_fd,
                     const struct sockaddr_in *peer_addr,
                     uint32_t file_size, uint32_t num_chunks,
                     lossy_link_t *lossy_link);

/*
 * Sender: Send next packets in window
 * Returns number of packets sent, or -1 on error
 */
int sr_sender_send_window(sr_sender_t *sender);

/*
 * Sender: Handle ACK packet
 * Returns 0 on success, -1 on error
 */
int sr_sender_handle_ack(sr_sender_t *sender, const udp_header_t *hdr);

/*
 * Sender: Check for timeouts and retransmit
 * Returns number of packets retransmitted, or -1 on error
 */
int sr_sender_check_timeouts(sr_sender_t *sender, uint64_t now_ms);

/*
 * Sender: Check if transfer complete
 * Returns 1 if complete, 0 otherwise
 */
int sr_sender_is_complete(const sr_sender_t *sender);

/*
 * Receiver: Handle data packet
 * Returns 0 on success, -1 on error
 */
int sr_receiver_handle_data(sr_receiver_t *receiver, const uint8_t *packet, size_t len);

/*
 * Receiver: Send ACK for received packet
 * Returns 0 on success, -1 on error
 */
int sr_receiver_send_ack(sr_receiver_t *receiver, uint32_t seq_num);

/*
 * Receiver: Check if transfer complete
 * Returns 1 if complete, 0 otherwise
 */
int sr_receiver_is_complete(const sr_receiver_t *receiver);

/*
 * Receiver: Get assembled file buffer
 * Returns pointer to file data, sets size if non-NULL
 */
const uint8_t* sr_receiver_get_file(const sr_receiver_t *receiver, uint32_t *size);

/* Cleanup */
void sr_sender_cleanup(sr_sender_t *sender);
void sr_receiver_cleanup(sr_receiver_t *receiver);

#endif // SELECTIVE_REPEAT_H
