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
#include "share_file_logic.h"
#include <poll.h>
#include <signal.h>
#include <time.h>

// global flag for loop
volatile int running = 1;

// gloabla vars for sender
static sr_sender_t global_sender;
static uint8_t* global_file_buffer = NULL;
static int transfer_active = 0;

// global vars for receiver
// static sr_sender_t global_receiver;
// static int receive_active = 0;
// static char receive_filename[256];
// static struct sockaddr_in receive_peer_adddres;


int share(int tcp_sock, const char *filename) { 

    // Read the file from the local filesystem
    // check to make sure the file is valid
    if (filename == NULL){
        ERROR_PRINT("There was an error sharing the file");
        return 1;
    }

    // reading the path here
    uint8_t* buff = NULL;
    size_t size = 0;
    if (read_file_to_buffer(filename, &buff, &size) != 0){
        ERROR_PRINT("There was an error calling read_file_to_buffer");
        free(buff);
        buff = NULL;
        return 1;
    }

    if (size > UINT32_MAX){
        ERROR_PRINT("The var size is too large");
        free(buff);
        buff = NULL;
        return 1;
    }

    // computing the SHA256
    uint8_t hash[32];
    compute_sha256(buff, size, hash);

    // free the buffer from read_file_to_buffer()
    free(buff);
    buff = NULL;

    // Send `TCP_SHARE_FILE` message to server with filename, size, and hash
    tcp_share_file_t tcp_message;
    tcp_message.file_size = (uint32_t) size;
    strncpy(tcp_message.filename, filename, 255);
    tcp_message.filename[255] = '\0';
    memcpy(tcp_message.hash, hash, sizeof(tcp_message.hash));

    // tcp message type
    tcp_header_t tcp_head;
    tcp_head.msg_type = TCP_SHARE_FILE;
    tcp_head.data_len = sizeof(tcp_message);
    tcp_head.error_code = ERR_NONE;

    // sending the header
    ssize_t bytes_sent_head = 0;
    ssize_t n_head = 0;
    while (bytes_sent_head < (ssize_t)sizeof(tcp_head)){

        n_head = send(tcp_sock, (uint8_t *) &(tcp_head) + bytes_sent_head, sizeof(tcp_head) - bytes_sent_head, 0);
        if (n_head < 0){
            ERROR_PRINT("send failed");
            return 1;
        }
        else if (n_head == 0){
            ERROR_PRINT("Connection closed by peer");
            return 1;
        }

        bytes_sent_head += n_head;
    }    

    // send the message (need to loop until all bytes are sent from the message)
    ssize_t bytes_sent_message = 0;
    ssize_t n_message = 0;
    while (bytes_sent_message < (ssize_t)sizeof(tcp_message)){

        n_message = send(tcp_sock, (uint8_t *) &(tcp_message) + bytes_sent_message, sizeof(tcp_message) - bytes_sent_message, 0);
        if (n_message < 0){
            ERROR_PRINT("send failed");
            return 1;
        }
        else if (n_message == 0){
            ERROR_PRINT("Connection closed by peer");
            return 1;
        }

        bytes_sent_message += n_message;
    }

    //- Receive `TCP_SHARE_ACK` with assigned file_id
    tcp_header_t response_header;
    ssize_t bytes_read_message = 0;
    ssize_t n_read = 0;
    while (bytes_read_message < (ssize_t)sizeof(response_header)){
    
        n_read = recv(tcp_sock, (uint8_t *) &(response_header) + bytes_read_message, sizeof(response_header) - bytes_read_message, 0);
        if (n_read < 0){
            ERROR_PRINT("read failed");
            return 1;
        }
        else if (n_read == 0){
            ERROR_PRINT("Connection closed by peer");
            return 1;
        }

        bytes_read_message += n_read;
    }
    
    // safety checks
    if (response_header.msg_type == TCP_ERROR){
        ERROR_PRINT("There was an error: %u", response_header.error_code);
        return 1;
    }

    if (response_header.msg_type != TCP_SHARE_ACK){
        ERROR_PRINT("TCP msg type is not set to SHARE_ACK and this is the error code: %u", response_header.msg_type);
        return 1;
    }

    if (response_header.data_len != sizeof(tcp_share_ack_t)){

        ERROR_PRINT("Expected %zu bytes but got %u", sizeof(tcp_share_ack_t), response_header.data_len);
        return 1;
    }

    // reading the data and putting it into the the share ack
    tcp_share_ack_t share_ack;
    ssize_t bytes_read_share = 0;
    ssize_t n_share = 0;
    while (bytes_read_share < (ssize_t)sizeof(share_ack)){
    
        n_share = recv(tcp_sock, (uint8_t *) &(share_ack) + bytes_read_share, sizeof(share_ack) - bytes_read_share, 0);
        if (n_share < 0){
            ERROR_PRINT("read failed");
            return 1;
        }
        else if (n_share == 0){
            ERROR_PRINT("Connection closed by peer");
            return 1;
        }

        bytes_read_share += n_share;
    }

    // saving the file locally
    add_shared_file(share_ack.file_id, (uint32_t) size, hash, filename);

    // Print confirmation message
    INFO_PRINT("Shared file: %s (file_id=%u, size=%zu bytes)", filename, share_ack.file_id, size);

    // return success
    return 0;
}

int list(int tcp_sock) { 

    // need to check to see if the socket is available (don't because you are passing in the socket)
    // need to sned bytes from socket?

    // Send `TCP_LIST_FILES` message to server
    // first need to create the header and change the message flag to TCP_LIST_FILES
    tcp_header_t tcp_send_message;
    tcp_send_message.error_code = ERR_NONE;
    tcp_send_message.msg_type = TCP_LIST_FILES;
    tcp_send_message.data_len = 0;
    ssize_t bytes_send_request = 0;
    ssize_t n_sent = 0; 

    while (bytes_send_request < sizeof(tcp_header_t)){

        n_sent = send(tcp_sock, (uint8_t *) &(tcp_send_message) + bytes_send_request, sizeof(tcp_send_message) - bytes_send_request, 0);
        if (n_sent < 0){
            ERROR_PRINT("send failed");
            return 1;
        }
        else if (n_sent == 0){
            ERROR_PRINT("Connection closed by peer");
            return 1;
        }

        bytes_send_request += n_sent;
    }

    // now need to see if the message type changed
    tcp_header_t tcp_received_message;
    ssize_t bytes_recv_request = 0;
    ssize_t n_recv = 0; 

    while (bytes_recv_request < (ssize_t)sizeof(tcp_received_message)){

        n_recv = recv(tcp_sock, (uint8_t *) &(tcp_received_message) + bytes_recv_request, sizeof(tcp_received_message) - bytes_recv_request, 0);
        if (n_recv < 0){
            ERROR_PRINT("read failed");
            return 1;
        }
        else if (n_recv == 0){
            ERROR_PRINT("Connection closed by peer");
            return 1;
        }

        bytes_recv_request += n_recv;
    }


    // safety check to see if there was an error
    if (tcp_received_message.msg_type == TCP_ERROR){

        ERROR_PRINT("There was an error: %u", tcp_received_message.error_code);
        return 1;
    }

    // what to do: need to figure out how many fiels there are, loop through them, print them
    // need to allocate a buffer size of data.len if it greater than 0
    if (tcp_received_message.data_len > 0){

        // allocating memory and checking if it was successful
        uint8_t *buffer = malloc(tcp_received_message.data_len);
        if (buffer == NULL){
            free(buffer);
            return 1;
        }

        // var declarations for reading from the socket
        ssize_t bytes_read_dl = 0;
        ssize_t n_dl = 0;

        // condition for loop
        while (bytes_read_dl < tcp_received_message.data_len){

            n_dl = recv(tcp_sock, buffer + bytes_read_dl, tcp_received_message.data_len - bytes_read_dl, 0);
            if (n_dl < 0){
                ERROR_PRINT("read failed");
                free(buffer);
                buffer = NULL;
                return 1;
            }
            else if (n_dl == 0){
                ERROR_PRINT("Connection closed by peer");
                free(buffer);
                buffer = NULL;
                return 1;
            }

            bytes_read_dl += n_dl;
        }

        // buffer now has the payload and I am casting it as a file_info_struct
        file_info_t *file_info = (file_info_t*) buffer;

        // have the number of files
        size_t num_files = tcp_received_message.data_len / sizeof(file_info_t);
        INFO_PRINT("=== Available Files ===");
        for (size_t i = 0; i < num_files; i++){

            // can add SHA 256 computation later
            INFO_PRINT("File ID: %u, Filename: %s, File size %u, Num peers: %u", file_info->file_id, file_info->filename, file_info->file_size, file_info->num_peers);
        }

        // free buffer and point to NULL
        free(buffer);
        buffer = NULL;
    }

    // if there are no files available
    else {

        INFO_PRINT("=== Available Files ===\nNo files available");

    }

    return 0;
}

int get(int tcp_sock, int udp_sock, uint32_t file_id, lossy_link_t *lossy_link) { 
// need to create a message to the server with a request file
// need to check to see it was properly received
// then need to go the the server and build a tcp_request_file with the proper file id
// need to check the response to make sure it was processed correctly
// 

    // creating tcp header for server for requesting a file
    tcp_header_t tcp_send_header;
    tcp_send_header.msg_type = TCP_REQUEST_FILE;
    tcp_send_header.error_code = ERR_NONE;
    tcp_send_header.data_len = sizeof(tcp_request_file_t);

    // var declarations for sending tcp_header
    ssize_t bytes_sent = 0;
    ssize_t n_sent = 0;

    // iterating until the tcp header has been sent with checks
    while (bytes_sent < sizeof(tcp_header_t)){

        n_sent = send(tcp_sock, (uint8_t *) &(tcp_send_header) + bytes_sent, sizeof(tcp_send_header) - bytes_sent, 0);

        if (n_sent < 0){
            ERROR_PRINT("send failed");
            return 1;
        }
        else if (n_sent == 0){
            ERROR_PRINT("send failed");
            return 1;
        }

        bytes_sent += n_sent;
    }

    // creating request file struct to send to server
    tcp_request_file_t req_file;
    req_file.file_id = file_id;
    
    // var declarations for sending tcp_request_file
    ssize_t bytes_send_req = 0;
    ssize_t n_send_req = 0;

    // sending tcp_request_file to server with checks
    while (bytes_send_req < sizeof(tcp_request_file_t)){

        n_send_req = send(tcp_sock, (uint8_t *) &(req_file) + bytes_send_req, sizeof(req_file) - bytes_send_req,0);

        if (n_send_req < 0){
            ERROR_PRINT();
            return 1;
        }
        else if (n_send_req == 0){
            ERROR_PRINT();
            return 1;
        }

        bytes_send_req += n_send_req;

    }

    // creating tcp_header to read from server
    tcp_header_t tcp_receive_header;

    // initializing vars for reading from server
    ssize_t bytes_read = 0;
    ssize_t n_read = 0;

    // iterating until all the data is in the tcp_header
    while (bytes_read < sizeof(tcp_header_t)){

        n_read = recv(tcp_sock, (uint8_t*)&(tcp_receive_header) + bytes_read, sizeof(tcp_header_t) - bytes_read, 0);
        if (n_read < 0){
            ERROR_PRINT("read failed");
            return 1;
        }
        else if (n_read == 0){
            ERROR_PRINT("Connection closed by peer");
            return 1;
        }

        bytes_read += n_read;
    }

    // checking the tcp header return types
    if (tcp_receive_header.msg_type == TCP_ERROR){
        ERROR_PRINT("Server error: %u", tcp_receive_header.error_code);
        return 1;
    }
    if (tcp_receive_header.msg_type != TCP_FILE_LOCATION){
        ERROR_PRINT("Unexpected message type: %u", tcp_receive_header.msg_type);
        return 1;
    }
    if (tcp_receive_header.data_len != sizeof(tcp_file_location_t)){

        ERROR_PRINT("Invalid TCP_FILE_LOCATION: expected %zu bytes, got %u", sizeof(tcp_file_location_t), tcp_receive_header.data_len);
        return 1;
    }

    // creating tcp file location to read from the server
    tcp_file_location_t rec_file;

    // initializing vars for reading from server
    ssize_t bytes_read_file = 0;
    ssize_t n_read_file = 0;

    // iterating until all the data is in the struct for tcp file location
    while (bytes_read_file < sizeof(tcp_file_location_t)){

        n_read_file = recv(tcp_sock, (uint8_t*) &(rec_file) + bytes_read_file, sizeof(tcp_file_location_t) - bytes_read_file, 0);
        if (n_read_file < 0){
            ERROR_PRINT("recv TCP_FILE_LOCATION body failed");
            return 1;
        }
        else if (n_read_file == 0){
            ERROR_PRINT("Connection closed by server while reading file location");
            return 1;
        }

        bytes_read_file +=n_read_file;
    }

    // fill in later
    if (file_id != rec_file.file_id){
        ERROR_PRINT("Server responded with different file_id (got %u, expected %u)", rec_file.file_id, file_id);
        return 1;
    }

    // adding it to receive_filename
    strncpy(receive_filename, rec_file.filename, sizeof(receive_filename) - 1);
    receive_filename[sizeof(receive_filename) - 1] = '\0';

    // need to do UDP peer to peer now
    struct sockaddr_in peer;
    memset(&peer, 0, sizeof(peer));
    peer.sin_port = rec_file.peer_port;
    peer.sin_addr.s_addr = rec_file.peer_ip;
    peer.sin_family = AF_INET;

    INFO_PRINT("Requesting file_id=%u", file_id);
    INFO_PRINT("Peer location: %s:%u", inet_ntoa(peer.sin_addr), ntohs(peer.sin_port));
    INFO_PRINT("Starting file transfer: %s (%u bytes)", rec_file.filename, rec_file.file_size);
    
    // building UDP header
    udp_header_t udp_head;
    memset(&udp_head, 0, sizeof(udp_head));
    udp_head.msg_type = UDP_REQUEST_FILE;
    udp_head.flags = 0;
    udp_head.seq_num = 0;
    udp_head.ack_num = 0;
    udp_head.data_len = sizeof(udp_request_file_t);
    udp_head.window = WINDOW_SIZE;
    udp_head.checksum = 0;  // temporairily

    // building the payload for request file
    udp_request_file_t udp_req_file;
    udp_req_file.file_id = file_id;
    memcpy(udp_req_file.hash, rec_file.hash, sizeof(udp_req_file.hash));       // copying because it is an int array, not a pointer

    // creating a buffer to store the header and payload
    uint16_t total_len = sizeof(udp_header_t) + sizeof(udp_request_file_t);
    uint8_t buff [total_len];
    memcpy(buff, &udp_head, sizeof(udp_header_t));
    memcpy(buff + sizeof(udp_header_t), &udp_req_file, sizeof(udp_request_file_t));

    // calculating the checksum and putting that abck value into the buffer 
    uint16_t check_sum = calculate_checksum(buff, total_len);
    udp_header_t *buff_header = (udp_header_t*) buff;
    buff_header->checksum = check_sum;

    // sending datagram as one packet (header + payload)
    ssize_t udp_bytes_sent = 0;
    udp_bytes_sent = sendto(udp_sock, buff, total_len, 0, (struct sockaddr *) &peer, sizeof(peer));
    
    // fill in later
    if (udp_bytes_sent < 0){
        ERROR_PRINT("sendto UDP_REQUEST_FILE failed");
        return 1;
    }

    // fill in later
    if (udp_bytes_sent != total_len){
        ERROR_PRINT("Partial send of UDP_REQUEST_FILE (%zd of %u bytes)", udp_bytes_sent, total_len);
        return 1;
    }

    // now we need to read from the socket

    // variable declarations for receiving
    uint16_t max_udp_packet_size = sizeof(udp_header_t) + CHUNK_SIZE;
    uint8_t recv_buff[max_udp_packet_size];
    struct sockaddr_in sender_addr;
    socklen_t sender_len = sizeof(sender_addr);

    // receiving the length from the udp socket
    // ssize_t rec_len = recvfrom(udp_sock, recv_buff, max_udp_packet_size,0,(struct sockaddr*) &sender_addr, &sender_len);
    ssize_t rec_len = lossy_recv(lossy_link, recv_buff, max_udp_packet_size, 0, (struct sockaddr*)&sender_addr, &sender_len);

    // checks
    // fill in later
    if (rec_len < 0){
        ERROR_PRINT();
        return 1;
    }

    // fill in later
    if (rec_len < sizeof(udp_header_t)){
        ERROR_PRINT();
        return 1;
    }  

    // casting the received buffer as a udp header
    udp_header_t *rec_udp_header = (udp_header_t*) recv_buff;
    uint16_t data_len = ntohs(rec_udp_header->data_len);

    // checking
    if (rec_len < sizeof(udp_header_t) + data_len) {
        // header says there's N bytes of data, but packet is shorter
        ERROR_PRINT();
        return 1;
    }     

    uint8_t tmp_buf[max_udp_packet_size];
    memcpy(tmp_buf, recv_buff, rec_len);

    // Grab header inside the temp buffer
    udp_header_t *tmp_hdr = (udp_header_t *)tmp_buf;

    // Save the checksum from the packet, then zero the field
    uint16_t expected_checksum = tmp_hdr->checksum;
    
    tmp_hdr->checksum = 0;
    // Now verify: calculate_checksum(tmp_buf, rec_len) should match expected_checksum
    if (!verify_checksum(tmp_buf, rec_len, expected_checksum)) {
        ERROR_PRINT("UDP_FILE_START checksum mismatch");
        return 1;
    }

    // fill in later
    if (sender_addr.sin_addr.s_addr != rec_file.peer_ip || sender_addr.sin_port != rec_file.peer_port){
        ERROR_PRINT();
        return 1;

    }

    // fill in later
    if (rec_udp_header->msg_type != UDP_FILE_START){
        ERROR_PRINT();
        return 1;
    }

    // fill in later
    if (data_len != sizeof(udp_file_start_t)){
        ERROR_PRINT();
        return 1;
    }

    // write comment later
    udp_file_start_t* start = (udp_file_start_t*) (recv_buff + sizeof(udp_header_t));
    sr_receiver_t receiver;

    // need to print requesting info
    // INFO_PRINT("Requesting file_id=%u from server", file_id);
    // INFO_PRINT("Peer location: %s:%u", inet_ntoa(peer.sin_addr), ntohs(peer.sin_port));
    // vars and structs for selective repeat:
    if (sr_receiver_init(&receiver, udp_sock, &peer, start->file_size, start->num_chunks, lossy_link) != 0){
        ERROR_PRINT();
        sr_receiver_cleanup(&receiver);
        return 1;
    }

    // new vars for selective repeat
    struct sockaddr_in sender_addr_sr;
    socklen_t sender_len_sr = sizeof(sender_addr_sr);

    // INFO_PRINT("Starting file transfer: %s (%u bytes)", rec_file.filename, rec_file.file_size);
    // UDP selective repeat loop
    while(!sr_receiver_is_complete(&receiver)){


        // ssize_t rec_len_sr = recvfrom(udp_sock, recv_buff, max_udp_packet_size, 0, (struct sockaddr*)&sender_addr_sr, &sender_len_sr);
        ssize_t rec_len_sr = lossy_recv(lossy_link, recv_buff, max_udp_packet_size, 0, (struct sockaddr*)&sender_addr_sr, &sender_len_sr);

        // per packet checks

        // check if error from recvfrom()
        if (rec_len_sr < 0){
            ERROR_PRINT();
            sr_receiver_cleanup(&receiver);
            return -1;
        }

        // check sender IP
        if (sender_addr_sr.sin_addr.s_addr != peer.sin_addr.s_addr || sender_addr_sr.sin_port != peer.sin_port){
            continue;
        }

        if (sr_receiver_handle_data(&receiver, recv_buff, (size_t)rec_len_sr) < 0) {
            ERROR_PRINT("sr_receiver_handle_data reported an error");
            sr_receiver_cleanup(&receiver);
            return 1;
        }
    }

    // print file transfer complete
    INFO_PRINT("File transfer complete");

    // need to assemble the file via sr_receiver_get_file()
    uint32_t assembled_size = 0;
    const uint8_t* assembled_file = sr_receiver_get_file(&receiver, &assembled_size);

    // Compute SHA-256
    uint8_t computed_hash[32];
    compute_sha256(assembled_file, assembled_size, computed_hash);

    // converting the computed to string to print
    char computed_hash_str[65];
    hash_to_string(computed_hash,computed_hash_str);

    // compare file.
    if (memcmp(computed_hash, rec_file.hash, 32) == 0){

        // files are the same
        INFO_PRINT("Hash verified: %s", computed_hash_str);

        // need to save the file 

        // open the file (overwriets existing file if present)
        FILE* fp = fopen(rec_file.filename, "wb");

        // check for error
        if (fp == NULL){
            ERROR_PRINT();
            sr_receiver_cleanup(&receiver);
            return -1;
        }

        // write into the file
        fwrite(assembled_file, 1, assembled_size, fp);

        // close the file
        fclose(fp);

        // print success
        INFO_PRINT("Saved file: %s", rec_file.filename);
    }
    else{

        char rec_file_hash_str[65];
        hash_to_string(rec_file.hash, rec_file_hash_str);

        ERROR_PRINT("Hash mismatch!");
        ERROR_PRINT("  Expected: %s", rec_file_hash_str);
        ERROR_PRINT("  Received: %s", computed_hash_str);
        ERROR_PRINT("File may be corrupted");
    }

    // clean up the receiver
    sr_receiver_cleanup(&receiver);

    return 0;
}

void quit(int tcp_sock, int udp_sock) { 

    // print shutting down
    INFO_PRINT("Shutting down...");

    // creating the tcp message to server
    tcp_header_t tcp_send_header;
    tcp_send_header.msg_type = TCP_UNREGISTER;
    tcp_send_header.error_code = ERR_NONE;
    tcp_send_header.data_len = 0;

    // variables for send()
    size_t bytes_sent = 0;
    ssize_t n_sent = 0;

    // iterating until the entire message is sent
    while (bytes_sent < sizeof(tcp_header_t)){

        n_sent = send(tcp_sock, (uint8_t *) &(tcp_send_header) + bytes_sent, sizeof(tcp_send_header) - bytes_sent, 0);

        if (n_sent < 0){
            ERROR_PRINT("send failed");
            return;
        }
        else if (n_sent == 0){
            ERROR_PRINT("send failed");
            return;
        }

        bytes_sent += (size_t) n_sent;
    }

    // printing successful error
    INFO_PRINT("Unregistered from server");
    return;
}

void handle_command(char* line, int tcp_sock, int udp_sock, lossy_link_t *lossy_link){

    // getting the input until the new line
    line[strcspn(line, "\n")] = '\0';

    // all the cases for share, list, get, quit, and invalid
    if (strncmp(line, "share ", 6) == 0){
        
        // share() wants to be called

        // pointing to the filename now
        char* file_arg = line + 6;

        // call share()
        share(tcp_sock, file_arg);
    }
    else if (strcmp(line, "list") == 0){
        
        // call list()
        list(tcp_sock);
    }

    else if (strncmp(line, "get ", 4) == 0){

        // get() wants to be called

        //variables for getting file_id
        char* file_arg = line + 4;
        char* end_ptr = NULL;

        uint32_t file_id = strtoul(file_arg, &end_ptr, 10);

        if (end_ptr == file_arg){
            ERROR_PRINT("Invalid file ID");
            return;
        }

        if (*end_ptr != '\0') {
            ERROR_PRINT("Invalid file ID");
            return;
        }

        // call get()
        get(tcp_sock, udp_sock, file_id, lossy_link);

    }
    else if (strcmp(line, "quit") == 0){

        // call quit()
        quit(tcp_sock, udp_sock);
        running = 0;

    }
    else {
        // incorrect command, ask for valid command
        ERROR_PRINT("Incorrect command, ask for valid command");
    }
    return;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        ERROR_PRINT("Usage: %s <server_ip> <server_port> [udp_port] [loss_rate]", argv[0]);
        ERROR_PRINT("udp_port:   UDP port to bind (default: random)");
        ERROR_PRINT("  loss_rate:  Packet loss rate 0.0-1.0 (default: 0.0)");
        return 1;
    }

    // TODO: Implement client
    // You need to design and implement the entire client from scratch.
    // Refer to the assignment document and protocol.h for requirements.

    // configuring the udp port and creating a socket for udp
    uint16_t udp_port = (argc >= 4) ? (uint16_t)atoi(argv[3]): 0;

    // creating a socket and checking for failure
    int udp_fd = create_udp_socket();
    if (udp_fd < 0){
        ERROR_PRINT("UDP socket failed");
        return 1;
    }

    // checking the udp binding
    if (bind_socket(udp_port, udp_fd) < 0){
        ERROR_PRINT("There was an error binding the udp port to the socket");
        close(udp_fd);
        return 1;
    }

    // if udp_port is 0 figure out where it was assigned to 
    if (udp_port == 0){
        struct sockaddr_in addr;
        socklen_t addr_len = sizeof(addr);
        if (getsockname(udp_fd, (struct sockaddr*)&addr, &addr_len) == -1){
            close(udp_fd);
            ERROR_PRINT("There was an error in getsockname()");
            return 1;
        }
        udp_port = ntohs(addr.sin_port);
        INFO_PRINT("Bound the UDP socket to port %u", udp_port);
    }

    // configuring the loss rate
    float loss_rate = (argc >= 5) ? atof(argv[4]) : 0.0f;
    lossy_link_t lossy_link;
    lossy_init(&lossy_link, udp_fd, loss_rate, time(NULL));

    // TCP connect to directory server
    const char *server_ip = argv[1];
    uint16_t server_port = (uint16_t)atoi(argv[2]);

    // creating the tcp socket and checking it
    int tcp_fd = create_tcp_socket();
    if (tcp_fd < 0) {
        ERROR_PRINT("TCP socket failed");
        close(udp_fd);
        return 1;
    }

    // creating the IPv4 Socket
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);

    // 
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) != 1) {
        ERROR_PRINT("ip address isn't valid: %s", server_ip);
        close(udp_fd);
        close(tcp_fd);
        return 1;
    }

    //
    if (connect(tcp_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ERROR_PRINT("couldn't connect to dir server: %s", strerror(errno));
        close(tcp_fd);
        close(udp_fd);
        return 1;
    }

    INFO_PRINT("Conntected to directory server at %s:%u", server_ip, server_port);

    // Send TCP_REGISTER
    tcp_header_t tcp_header;
    tcp_header.data_len = sizeof(tcp_register_t);
    tcp_header.error_code = ERR_NONE;
    tcp_header.msg_type = TCP_REGISTER;

    size_t bytes_header = 0;
    ssize_t n_bytes_header = 0;

    while (bytes_header < sizeof(tcp_header_t)){

        n_bytes_header = send(tcp_fd, (uint8_t*) &(tcp_header) + bytes_header, sizeof(tcp_header) - bytes_header, 0);

        if (n_bytes_header < 0){
            close(tcp_fd);
            close(udp_fd);
            ERROR_PRINT("There was an error in sending bytes for register header");
            return 1;
        }
        else if (n_bytes_header == 0){

            ERROR_PRINT("There was an error in the connection on the server side");
            return 1;
        }

        bytes_header += (size_t) n_bytes_header;
    }

    tcp_register_t tcp_register;
    tcp_register.udp_port = udp_port;

    size_t bytes_register = 0;
    ssize_t n_bytes_register = 0;

    while (bytes_register < sizeof(tcp_register)){

        n_bytes_register = send(tcp_fd, (uint8_t*) &(tcp_register) + bytes_register, sizeof(tcp_register) - bytes_register, 0);

        if (n_bytes_register < 0){
            close(tcp_fd);
            close(udp_fd);
            ERROR_PRINT("There was an error in the connection on the server side");
            return 1;
        }

        else if (n_bytes_register == 0){

            ERROR_PRINT("There was an error in the connection on the server side");
            return 1;
        }

        bytes_register += n_bytes_register;
    }

    // Receive TCP_REGISTER_ACK header
    tcp_header_t rec_header;

    size_t bytes_rec_header = 0;
    ssize_t n_bytes_rec = 0;

    while (bytes_rec_header < sizeof(tcp_header_t)){

        n_bytes_rec = recv(tcp_fd, (uint8_t *) &(rec_header) + bytes_rec_header, sizeof(rec_header) - bytes_rec_header, 0);

        if (n_bytes_rec < 0){
            close(tcp_fd);
            close(udp_fd);
            ERROR_PRINT("There was an error in the connection on the server side");
            return 1;
        }

        else if (n_bytes_rec == 0){

            ERROR_PRINT("There was an error in the connection on the server side");
            return 1;
        }

        bytes_rec_header += (size_t) n_bytes_rec;
    }

    if (rec_header.msg_type == TCP_ERROR){
        ERROR_PRINT("The error code for the rec header in main is %u", rec_header.error_code);
        close(tcp_fd);
        close(udp_fd);
        return 1;
    }

    if (rec_header.msg_type != TCP_REGISTER_ACK){
        ERROR_PRINT("The message received for the rec header in main is %u", rec_header.msg_type);
        close(tcp_fd);
        close(udp_fd);
        return 1;
    }

    if (rec_header.data_len != sizeof(tcp_register_ack_t)){

        ERROR_PRINT("There is a body size mismatch in the received header");
        close(tcp_fd);
        close(udp_fd);
        return 1;
    }

    tcp_register_ack_t tcp_reg_ack;
    
    size_t bytes_reg_ack = 0;
    ssize_t n_bytes_reg_ack = 0;

    while (bytes_reg_ack < sizeof(tcp_register_ack_t)){

        n_bytes_reg_ack = recv(tcp_fd, (uint8_t*) &(tcp_reg_ack) + bytes_reg_ack, sizeof(tcp_reg_ack) - bytes_reg_ack, 0);

        if (n_bytes_reg_ack < 0){

            ERROR_PRINT("There is an error from the server in main for tcp_reg_ack ");
            close(tcp_fd);
            close(udp_fd);
            return 1;
        }
        else if (n_bytes_reg_ack == 0){

            ERROR_PRINT("here was an error in the connection on the server side for tcp_reg_ack");
            return 1;
        }

        bytes_reg_ack += (size_t) n_bytes_reg_ack;
    }

    // here is the client id
    uint32_t client_id = tcp_reg_ack.client_id;
    INFO_PRINT("Connected to %u", client_id);

    // implementing the user commands (share, list, get, quit)

    // var delcarations for polling
    struct pollfd fds[2];
    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;
    fds[1].fd = udp_fd;
    fds[1].events = POLLIN;

    printf("> ");
    fflush(stdout);

    while (running) {

        // polling
        int poll_res = poll(fds, 2, 100);  // 100ms timeout

        // error checking for poll
        if (poll_res < 0){
            ERROR_PRINT("There was an error in calling poll");
            break;
        }

        // parsing the input 
        if (fds[0].revents & POLLIN) {
            char line[1024];
            if (fgets(line, sizeof(line), stdin)) {
                // Parse and handle command
                handle_command(line, tcp_fd, udp_fd, &lossy_link);
                printf("> ");
                fflush(stdout);
            }
        }

        // ... handle UDP events ...
        // if there is a packet
        if (fds[1].revents & POLLIN) {

            size_t udp_len = CHUNK_SIZE + sizeof(udp_header_t);
            uint8_t udp_buffer[udp_len];

            struct sockaddr_in peer_addr;
            socklen_t addr_len = sizeof(struct sockaddr_in);

            ssize_t lossy_return = lossy_recv(&lossy_link, udp_buffer, udp_len, 0, (struct sockaddr*) &peer_addr, &addr_len);

            if (lossy_return < 0){

                ERROR_PRINT("There was an error reading from lossy_recv");
                continue;            
            }
            else if (lossy_return == 0){

                // change the message
                ERROR_PRINT("If the peer has performed an orderly shutdown");
                continue;
            }

            if (lossy_return < (ssize_t) sizeof(udp_header_t)){
                ERROR_PRINT("There is a size mismatch");
                continue;
            }

            // cast buffer to udp_header to check message types
            udp_header_t* udp_header = (udp_header_t *) udp_buffer;

            // 1) if true, I am the owner of the file
            if (udp_header->msg_type == UDP_REQUEST_FILE){

                // validate the packet length
                if (lossy_return < (ssize_t) (sizeof(udp_header_t) + sizeof(udp_request_file_t))){

                    ERROR_PRINT("There is a size mismatch for the lossy return");
                    continue;
                }

                uint16_t req_len = ntohs(udp_header->data_len);
                if (req_len != sizeof(udp_request_file_t)){
                    ERROR_PRINT("UDP_REQUEST_FILE data_len mismatch (got %u, expected %zu)", req_len, sizeof(udp_request_file_t));                    continue;
                }

                // parse the body
                udp_request_file_t* udp_request_file = (udp_request_file_t *)(udp_buffer + sizeof(udp_header_t));

                // find the file by id and checking if it exists
                shared_file_t *file = find_shared_file_by_id(udp_request_file->file_id);
                if (file == NULL){
                    ERROR_PRINT("Could not find the file: %u", udp_request_file->file_id);
                    continue;
                }
                
                // verifying the hash
                if (memcmp(file->hash, udp_request_file->hash, 32) != 0) {
                    ERROR_PRINT("Hash mismatch for file_id=%u, refusing transfer", udp_request_file->file_id);
                    continue;
                }

                // opening the file and checking the return type
                FILE* fp = fopen(file->file_name, "rb");
                if (fp == NULL){
                    ERROR_PRINT("There was an error opening the file on disk");
                    continue;
                }

                // getting the actual size of the file
                fseek(fp, 0, SEEK_END);
                long actual_size = ftell(fp);
                fseek(fp, 0, SEEK_SET);

                // checking to see if the sizes match
                if (actual_size != file->file_size){
                    ERROR_PRINT("There was an a mismatch on copying the file size on disk");
                    fclose(fp);
                    continue;
                }

                // need to figure out the number of chunks
                int num_chunks = (actual_size + CHUNK_SIZE - 1) / CHUNK_SIZE;

                // building a UDP header
                udp_header_t udp_header_start;
                memset(&udp_header_start, 0, sizeof(udp_header_start));
                udp_header_start.msg_type = UDP_FILE_START;
                // udp_header_start.flags = 0;
                // udp_header_start.seq_num = 0;
                // udp_header_start.ack_num = 0;
                udp_header_start.data_len = htons(sizeof(udp_file_start_t));
                udp_header_start.window = WINDOW_SIZE;
                udp_header_start.checksum = 0;

                // building a UDP_FILE_START packet 
                udp_file_start_t udp_file_start;
                udp_file_start.file_size = (uint32_t) actual_size;
                udp_file_start.num_chunks = num_chunks;

                // storing the payload in the buffer
                uint16_t start_payload_size = sizeof(udp_header_t) + sizeof(udp_file_start_t);
                uint8_t buff[start_payload_size];

                // copy the header
                memcpy(buff, &udp_header_start, sizeof(udp_header_t));

                // copy the body after the buffer
                memcpy(buff + sizeof(udp_header_t), &udp_file_start, sizeof(udp_file_start_t));

                // calculating checksum and storing in buffer pointer
                uint16_t check_sum = calculate_checksum(buff, start_payload_size);
                udp_header_t *buff_header = (udp_header_t *) buff;
                buff_header->checksum = check_sum;

                ssize_t n_sent_udp_start_file = 0;
                n_sent_udp_start_file = sendto(udp_fd, buff, start_payload_size, 0, (struct sockaddr *)&peer_addr, addr_len);

                // check errors
                if (n_sent_udp_start_file < 0){
                    ERROR_PRINT("sendto UDP_FILE_START failed");
                    continue;               
                }
                if (n_sent_udp_start_file == 0){
                    ERROR_PRINT("the connection timed out");
                    continue;
                }

                // loading the file after request and checking return type of malloc
                uint8_t *file_buffer = malloc(actual_size);
                if (file_buffer == NULL){
                    ERROR_PRINT("There was an error in malloc");
                    fclose(fp);
                    continue;
                }

                // reading from the fp and checking return type
                size_t n_read = fread(file_buffer, 1, actual_size, fp);

                // closing the file pointer
                fclose(fp);

                // check to see if there is a size mismatch
                if (n_read != (size_t) actual_size){
                    ERROR_PRINT("There was an error in fread");
                    free(file_buffer);
                    continue;
                }

                // global sr state
                int sr_init_res = sr_sender_init(&global_sender, udp_fd, &peer_addr, file_buffer,(uint32_t) actual_size, &lossy_link);

                if (sr_init_res < 0){
                    ERROR_PRINT("There was an error initializing sr for sender");
                    free(file_buffer);
                    continue;
                }

                global_file_buffer = file_buffer;
                transfer_active = 1;

                sr_sender_send_window(&global_sender);
            }
            else if (udp_header->msg_type == UDP_ACK){

                // if we are currently transferring
                if (transfer_active){          
                    sr_sender_handle_ack(&global_sender, udp_header);
                }
            }
            // else if (udp_header->msg_type == UDP_FILE_START){

            //     // validating packet length
            //     if (lossy_return < (ssize_t)(sizeof(udp_header_t) + sizeof(udp_file_start_t))) {
            //         ERROR_PRINT("UDP_FILE_START packet too short");
            //         continue;
            //     }

            //     // 
            //     uint16_t udp_header_data_len = ntohs(udp_header->data_len);
            //     if (udp_header_data_len != sizeof(udp_file_start_t)) {
            //         ERROR_PRINT("UDP_FILE_START data_len mismatch (got %u, expected %zu)", udp_header_data_len, sizeof(udp_file_start_t));
            //         continue;
            //     }

            //     // Parse the udp_file_start_t body.
            //     udp_file_start_t *file_start = (udp_file_start_t *) (udp_buffer + sizeof(udp_header_t));
                
            //     // initializing receiver and checking return type
            //     int res_receiver_init = sr_receiver_init(&global_receiver,udp_fd, &peer_addr, file_start->file_size, file_start->num_chunks, &lossy_link);
            //     if (res_receiver_init < 0){
            //         ERROR_PRINT("There was an error initializing sr for receiver");
            //         continue;
            //     }

            //     // set the flag to receive data
            //     receive_active = 1;
            // }
            // else if (udp_header->msg_type == UDP_DATA){

            //     if (receive_active){

            //         int receiver_handle_res = sr_receiver_handle_data(&global_receiver, udp_buffer, (size_t) lossy_return);

            //         // if there as an error, cleanup
            //         if (receiver_handle_res < 0){
            //             ERROR_PRINT("sr_receiver_handle_data reported an error");
            //             sr_receiver_cleanup(&global_receiver);
            //             receive_active = 0;
            //             continue;
            //         }
            //     }
            // }
        }

        // for selective repeat
        if (transfer_active){

            // send window
            sr_sender_send_window(&global_sender);

            // check for timeouts
            uint64_t curr_ms = get_time_ms();
            int res = sr_sender_check_timeouts(&global_sender, curr_ms);
            if (res < 0){
                ERROR_PRINT("sr_sender_check_timeouts exceeded max retries, aborting transfer");
                free(global_file_buffer);
                global_file_buffer = NULL;
                sr_sender_cleanup(&global_sender);
                transfer_active = 0;
            }
            // check to see if we are done transferring
            else if (sr_sender_is_complete(&global_sender)){
                INFO_PRINT("File is done transferring");
                free(global_file_buffer);
                global_file_buffer = NULL;
                sr_sender_cleanup(&global_sender);
                transfer_active = 0;
            }  
        }

        // if (receive_active){

        //     // if the receiver is done getting the file
        //     if (sr_receiver_is_complete(&global_receiver)) {

        //         // var declarations
        //         uint32_t file_size = 0;
        //         const uint8_t *file_buf = sr_receiver_get_file(&global_receiver, &file_size);

        //         // save to disk
        //         if (write_buffer_to_file(receive_filename, file_buf, file_size) == 0) {
        //             INFO_PRINT("Download complete: saved to %s", receive_filename);
        //         } 
        //         else {
        //             ERROR_PRINT("Failed to save file to %s", receive_filename);
        //         }

        //         // free SR receiver resources
        //         sr_receiver_cleanup(&global_receiver);
        //         receive_active = 0;
        //     }  
        // }
    }

    // do I close the sockets here or in quit()?
    close(tcp_fd);
    close(udp_fd);
    return 0;
}
