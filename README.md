[![Review Assignment Due Date](https://classroom.github.com/assets/deadline-readme-button-22041afd0340ce965d47ae6ef1cefeee28c7c493a6346c4f15d667ab976d596c.svg)](https://classroom.github.com/a/ZrmjaqLW)
# CPE 464: P2P File Sharing with Reliable UDP

## Overview

In this assignment, you will implement a peer-to-peer (P2P) file sharing system that demonstrates the principles of distributed systems and reliable data transfer. The system consists of:

1. **Directory Server (TCP)**: A centralized server that maintains a directory of shared files and coordinates peer discovery
2. **P2P File Transfer (UDP)**: Direct peer-to-peer file transfers using UDP with a selective repeat ARQ protocol for reliability

This architecture mirrors real-world systems (e.g., BitTorrent), where a central tracker manages peer discovery while actual data transfer happens directly between peers.

## Learning Objectives

- Implement a reliable data transfer protocol (Selective Repeat ARQ) on top of unreliable UDP
- Handle packet loss, reordering, and corruption
- Design and implement a peer-to-peer protocol (UDP)
- Work with binary network protocols and packet structures

## System Architecture

```
┌─────────────┐                ┌─────────────┐
│  Client A   │────TCP─────────│  Directory  │
│             │                │   Server    │
│  UDP: 6001  │                │             │
└──────┬──────┘                └─────────────┘
       │                              │
       │                              │ TCP
       │                              │
       │                       ┌──────┴──────┐
       │                       │  Client B   │
       │                       │             │
       └────────UDP────────────│  UDP: 6002  │
         (P2P file transfer)   └─────────────┘
```

### Two-Channel System

#### Channel 1: TCP Control Channel (Client ↔ Directory Server)

The directory server maintains:
- List of online clients and their UDP ports
- Registry of shared files (filename, size, hash, owner)

Clients can:
- **Register**: Connect to server and announce their UDP port
- **Share**: Announce a file they want to share
- **List**: Request list of all available files
- **Request**: Request peer information for a specific file
- **Unregister**: Disconnect from server

#### Channel 2: UDP Data Channel (Peer ↔ Peer)

When a client wants to download a file:
1. Request file location from directory server (TCP)
2. Server responds with peer's IP and UDP port (TCP)
3. Client initiates direct UDP connection to peer
4. File is transferred using Selective Repeat ARQ protocol

## Protocol Specification

### TCP Protocol Messages

All TCP messages start with a 4-byte header:

```c
typedef struct {
    uint8_t  msg_type;      // Message type
    uint8_t  error_code;    // Error code (0 = no error)
    uint16_t data_len;      // Length of data following header
} __attribute__((packed)) tcp_header_t;
```

#### Message Types

| Type | Name | Direction | Description |
|------|------|-----------|-------------|
| 1 | `TCP_REGISTER` | Client → Server | Register with server |
| 2 | `TCP_REGISTER_ACK` | Server → Client | Registration acknowledgment |
| 3 | `TCP_SHARE_FILE` | Client → Server | Announce file to share |
| 4 | `TCP_SHARE_ACK` | Server → Client | Share acknowledgment |
| 5 | `TCP_LIST_FILES` | Client → Server | Request file list |
| 6 | `TCP_FILE_LIST` | Server → Client | File list response |
| 7 | `TCP_REQUEST_FILE` | Client → Server | Request file location |
| 8 | `TCP_FILE_LOCATION` | Server → Client | Peer location response |
| 9 | `TCP_UNREGISTER` | Client → Server | Unregister from server |
| 10 | `TCP_ERROR` | Server → Client | Error response |

See [protocol.h](reference/protocol.h) for complete message structures.

### UDP Protocol Messages

All UDP messages start with a 16-byte header:

```c
typedef struct {
    uint8_t  msg_type;      // Message type
    uint8_t  flags;         // Reserved for flags
    uint16_t checksum;      // 16-bit checksum
    uint32_t seq_num;       // Sequence number
    uint32_t ack_num;       // Acknowledgment number
    uint16_t data_len;      // Length of data following header
    uint16_t window;        // Receiver window size
} __attribute__((packed)) udp_header_t;
```

#### Message Types

| Type | Name | Description |
|------|------|-------------|
| 1 | `UDP_DATA` | Data packet containing file chunk |
| 2 | `UDP_ACK` | Acknowledgment packet |
| 3 | `UDP_REQUEST_FILE` | Initiate file transfer |
| 4 | `UDP_FILE_START` | Confirm transfer start |

## Selective Repeat ARQ Protocol

### Overview

Selective Repeat is a sliding window protocol that allows efficient use of network bandwidth while providing reliability. Unlike Stop-and-Wait, it allows multiple packets to be in-flight simultaneously. Unlike Go-Back-N, it only retransmits packets that are lost rather than the entire window.

**Key Advantages**:
- Multiple packets in flight (better bandwidth utilization)
- Selective retransmission (only lost packets are resent)
- Out-of-order packet buffering (reduced retransmissions)

### Key Parameters

```c
#define CHUNK_SIZE      1024    // Data chunk size (bytes)
#define WINDOW_SIZE     64      // Sliding window size (packets)
#define TIMEOUT_MS      500     // Retransmission timeout (milliseconds)
#define MAX_RETRIES     10      // Max retransmissions per packet
```

### Data Structures

#### Sender Window Entry

Each slot in the sender's window tracks:

```c
typedef struct {
    packet_state_t state;       // PKT_EMPTY, PKT_PENDING, or PKT_ACKED
    uint32_t seq_num;           // Sequence number of this packet
    uint8_t data[CHUNK_SIZE];   // Packet data (buffered for retransmission)
    uint16_t data_len;          // Actual data length (last chunk may be smaller)
    uint64_t send_time_ms;      // When packet was sent (for timeout detection)
    int retries;                // Number of retransmission attempts
} send_window_entry_t;
```

**Window State Transitions**:
- `PKT_EMPTY` → `PKT_PENDING`: When packet is first sent
- `PKT_PENDING` → `PKT_ACKED`: When ACK received
- `PKT_ACKED` → `PKT_EMPTY`: When window slides past this packet

#### Receiver Window Entry

Each slot in the receiver's window tracks:

```c
typedef struct {
    int received;               // 1 if packet has been received
    uint32_t seq_num;           // Expected sequence number for this slot
    uint8_t data[CHUNK_SIZE];   // Buffered packet data
    uint16_t data_len;          // Actual data length
} recv_window_entry_t;
```

### Detailed Sender Algorithm

#### Initialization (`sr_sender_init`)

```c
1. Calculate num_chunks = (file_size + CHUNK_SIZE - 1) / CHUNK_SIZE
2. Initialize base = 0 (oldest unacknowledged packet)
3. Initialize next_seq = 0 (next packet to send)
4. Initialize all window slots to PKT_EMPTY
5. Store file data pointer and send function
```

#### Sending Packets (`sr_sender_send_window`)

This function is called periodically from the event loop:

```c
1. Get current time (for timestamps)
2. While next_seq < base + WINDOW_SIZE AND next_seq < num_chunks:
   a. Calculate window index: idx = next_seq % WINDOW_SIZE
   b. If window[idx].state != PKT_EMPTY: break (slot occupied)
   c. Extract chunk from file:
      - offset = next_seq * CHUNK_SIZE
      - chunk_len = min(CHUNK_SIZE, file_size - offset)
   d. Build UDP data packet:
      - Set msg_type = UDP_DATA
      - Set seq_num = next_seq (convert to network byte order!)
      - Set data_len = chunk_len
      - Copy chunk data after header
      - Calculate and set checksum
   e. Send packet:
      - If sender->lossy_link != NULL: use lossy_send(sender->lossy_link, ...)
      - Otherwise: use sendto(sender->udp_fd, ...)
   f. Update window entry:
      - state = PKT_PENDING
      - seq_num = next_seq
      - send_time_ms = current time
      - retries = 0
      - Copy data for potential retransmission
   g. Increment next_seq
3. Return number of packets sent
```

**Important**: You must buffer the packet data in the window entry because you may need to retransmit it later!

#### Handling ACKs (`sr_sender_handle_ack`)

Called when a UDP_ACK packet is received:

```c
1. Extract ack_num from ACK header (convert from network byte order!)
2. Validate ACK is within window:
   - If ack_num < base: Old ACK, ignore
   - If ack_num >= base + WINDOW_SIZE: Future ACK, ignore
   - Otherwise: Valid ACK, process it
3. Calculate window index: idx = ack_num % WINDOW_SIZE
4. Verify packet matches:
   - If window[idx].state == PKT_PENDING AND window[idx].seq_num == ack_num:
     * Mark window[idx].state = PKT_ACKED
     * Increment chunks_acked counter
5. Attempt to slide window (CRITICAL STEP):
   While base < num_chunks:
     a. idx = base % WINDOW_SIZE
     b. If window[idx].state != PKT_ACKED: break
     c. Mark window[idx].state = PKT_EMPTY (free slot for new packets)
     d. Increment base
```

**Key Insight**: The window only slides when **consecutive** packets starting from `base` are ACKed. This is why we can buffer out-of-order packets efficiently.

#### Checking Timeouts (`sr_sender_check_timeouts`)

Called periodically (e.g., every 100ms) from event loop:

```c
1. Get current time
2. For each seq in range [base, next_seq):
   a. Calculate idx = seq % WINDOW_SIZE
   b. If window[idx].state != PKT_PENDING: skip
   c. If window[idx].seq_num != seq: skip (different packet)
   d. Calculate elapsed = current_time - window[idx].send_time_ms
   e. If elapsed >= TIMEOUT_MS:
      - Check if window[idx].retries >= MAX_RETRIES:
        * If yes: return -1 (transfer failed)
      - Rebuild packet from buffered data
      - Resend packet:
        * If sender->lossy_link != NULL: use lossy_send(sender->lossy_link, ...)
        * Otherwise: use sendto(sender->udp_fd, ...)
      - Update window[idx].send_time_ms = current_time
      - Increment window[idx].retries
      - Count this retransmission
3. Return count of retransmitted packets
```

**Important**: Only retransmit packets that have timed out, not the entire window!

### Window Sliding Example

Let's trace a detailed example with WINDOW_SIZE=4:

```
Initial state:
  Sender: base=0, next_seq=0
  Receiver: base=0

Step 1: Sender sends packets 0,1,2,3
  Sender window: [0-PENDING][1-PENDING][2-PENDING][3-PENDING]
  Sender: base=0, next_seq=4

Step 2: Receiver gets 0,2,3 (packet 1 lost)
  Receiver window: [0-rcvd][empty][2-rcvd][3-rcvd]
  - Receive 0: Deliver to app, slide to base=1
  - Receive 2: Buffer (waiting for 1)
  - Receive 3: Buffer (waiting for 1)
  Receiver: base=1, sends ACK(0), ACK(2), ACK(3)

Step 3: Sender receives ACK(0), ACK(2), ACK(3)
  Sender window: [0-ACKED][1-PENDING][2-ACKED][3-ACKED]
  - ACK(0): Slide to base=1
  - ACK(2): Mark as acked, but can't slide (1 not acked)
  - ACK(3): Mark as acked, but can't slide (1 not acked)
  Sender: base=1, next_seq=4

Step 4: Sender sends packet 4
  Sender window: [1-PENDING][2-ACKED][3-ACKED][4-PENDING]
  (Window wraps around: idx 0 now holds packet 4)
  Sender: base=1, next_seq=5

Step 5: Packet 1 timeout, retransmit
  Sender resends packet 1
  Update send_time_ms, increment retries

Step 6: Receiver gets packet 1
  Receiver window: [1-rcvd][2-rcvd][3-rcvd][empty]
  - Buffer packet 1
  - Window slide: Deliver 1,2,3 to app, base=4
  Receiver: base=4, sends ACK(1)

Step 7: Sender receives ACK(1)
  Sender window: [1-ACKED][2-ACKED][3-ACKED][4-PENDING]
  - Mark 1 as ACKED
  - Slide window: 1,2,3 all acked, slide to base=4
  Sender: base=4, next_seq=5
```

### Handling Out-of-Order Packets

The receiver must buffer out-of-order packets. Example:

```
Window size: 4, Base: 0

Receive seq=0: Buffer in slot 0, deliver immediately, slide to base=1
Receive seq=2: Buffer in slot 2 (can't deliver yet, waiting for 1)
Receive seq=3: Buffer in slot 3 (can't deliver yet, waiting for 1)
Receive seq=1: Buffer in slot 1, now deliver 1,2,3, slide to base=4
```

**Why this works**: The window slots act as a reassembly buffer, holding out-of-order packets until the gaps are filled.

### Checksum Calculation

Use a simple additive checksum:

```c
uint16_t calculate_checksum(const void *data, size_t len) {
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
```

When sending: calculate checksum over entire packet (with checksum field set to 0 first).
When receiving: verify checksum matches.

## File Integrity Verification

All files are identified by their SHA-256 hash. When sharing a file:

1. Compute SHA-256 hash of entire file
2. Include hash in `TCP_SHARE_FILE` message
3. Server stores hash in directory
4. When requesting file, client receives expected hash
5. After download, client recomputes hash and verifies match

This ensures:
- Files are transferred correctly
- Protection against corruption
- Ability to detect identical files

## Implementation Requirements

### Directory Server

The server is fully written for you in `server.c`. It must:

1. Listen for TCP connections on specified port
2. Handle multiple clients simultaneously (use `poll()`)
3. Maintain client registry with:
   - Client ID
   - TCP socket
   - UDP port
   - Connection status
4. Maintain file registry with:
   - File ID
   - Filename
   - File size
   - SHA-256 hash
   - Owner client ID
5. Process all TCP protocol messages correctly
6. Remove files when client disconnects

### Client

The client must:

1. **TCP Operations**:
   - Connect to directory server
   - Register with server
   - Share files (read file, compute hash, send to server)
   - List available files
   - Request file location
   - Unregister on exit

2. **UDP Operations (Sender)**:
   - Listen for incoming file requests
   - Implement selective repeat sender
   - Track send window and timeouts
   - Retransmit lost packets
   - Handle ACKs correctly

3. **UDP Operations (Receiver)**:
   - Request file from peer
   - Implement selective repeat receiver
   - Buffer out-of-order packets
   - Send ACKs for received packets
   - Deliver data in-order
   - Verify file hash

### Client Command Interface

Your client **must** implement an interactive command-line interface that accepts the following commands from stdin:

#### 1. `share <filepath>`

Share a file with the network.

**Behavior:**
- Read the file from the local filesystem (done)
- Compute SHA-256 hash of the file contents (done)
- Send `TCP_SHARE_FILE` message to server with filename, size, and hash (done)
- Receive `TCP_SHARE_ACK` with assigned file_id (done)
- Print confirmation message (done)

**Example:**
```
> share /tmp/myfile.txt
[INFO] Shared file: myfile.txt (file_id=1, size=2048 bytes)
```

**Error Handling:**
- If file doesn't exist or can't be read: print error, don't send to server
- If server returns error: print error message

#### 2. `list`

List all files available in the network.

**Behavior:**
- Send `TCP_LIST_FILES` message to server
- Receive `TCP_FILE_LIST` response with array of file_info_t
- Display files in readable format showing:
  - File ID
  - Filename
  - File size
  - Number of peers sharing the file
  - SHA-256 hash (optional, can show first 8 hex chars)

**Example:**
```
> list
=== Available Files ===
ID  | Filename        | Size    | Peers | Hash (partial)
----|-----------------|---------|-------|------------------
1   | test.txt        | 2048 B  | 2     | a3f5d1e2...
2   | document.pdf    | 51200 B | 1     | b7c3a8f1...

Total: 2 files available
```

**If no files:**
```
> list
=== Available Files ===
No files available
```

#### 3. `get <file_id>`

Download a file from a peer.

**Behavior:**
- Send `TCP_REQUEST_FILE` message to server with file_id
- Receive `TCP_FILE_LOCATION` response with peer's IP, UDP port, filename, size, and hash
- Send `UDP_REQUEST_FILE` to the peer
- Receive `UDP_FILE_START` from peer
- Initialize selective repeat receiver
- Receive file using selective repeat protocol
- Save file to current directory with original filename
- Compute SHA-256 hash of received file
- Verify hash matches expected hash
- Print success or error message

**Example (Success):**
```
> get 1
[INFO] Requesting file_id=1 from server
[INFO] Peer location: 127.0.0.1:6002
[INFO] Starting file transfer: test.txt (2048 bytes)
[INFO] File transfer complete
[INFO] Hash verified: a3f5d1e234b8c7f1...
[INFO] Saved file: test.txt
```

**Example (Hash Mismatch):**
```
> get 1
[INFO] Requesting file_id=1 from server
[INFO] Peer location: 127.0.0.1:6002
[INFO] Starting file transfer: test.txt (2048 bytes)
[INFO] File transfer complete
[ERROR] Hash mismatch!
[ERROR]   Expected: a3f5d1e234b8c7f1...
[ERROR]   Received: b4c8e2f345d9a8c2...
[ERROR] File may be corrupted
```

**Error Handling:**
- If file_id doesn't exist: print error from server
- If no peers have the file: print error from server
- If transfer times out or fails: print error
- If hash doesn't match: print error, don't keep corrupted file
- If filename already exists in current directory: **overwrite it without warning** (this matches behavior of tools like wget/curl and keeps implementation simple)

#### 4. `quit`

Exit the client gracefully.

**Behavior:**
- Send `TCP_UNREGISTER` message to server
- Close TCP connection
- Close UDP socket
- Clean up any ongoing transfers
- Exit program

**Example:**
```
> quit
[INFO] Shutting down...
[INFO] Unregistered from server
```

### Command Input Requirements

**Interactive Mode:**
- Display a prompt (`> `) when waiting for user input
- Read commands from stdin using `fgets()` or similar
- Parse command and arguments using `sscanf()` or `strtok()`
- Handle commands case-sensitively (all lowercase)
- Ignore empty lines
- For unknown commands, print available commands

**Event Loop Integration:**
- Use `poll()` with stdin (STDIN_FILENO) as one of the file descriptors
- Check for `POLLIN` on stdin to detect user input
- Process commands without blocking UDP/TCP events
- See "Event Loop Integration" section for details

**Example Input Handling:**
```c
struct pollfd fds[2];
fds[0].fd = STDIN_FILENO;
fds[0].events = POLLIN;
fds[1].fd = udp_fd;
fds[1].events = POLLIN;

while (running) {
    poll(fds, 2, 100);  // 100ms timeout

    if (fds[0].revents & POLLIN) {
        char line[1024];
        if (fgets(line, sizeof(line), stdin)) {
            // Parse and handle command
            handle_command(line);
            printf("> ");
            fflush(stdout);
        }
    }
    // ... handle UDP events ...
}
```

### Handling Concurrent Operations

A client may need to simultaneously:
- Read user commands from stdin
- Maintain TCP connection to server
- Send a file to one peer (UDP sender role)
- Receive a file from another peer (UDP receiver role)

You must use `poll()` to handle multiple socket operations. Do not use `select()` or threads for this assignment.

**Key Points**:
1. Use poll timeout (100ms) to drive sender operations (send_window, check_timeouts)
2. Sender is **proactive**: continuously sends and retransmits
3. Receiver is **reactive**: responds to incoming data packets
4. Both roles can be active simultaneously (but simplified version can restrict to one at a time)

## Testing with Lossy UDP

You are provided with a lossy UDP shim layer (`lossy_udp.c` and `lossy_udp.h`) that simulates packet loss. This allows you to test your selective repeat implementation.

### Using Lossy UDP in Your Client

The lossy UDP layer is already integrated into the selective repeat API. Simply pass a `lossy_link_t` pointer when initializing:

```c
#include "lossy_udp.h"

// In your client state
lossy_link_t lossy_link;
double loss_rate = 0.20;  // From command line argument

// Initialize lossy link
int udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
lossy_init(&lossy_link, udp_fd, loss_rate, time(NULL));  // Random seed

// Initialize selective repeat sender (simplified API!)
sr_sender_init(&sender, udp_fd, &peer_addr, file_data, file_size,
               &lossy_link);  // Just pass the lossy_link!

// Initialize selective repeat receiver
sr_receiver_init(&receiver, udp_fd, &peer_addr, file_size, num_chunks,
                 &lossy_link);  // Same simple interface!
```

**Key Points:**
- Pass `&lossy_link` to enable packet loss simulation
- Pass `NULL` for no packet loss (normal UDP)
- The selective repeat code handles sending internally:
  - `if (lossy_link) lossy_send(...) else sendto(...)`
- **No wrapper functions needed!** Much simpler than function pointers.

**For receiving:**
- Always use regular `recvfrom()` on the UDP socket
- Packet loss only affects sending (simulating network drops)

Your selective repeat implementation should successfully transfer files even with 50% packet loss!

## Provided Files (Skeleton Code)

The `skeleton/` directory contains the following files:

### Complete Implementations (Do Not Modify)

These files are **fully implemented** and ready to use:

- **`server.c`**: Complete directory server implementation
  - Handles all TCP protocol messages
  - Manages client and file registries
  - Uses `poll()` for concurrency

- **`protocol.h`**: Complete protocol definitions
  - All TCP and UDP message structures
  - Message type constants
  - Checksum calculation functions
  - All structures use `__attribute__((packed))` for correct binary layout

- **`common.h` / `common.c`**: Complete utility library
  - `create_tcp_socket()` - Create TCP socket with SO_REUSEADDR
  - `create_udp_socket()` - Create UDP socket with SO_REUSEADDR
  - `bind_socket()` - Bind socket to port
  - `get_time_ms()` - Get current time in milliseconds (for timeouts)
  - `read_file_to_buffer()` - Read entire file into memory buffer
  - `write_buffer_to_file()` - Write buffer to file
  - `compute_sha256()` - Compute SHA-256 hash using OpenSSL
  - `hash_to_string()` - Convert hash to hex string
  - Debug macros: `DEBUG_PRINT()`, `INFO_PRINT()`, `ERROR_PRINT()`

- **`lossy_udp.h` / `lossy_udp.c`**: Lossy UDP simulator
  - `lossy_init()` - Initialize lossy link with loss rate
  - `lossy_send()` - Send with simulated packet loss
  - `lossy_recv()` - Normal receive (for symmetry)
  - Uses xorshift64 PRNG for deterministic testing

- **`Makefile`**: Build system
  - Compiles with `-std=c99 -D_POSIX_C_SOURCE=200809L`
  - Links with OpenSSL (`-lcrypto`)
  - Produces `server` and `client` binaries

### Skeleton Files (You Must Implement)

These files provide structure with TODO comments:

- **`selective_repeat.h`**: Complete header file
  - Data structure definitions for sender and receiver
  - Function prototypes for all SR functions
  - **Do not modify this file** - implement the .c file

- **`selective_repeat.c`**: **YOU IMPLEMENT THIS**
  - Contains 11 TODOs with detailed implementation guidance
  - Skeleton functions for sender and receiver
  - Helper functions `build_data_packet()` and `build_ack_packet()`

- **`client.c`**: **YOU IMPLEMENT THIS**
  - Minimal stub file
  - You must design and implement the entire client
  - Includes argument parsing skeleton

## Command-Line Arguments

### Server

```bash
./server <port>
```

**Arguments:**
- `<port>` (required): TCP port for directory server to listen on (e.g., 5000)

**Example:**
```bash
./server 5000
```

The server will print the port it's listening on and remain running until terminated (Ctrl+C).

### Client

```bash
./client <server_ip> <server_port> [udp_port] [loss_rate]
```

**Arguments:**
- `<server_ip>` (required): IP address or hostname of directory server (e.g., "127.0.0.1" or "localhost" if running locally)
- `<server_port>` (required): TCP port of directory server (e.g., 5000)
- `[udp_port]` (optional): UDP port for this client to bind to (default: OS assigns random port)
- `[loss_rate]` (optional): Simulated packet loss rate for testing (0.0 to 1.0, default: 0.0)

**Examples:**

No packet loss (normal operation):
```bash
./client 127.0.0.1 5000
./client localhost 5000 6001
./client 192.168.1.100 5000 6001 0.0
```

With packet loss (for testing):
```bash
./client 127.0.0.1 5000 6001 0.20    # 20% packet loss
./client 127.0.0.1 5000 6001 0.50    # 50% packet loss
```

**Notes:**
- If `udp_port` is not specified, the OS will assign a random available port (recommended for multiple clients)
- If `udp_port` is specified, you must ensure it's not already in use
- The `loss_rate` parameter only affects packets **sent** by this client (simulating outbound drops on the lossy link)
- Loss rate of 0.0 means no packet loss (default), 1.0 means all packets are dropped
- The client will print its assigned UDP port after connecting to the server
- if you wish to specify a loss rate, you must also specify a UDP port

## What You Need to Implement

Based on the skeleton files, you must implement:

1. **`selective_repeat.c`**: Complete selective repeat protocol (11 functions)
2. **`client.c`**: Complete P2P client from scratch

## Tips and Common Pitfalls

### Protocol Implementation

1. **Byte Order**: All multi-byte integers must be in network byte order (big-endian). Use `htons()`, `htonl()`, `ntohs()`, `ntohl()`.
   ```c
   // WRONG
   header->seq_num = 42;

   // CORRECT
   header->seq_num = htonl(42);

   // WRONG
   uint32_t seq = header->seq_num;

   // CORRECT
   uint32_t seq = ntohl(header->seq_num);
   ```

2. **Struct Padding**: Use `__attribute__((packed))` to prevent compiler padding in protocol structures.

3. **Checksums**: Set checksum field to 0 before computing checksum!
   ```c
   // CORRECT checksum calculation
   hdr->checksum = 0;
   hdr->checksum = calculate_checksum(packet, packet_len);

   // CORRECT checksum verification
   uint16_t received_checksum = hdr->checksum;
   hdr->checksum = 0;
   uint16_t computed = calculate_checksum(packet, packet_len);
   if (received_checksum != computed) {
       // Checksum failed, drop packet
   }
   hdr->checksum = received_checksum;  // Restore
   ```

### Selective Repeat - Common Mistakes

1. **Window Base Confusion**:
   - `base` = oldest unacknowledged packet (or next expected on receiver)
   - `next_seq` = next packet to send (sender only)
   - Window range: `[base, base + WINDOW_SIZE)`
   ```c
   // WRONG: using next_seq as base
   if (seq_num < next_seq) { /* ... */ }

   // CORRECT: using base
   if (seq_num < base) { /* duplicate */ }
   ```

2. **Window Index Calculation**:
   ```c
   // CORRECT: modulo arithmetic for circular buffer
   int idx = seq_num % WINDOW_SIZE;

   // WRONG: linear index (will overflow)
   int idx = seq_num;
   ```

3. **Not Buffering Packet Data for Retransmission**:
   ```c
   // WRONG: only storing pointer
   window[idx].data = file_data + offset;  // DON'T DO THIS

   // CORRECT: copying data
   memcpy(window[idx].data, file_data + offset, chunk_len);
   ```

4. **Forgetting to Slide Window**:
   ```c
   // WRONG: only marking as ACKed
   window[idx].state = PKT_ACKED;

   // CORRECT: mark as ACKed AND try to slide
   window[idx].state = PKT_ACKED;
   while (base < num_chunks && window[base % WINDOW_SIZE].state == PKT_ACKED) {
       window[base % WINDOW_SIZE].state = PKT_EMPTY;
       base++;
   }
   ```

5. **Not ACKing Duplicates**:
   ```c
   // WRONG: ignoring duplicates
   if (seq_num < base) return;

   // CORRECT: ACK duplicates
   if (seq_num < base) {
       sr_receiver_send_ack(receiver, seq_num);  // Still send ACK!
       return;
   }
   ```

6. **Dropping Out-of-Order Packets**:
   ```c
   // WRONG: only accepting base packet
   if (seq_num != base) return;  // DON'T DO THIS

   // CORRECT: buffering out-of-order
   if (seq_num >= base && seq_num < base + WINDOW_SIZE) {
       // Buffer packet in window
       window[idx].received = 1;
       memcpy(window[idx].data, ...);
   }
   ```

7. **Retransmitting Entire Window** (that's Go-Back-N, not Selective Repeat!):
   ```c
   // WRONG: retransmit all pending packets
   for (seq = base; seq < next_seq; seq++) {
       retransmit(seq);  // DON'T DO THIS
   }

   // CORRECT: only retransmit timed-out packets
   for (seq = base; seq < next_seq; seq++) {
       if (window[idx].state == PKT_PENDING &&
           (now - window[idx].send_time_ms) >= TIMEOUT_MS) {
           retransmit(seq);
       }
   }
   ```

8. **Using `time()` for Timestamps**:
   ```c
   // WRONG: 1-second granularity
   time_t now = time(NULL);

   // CORRECT: millisecond granularity
   uint64_t now = get_time_ms();  // provided in common.h
   ```

9. **Forgetting to Check Sequence Number Match**:
   ```c
   // WRONG: only checking index
   if (window[idx].state == PKT_PENDING) {
       window[idx].state = PKT_ACKED;  // Might be wrong packet!
   }

   // CORRECT: verify sequence number matches
   if (window[idx].state == PKT_PENDING && window[idx].seq_num == ack_num) {
       window[idx].state = PKT_ACKED;
   }
   ```

### Debugging Strategies

1. **Add Debug Prints**: The skeleton provides DEBUG_PRINT macro:
   ```c
   DEBUG_PRINT("Sent DATA seq=%u len=%u", seq_num, data_len);
   DEBUG_PRINT("Received ACK ack_num=%u", ack_num);
   DEBUG_PRINT("Window: base=%u next_seq=%u", base, next_seq);
   DEBUG_PRINT("Retransmitting seq=%u (retry %d)", seq_num, retries);
   ```

2. **Wireshark**: Use Wireshark to capture UDP traffic and verify:
   - Packet format (header fields)
   - Sequence numbers
   - ACK numbers
   - Checksums
   - Data content

3. **Test Incrementally**:
   - **Phase 1**: TCP protocol only (no UDP)
     - Register client
     - Share files
     - List files
     - Request file location
   - **Phase 2**: UDP with 0% loss and tiny files (10-100 bytes)
     - Single chunk transfers
     - Verify hash matches
   - **Phase 3**: UDP with 0% loss and larger files (10KB, 100KB)
     - Multi-chunk transfers
     - Verify window sliding
   - **Phase 4**: UDP with 10-20% loss
     - Verify retransmissions
     - Check timeout logic
   - **Phase 5**: UDP with 50% loss
     - Stress test

4. **Print Window State**: Visualize your window:
   ```c
   void print_sender_window(sr_sender_t *s) {
       printf("Window [base=%u next=%u]: ", s->base, s->next_seq);
       for (uint32_t i = 0; i < WINDOW_SIZE && s->base + i < s->next_seq; i++) {
           int idx = (s->base + i) % WINDOW_SIZE;
           char state = s->window[idx].state == PKT_EMPTY ? 'E' :
                       s->window[idx].state == PKT_PENDING ? 'P' : 'A';
           printf("[%u:%c] ", s->base + i, state);
       }
       printf("\n");
   }
   ```

5. **Count Statistics**:
   ```c
   static int packets_sent = 0;
   static int packets_retransmitted = 0;
   static int acks_received = 0;

   // At end:
   printf("Transfer stats:\n");
   printf("  Packets sent: %d\n", packets_sent);
   printf("  Retransmissions: %d (%.1f%%)\n",
          packets_retransmitted,
          100.0 * packets_retransmitted / packets_sent);
   printf("  ACKs received: %d\n", acks_received);
   ```

6. **Valgrind**: Check for memory leaks and invalid accesses:
   ```bash
   valgrind --leak-check=full --show-leak-kinds=all ./client server 5000
   ```

7. **Small Files First**: Start with tiny files:
   ```bash
   # Create test files
   echo "Hello" > test.txt                    # 6 bytes (1 chunk)
   dd if=/dev/zero of=test.bin bs=1024 count=10  # 10 KB (10 chunks)
   dd if=/dev/urandom of=large.bin bs=1024 count=100  # 100 KB (100 chunks)
   ```

## Submission Instructions

**Submission**: Submit via GitHub Classroom repository
