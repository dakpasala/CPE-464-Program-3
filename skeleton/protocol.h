#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

/*
 * P2P File Sharing Protocol Specification
 *
 * Two-layer architecture:
 * 1. TCP Control Channel: Client <-> Directory Server
 *    - Client registration/unregistration
 *    - File sharing announcements
 *    - File listing
 *    - Peer discovery for file requests
 *
 * 2. UDP Data Channel: Peer <-> Peer
 *    - Direct file transfer with selective repeat ARQ
 *    - Chunk-based transfer with sequence numbers
 *    - ACK/NAK support
 *    - Out-of-order packet handling
 */

/* ========== TCP Protocol (Client <-> Directory Server) ========== */

// TCP message types
#define TCP_REGISTER        1   // Client registers with server
#define TCP_REGISTER_ACK    2   // Server acknowledges registration
#define TCP_SHARE_FILE      3   // Client announces file to share
#define TCP_SHARE_ACK       4   // Server acknowledges file sharing
#define TCP_LIST_FILES      5   // Client requests file list
#define TCP_FILE_LIST       6   // Server sends file list
#define TCP_REQUEST_FILE    7   // Client requests file from peer
#define TCP_FILE_LOCATION   8   // Server responds with peer info
#define TCP_UNREGISTER      9   // Client unregisters
#define TCP_ERROR          10   // Error response

// Error codes
#define ERR_NONE            0
#define ERR_FILE_NOT_FOUND  1
#define ERR_NO_PEERS        2
#define ERR_INVALID_MSG     3
#define ERR_DUPLICATE_FILE  4

// TCP packet header (all TCP messages start with this)
typedef struct {
    uint8_t  msg_type;      // Message type (TCP_*)
    uint8_t  error_code;    // Error code (ERR_*)
    uint16_t data_len;      // Length of data following header
} __attribute__((packed)) tcp_header_t;

// TCP_REGISTER: Client -> Server
typedef struct {
    uint16_t udp_port;      // Client's UDP port for P2P transfers
} __attribute__((packed)) tcp_register_t;

// TCP_REGISTER_ACK: Server -> Client
typedef struct {
    uint32_t client_id;     // Assigned client ID
} __attribute__((packed)) tcp_register_ack_t;

// TCP_SHARE_FILE: Client -> Server
typedef struct {
    uint32_t file_size;     // File size in bytes
    uint8_t  hash[32];      // SHA-256 hash of file
    char     filename[256]; // Null-terminated filename
} __attribute__((packed)) tcp_share_file_t;

// TCP_SHARE_ACK: Server -> Client
typedef struct {
    uint32_t file_id;       // Assigned file ID
} __attribute__((packed)) tcp_share_ack_t;

// TCP_LIST_FILES: Client -> Server (header only, no additional data)

// FILE_INFO: Part of TCP_FILE_LIST response
typedef struct {
    uint32_t file_id;       // File ID
    uint32_t file_size;     // File size in bytes
    uint8_t  hash[32];      // SHA-256 hash
    uint8_t  num_peers;     // Number of peers with this file
    char     filename[256]; // Null-terminated filename
} __attribute__((packed)) file_info_t;

// TCP_FILE_LIST: Server -> Client
// Format: tcp_header_t followed by array of file_info_t structs

// TCP_REQUEST_FILE: Client -> Server
typedef struct {
    uint32_t file_id;       // Requested file ID
} __attribute__((packed)) tcp_request_file_t;

// TCP_FILE_LOCATION: Server -> Client
typedef struct {
    uint32_t file_id;       // File ID
    uint32_t file_size;     // File size in bytes
    uint8_t  hash[32];      // SHA-256 hash
    uint32_t peer_ip;       // Peer's IP address (network byte order)
    uint16_t peer_port;     // Peer's UDP port (network byte order)
    char     filename[256]; // Filename for saving
} __attribute__((packed)) tcp_file_location_t;

// TCP_UNREGISTER: Client -> Server (header only, no additional data)

// TCP_ERROR: Server -> Client (error_code in header)

/* ========== UDP Protocol (Peer <-> Peer File Transfer) ========== */

// UDP message types
#define UDP_DATA            1   // Data chunk
#define UDP_ACK             2   // Acknowledgment
#define UDP_REQUEST_FILE    3   // Initiate file transfer
#define UDP_FILE_START      4   // Confirm file transfer start

// Selective Repeat ARQ parameters
#define CHUNK_SIZE          1024    // Data chunk size (bytes)
#define WINDOW_SIZE         64      // Sliding window size (packets)
#define TIMEOUT_MS          500     // Retransmission timeout (milliseconds)
#define MAX_RETRIES         10      // Max retransmissions per packet

// UDP packet header
typedef struct {
    uint8_t  msg_type;      // Message type (UDP_*)
    uint8_t  flags;         // Reserved for flags
    uint16_t checksum;      // 16-bit checksum (simple additive)
    uint32_t seq_num;       // Sequence number
    uint32_t ack_num;       // Acknowledgment number (for cumulative ACK)
    uint16_t data_len;      // Length of data following header
    uint16_t window;        // Receiver window size
} __attribute__((packed)) udp_header_t;

// UDP_REQUEST_FILE: Requester -> Owner
typedef struct {
    uint32_t file_id;       // Requested file ID
    uint8_t  hash[32];      // Expected file hash (for verification)
} __attribute__((packed)) udp_request_file_t;

// UDP_FILE_START: Owner -> Requester
typedef struct {
    uint32_t file_size;     // File size in bytes
    uint32_t num_chunks;    // Total number of chunks
} __attribute__((packed)) udp_file_start_t;

// UDP_DATA: Data packet
// Format: udp_header_t followed by up to CHUNK_SIZE bytes of data

// UDP_ACK: Acknowledgment packet
// Format: udp_header_t with ack_num set to next expected sequence number
// For selective repeat, can include bitmap of received packets

typedef struct {
    uint8_t bitmap[WINDOW_SIZE / 8];  // Bitmap of received packets in window
} __attribute__((packed)) udp_ack_selective_t;

/* ========== Helper Functions ========== */

// Calculate simple additive checksum
static inline uint16_t calculate_checksum(const void *data, size_t len) {
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum += bytes[i];
    }
    // Fold 32-bit sum into 16 bits
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (uint16_t)~sum;
}

// Verify checksum
static inline int verify_checksum(const void *data, size_t len, uint16_t expected) {
    uint16_t computed = calculate_checksum(data, len);
    return computed == expected;
}

#endif // PROTOCOL_H
