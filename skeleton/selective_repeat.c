/*
 * CPE 464: P2P File Sharing - Selective Repeat ARQ
 *
 * This file implements the selective repeat protocol for reliable UDP transfer.
 * You need to implement both sender and receiver functionality.
 *
 * Key concepts:
 * - Sliding window protocol with WINDOW_SIZE packets
 * - Timeouts and retransmissions (TIMEOUT_MS, MAX_RETRIES)
 * - Out-of-order packet buffering
 * - Checksum validation
 * - Sequence number management
 */

#include "selective_repeat.h"
#include <string.h>
#include <stdio.h>

/* ========== Sender Implementation ========== */

/*
 * Initialize sender state.
 * Setup window, calculate number of chunks, initialize sequence numbers.
 */
int sr_sender_init(sr_sender_t *sender, int udp_fd,
                   const struct sockaddr_in *peer_addr,
                   const uint8_t *file_data, uint32_t file_size,
                   lossy_link_t *lossy_link) {
    // TODO: Initialize sender structure
    // - Zero out the structure (use memset)
    // - Store parameters: socket fd, peer address, file data/size, lossy_link
    // - Calculate num_chunks (round up: how many CHUNK_SIZE blocks needed?)
    // - Initialize sequence number tracking (base and next_seq)
    // - Initialize all window entries to PKT_EMPTY state
    
    // checking to see if any of these are errors
    if (sender == NULL || peer_addr == NULL || file_data == NULL || lossy_link == NULL){
        return -1;
    }

    memset(sender, 0, sizeof(*sender));

    sender->udp_fd = udp_fd;
    memcpy(&sender->peer_addr, peer_addr, sizeof(struct sockaddr_in));
    sender->file_data = file_data;
    sender->file_size = file_size;
    sender->lossy_link = lossy_link;

    sender->num_chunks = (file_size + CHUNK_SIZE - 1) / CHUNK_SIZE;

    sender->base = 0;
    sender->next_seq = 0;
    sender->chunks_acked = 0;

    for (int i = 0; i < WINDOW_SIZE; i++) {
        sender->window[i].state = PKT_EMPTY;
        sender->window[i].seq_num = 0;
        sender->window[i].data_len = 0;
        sender->window[i].send_time_ms = 0;
        sender->window[i].retries = 0;
    }

    return 0;
}

/*
 * Build a UDP data packet.
 * Creates UDP header with checksum and appends data payload.
 */
__attribute__((unused))
static void build_data_packet(uint8_t *packet, uint32_t seq_num,
                             const uint8_t *data, uint16_t data_len) {
    // TODO: Build data packet
    // 1. Cast packet buffer to udp_header_t and fill in fields:
    //    - msg_type = UDP_DATA
    //    - seq_num (IMPORTANT: convert to network byte order with htonl!)
    //    - data_len (IMPORTANT: convert to network byte order with htons!)
    //    - Set other fields appropriately (flags, ack_num, window)
    // 2. Copy data payload after the header
    // 3. Calculate checksum over entire packet (header + data):
    //    - MUST set checksum field to 0 first
    //    - Use calculate_checksum() from protocol.h
    //    - Store result in checksum field
    
    udp_header_t *hdr = (udp_header_t *)packet;

    hdr->msg_type = UDP_DATA;
    hdr->flags = 0;
    hdr->checksum = 0;
    hdr->seq_num = htonl(seq_num);
    hdr->ack_num = 0;         
    hdr->data_len = htons(data_len);
    hdr->window = htons(WINDOW_SIZE);      
    
    memcpy(packet + sizeof(udp_header_t), data, data_len);

    uint16_t checksum = calculate_checksum(packet, sizeof(udp_header_t) + data_len);
    hdr->checksum = checksum;
}

/*
 * Build a UDP ACK packet.
 */
__attribute__((unused))
static void build_ack_packet(uint8_t *packet, uint32_t ack_num) {
    // TODO: Build ACK packet (similar to data packet but no payload)
    // - Fill in udp_header_t: msg_type=UDP_ACK, ack_num, data_len=0
    // - IMPORTANT: Convert ack_num to network byte order (htonl)
    // - Calculate checksum over header (remember: set checksum=0 first)
    
    udp_header_t *hdr = (udp_header_t *)packet;
    hdr->msg_type = UDP_ACK;
    hdr->flags = 0;
    hdr->seq_num = htonl(0);
    hdr->ack_num = htonl(ack_num);
    hdr->data_len = htons(0);
    hdr->checksum = 0;
    hdr->window = htons(WINDOW_SIZE);

    uint16_t checksum = calculate_checksum(packet, sizeof(udp_header_t));
    hdr->checksum = checksum;
}

/*
 * Send packets within the current window.
 * Send all unsent packets from next_seq up to base + WINDOW_SIZE.
 * Returns number of packets sent.
 */
int sr_sender_send_window(sr_sender_t *sender) {
    // TODO: Send all unsent packets that fit in the current window
    //
    // For each packet that can be sent (within window bounds):
    //   1. Extract the appropriate chunk from file_data
    //      - Calculate offset and chunk size (last chunk may be smaller!)
    //   2. Build the data packet using build_data_packet()
    //   3. Send the packet:
    //      - Use lossy_send() if lossy_link is set, otherwise sendto()
    //   4. Update the window entry for this packet:
    //      - Mark as PENDING, store send timestamp, copy data for retransmission
    //   5. Advance next_seq
    //
    // HINT: The window can hold WINDOW_SIZE packets at a time
    // HINT: Use get_time_ms() for timestamps
    // Return the number of new packets sent
    
    if (sender == NULL){
        ERROR_PRINT("The sender variable in sr_sender_send_window() is NULL");
        return -1;
    }

    uint64_t now = get_time_ms();
    int packets = 0;

    while (sender->next_seq < sender->base + WINDOW_SIZE && sender->next_seq < sender->num_chunks) {
        int idx = sender->next_seq % WINDOW_SIZE;

        if (sender -> window[idx].state != PKT_EMPTY) break;

        uint32_t offset = sender->next_seq * CHUNK_SIZE;
        uint16_t chunk_len = (offset + CHUNK_SIZE > sender->file_size) ? (sender->file_size - offset) : CHUNK_SIZE;

        uint8_t packet[sizeof(udp_header_t) + CHUNK_SIZE];
        build_data_packet(packet, sender->next_seq, sender->file_data + offset, chunk_len);

        ssize_t packet_len = sizeof(udp_header_t) + chunk_len;

        if (sender->lossy_link) {
            lossy_send(sender->lossy_link,
                    packet,
                    packet_len,
                    0,
                    (struct sockaddr *)&sender->peer_addr,
                    sizeof(sender->peer_addr));
        } 
        else {
            sendto(sender->udp_fd,
                packet,
                packet_len,
                0,
                (struct sockaddr *)&sender->peer_addr,
                sizeof(sender->peer_addr));
        }


        sender->window[idx].state = PKT_PENDING;
        sender->window[idx].seq_num = sender->next_seq;
        sender->window[idx].data_len = chunk_len;
        sender->window[idx].send_time_ms = now;
        sender->window[idx].retries = 0;
        memcpy(sender->window[idx].data, sender->file_data + offset, chunk_len);

        sender->next_seq++;
        packets++;
    }

    return packets;
}

/*
 * Handle an incoming ACK packet.
 * Mark packet as acknowledged and slide window if possible.
 */
int sr_sender_handle_ack(sr_sender_t *sender, const udp_header_t *hdr) {
    // TODO: Process incoming ACK packet
    //
    // 1. Extract and validate the ack_num
    //    - IMPORTANT: Convert from network byte order (ntohl)
    //    - Ignore ACKs outside the current window range
    //
    // 2. Mark the corresponding packet as acknowledged in the window
    //    - Find the correct window slot (think: circular buffer)
    //    - Verify the sequence number matches before marking
    //    - Update chunks_acked counter
    //
    // 3. Attempt to slide the window forward
    //    - The window can only slide when consecutive packets starting
    //      from base are all acknowledged
    //    - Free up slots as the window slides (mark as PKT_EMPTY)
    //    - This allows new packets to be sent
    //
    // Think: Why does selective repeat only slide on consecutive ACKs?
    uint32_t ack_num = ntohl(hdr->ack_num);
    if (ack_num < sender->base) return 0;

    if (hdr->msg_type != UDP_ACK) return 0;

    if (ack_num >= sender->base + WINDOW_SIZE || ack_num >= sender->num_chunks) return 0;

    int idx = ack_num % WINDOW_SIZE;

    if (sender->window[idx].state == PKT_PENDING && sender->window[idx].seq_num == ack_num) {
        sender->window[idx].state = PKT_ACKED;
        sender->chunks_acked++;
    }

    while (sender->base < sender->num_chunks) {
        int base_idx = sender->base % WINDOW_SIZE;
        if (sender->window[base_idx].state != PKT_ACKED) break;
        sender->window[base_idx].state = PKT_EMPTY;
        sender->base++;
    }

    return 0;
}

/*
 * Check for timed-out packets and retransmit.
 * Scans all PENDING packets in window and retransmits if timeout expired.
 * Returns number of packets retransmitted, or -1 if max retries exceeded.
 */
int sr_sender_check_timeouts(sr_sender_t *sender, uint64_t now_ms) {
    // TODO: Detect and retransmit timed-out packets
    //
    // Scan all packets currently in the window (between base and next_seq):
    //   - For each PENDING packet that has timed out (>= TIMEOUT_MS):
    //     * Check retry limit (return -1 if MAX_RETRIES exceeded)
    //     * Retransmit the packet (data is buffered in window entry)
    //     * Update the send timestamp and retry count
    //
    // IMPORTANT: Only retransmit individual timed-out packets, not the entire window!
    // That's what makes this Selective Repeat, not Go-Back-N.
    //
    // Return: Number of packets retransmitted, or -1 on failure
    
    int retransmissions = 0;

    for (uint32_t seq = sender->base; seq < sender->next_seq; seq++) {
        int idx = seq % WINDOW_SIZE;
        send_window_entry_t *entry = &sender->window[idx];

        if (entry->state != PKT_PENDING) continue;
        if (entry->seq_num != seq) continue;

        if (now_ms - entry->send_time_ms >= TIMEOUT_MS) {
            if (entry->retries >= MAX_RETRIES) return -1;

            uint8_t packet[sizeof(udp_header_t) + CHUNK_SIZE];
            build_data_packet(packet, entry->seq_num, entry->data, entry->data_len);
            size_t packet_len = sizeof(udp_header_t) + entry->data_len;

            if (sender->lossy_link) {
                lossy_send(sender->lossy_link,
                        packet,
                        packet_len,
                        0,
                        (struct sockaddr *)&sender->peer_addr,
                        sizeof(sender->peer_addr));
            } 
            else {
                sendto(sender->udp_fd,
                    packet,
                    packet_len,
                    0,
                    (struct sockaddr *)&sender->peer_addr,
                    sizeof(sender->peer_addr));
            }


            entry->send_time_ms = now_ms;
            entry->retries++;
            retransmissions++;
        }
    }

    return retransmissions;
}

/*
 * Check if file transfer is complete.
 * Returns 1 if all chunks have been acknowledged.
 */
int sr_sender_is_complete(const sr_sender_t *sender) {
    // TODO: Check if all chunks have been acknowledged
    if(sender->chunks_acked >= sender->num_chunks) return 1;
    return 0;
}

/* ========== Receiver Implementation ========== */

/*
 * Initialize receiver state.
 * Allocate file buffer and setup window.
 */
int sr_receiver_init(sr_receiver_t *receiver, int udp_fd,
                     const struct sockaddr_in *peer_addr,
                     uint32_t file_size, uint32_t num_chunks,
                     lossy_link_t *lossy_link) {
    // TODO: Initialize receiver structure
    // - Zero out the structure (use memset)
    // - Store parameters: socket fd, peer address, file_size, num_chunks, lossy_link
    // - Allocate file_buffer to hold the complete file (use malloc)
    // - Initialize base = 0 (next expected sequence number)
    // - Initialize all window entries as not received
    //
    // The file_buffer will be filled in order as packets are received and the window slides
    
    // if there is an error with the inputs
    if(receiver == NULL || peer_addr == NULL || lossy_link == NULL){
        ERROR_PRINT();
        return -1;
    }

    memset(receiver, 0, sizeof(*receiver));

    receiver->udp_fd = udp_fd;
    memcpy(&receiver->peer_addr, peer_addr, sizeof(struct sockaddr_in));
    receiver->file_size = file_size;
    receiver->num_chunks = num_chunks;
    receiver->lossy_link = lossy_link;                    
    
    receiver->file_buffer = malloc(file_size);
    if (!receiver->file_buffer) {
        perror("malloc");
        return -1;
    }

    receiver->base = 0;
    receiver->chunks_received = 0;

    for (int i = 0; i < WINDOW_SIZE; i++) {
        receiver->window[i].received = 0;
        receiver->window[i].seq_num = 0;
        receiver->window[i].data_len = 0;
        memset(receiver->window[i].data, 0, CHUNK_SIZE);
    }

    return 0;
}                        
/*
 * Handle an incoming data packet.
 * Verify checksum, buffer if in window, send ACK, slide window if possible.
 */
int sr_receiver_handle_data(sr_receiver_t *receiver, const uint8_t *packet, size_t len) {
    // TODO: Process incoming data packet
    //
    // 1. Validate the packet:
    //    - Check minimum length (must have complete header)
    //    - Verify checksum (IMPORTANT: set checksum field to 0 before computing!)
    //    - Extract seq_num (IMPORTANT: convert from network byte order with ntohl)
    //
    // 2. Determine how to handle based on sequence number:
    //    Think about three cases:
    //    - Packet is behind the window (already delivered to application)
    //    - Packet is ahead of the window (too far in future, can't buffer)
    //    - Packet is within the window (buffer it!)
    //
    // 3. For packets that should be acknowledged:
    //    - Buffer the data in the appropriate window slot
    //    - Send an ACK back to the sender
    //    - Update chunks_received counter (if not a duplicate)
    //
    // 4. Try to slide the window:
    //    - Check if we now have consecutive packets starting from base
    //    - Deliver in-order data to the file_buffer
    //    - Advance base and free up window slots
    //
    // KEY INSIGHT: Why do we ACK duplicates? What happens if ACKs are lost?
    
    if (len < sizeof(udp_header_t)) return 0;

    udp_header_t hdr;
    memcpy(&hdr, packet, sizeof(udp_header_t));

    uint16_t received_checksum = hdr.checksum;

    uint8_t temp[len];
    memcpy(temp, packet, len);
    ((udp_header_t *)temp)->checksum = 0;

    uint16_t computed_checksum = calculate_checksum(temp, len);

    if (received_checksum != computed_checksum) return 0;
    if (hdr.msg_type != UDP_DATA) return 0;

    uint32_t seq_num = ntohl(hdr.seq_num);
    uint16_t data_len = ntohs(hdr.data_len);
    if (len < sizeof(udp_header_t) + data_len) return 0;

    const uint8_t *payload = packet + sizeof(udp_header_t);

    if (seq_num >= receiver->num_chunks) return 0;

    if (seq_num < receiver->base) {
        sr_receiver_send_ack(receiver, seq_num);
        return 0;
    }

    if (seq_num >= receiver->base + WINDOW_SIZE) return 0;

    int index = seq_num % WINDOW_SIZE;
    recv_window_entry_t *slot = &receiver->window[index];

    if (!slot->received) {
        slot->received = 1;
        slot->seq_num = seq_num;
        slot->data_len = data_len;
        memcpy(slot->data, payload, data_len);
        receiver->chunks_received++;
    }

    sr_receiver_send_ack(receiver, seq_num);

    while (receiver->base < receiver->num_chunks) {
        int base_idx = receiver->base % WINDOW_SIZE;
        recv_window_entry_t *base_slot = &receiver->window[base_idx];

        if (!base_slot->received) break;

        uint32_t offset = receiver->base * CHUNK_SIZE;
        memcpy(receiver->file_buffer + offset, base_slot->data, base_slot->data_len);

        base_slot->received = 0;
        receiver->base++;
    }

    return 0;
}

/*
 * Send an ACK for a received packet.
 */
int sr_receiver_send_ack(sr_receiver_t *receiver, uint32_t seq_num) {
    // TODO: Send ACK for the given sequence number
    // - Build an ACK packet using build_ack_packet()
    // - Send using lossy_send() if lossy_link is set, otherwise sendto()
    uint8_t packet[sizeof(udp_header_t)];
    build_ack_packet(packet, seq_num);

    if (receiver->lossy_link) {
        lossy_send(receiver->lossy_link,
           packet,
           sizeof(packet),
           0,
           (struct sockaddr *)&receiver->peer_addr,
           sizeof(receiver->peer_addr));

    }
    else sendto(receiver->udp_fd,
               packet,
               sizeof(packet),
               0,
               (struct sockaddr *)&receiver->peer_addr,
               sizeof(receiver->peer_addr));
    

    return 0;
}

/*
 * Check if file transfer is complete.
 * Returns 1 if all chunks have been received and delivered.
 */
int sr_receiver_is_complete(const sr_receiver_t *receiver) {
    // TODO: Check if all chunks have been received and delivered
    // HINT: When is the file complete? Think about base and num_chunks
   if (receiver->base >= receiver->num_chunks) return 1;
   return 0;
}

/*
 * Get the assembled file buffer.
 * Returns pointer to complete file data.
 */
const uint8_t* sr_receiver_get_file(const sr_receiver_t *receiver, uint32_t *size) {
    if (size) {
        *size = receiver->file_size;
    }
    return receiver->file_buffer;
}

/* ========== Cleanup ========== */

void sr_sender_cleanup(sr_sender_t *sender) {
    // File data is owned by caller, don't free
    (void)sender;
}

void sr_receiver_cleanup(sr_receiver_t *receiver) {
    if (receiver->file_buffer) {
        free(receiver->file_buffer);
        receiver->file_buffer = NULL;
    }
}
