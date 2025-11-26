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


    // Compute SHA-256 hash of the file contents
    // computing the SHA256
    uint8_t hash[32];
    compute_sha256(buff, size, hash);

    // free the buffer from read_file_to_buffer()
    free(buff);
    buff = NULL;

    // Send `TCP_SHARE_FILE` message to server with filename, size, and hash
    // copying the fields into the tcp_share_file_t struct
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

    // for the head
    ssize_t bytes_sent_head = 0;
    ssize_t n_head = 0;
    while (bytes_sent_head < (ssize_t)sizeof(tcp_head)){

        n_head = send(tcp_sock, (uint8_t *) &(tcp_head) + bytes_sent_head, sizeof(tcp_head) - bytes_sent_head, 0);
        if (n_head < 0){
            ERROR_PRINT("send failed");
            free(buff);
            buff = NULL;
            return 1;
        }
        else if (n_head == 0){
            ERROR_PRINT("Connection closed by peer");
            free(buff);
            buff = NULL;
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
            free(buff);
            buff = NULL;
            return 1;
        }
        else if (n_message == 0){
            ERROR_PRINT("Connection closed by peer");
            free(buff);
            buff = NULL;
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
            free(buff);
            buff = NULL;
            return 1;
        }
        else if (n_read == 0){
            ERROR_PRINT("Connection closed by peer");
            free(buff);
            buff = NULL;
            return 1;
        }

        bytes_read_message += n_read;
    }
    
    // safety checks
    if (response_header.msg_type == TCP_ERROR){

        ERROR_PRINT("There was an error: %u", response_header.error_code);
        free(buff);
        buff = NULL;
        return 1;
    }

    // if (response_header.msg_type != TCP_SHARE_ACK){

    //     ERROR_PRINT("TCP msg type is not set to SHARE_ACK and this is the error code:");
    //     free(buff);    
    //     return 1;
    // }

    if (response_header.data_len != sizeof(tcp_share_ack_t)){

        ERROR_PRINT("Expected %zu bytes but got %u", sizeof(tcp_share_ack_t), response_header.data_len);
        free(buff);
        buff = NULL;
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
            free(buff);
            buff = NULL;
            return 1;
        }
        else if (n_share == 0){
            ERROR_PRINT("Connection closed by peer");
            free(buff);
            buff = NULL;
            return 1;
        }

        bytes_read_share += n_share;
    }

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

    // creating the tcp message to server
    tcp_header_t tcp_send_header;
    tcp_send_header.msg_type = TCP_REQUEST_FILE;
    tcp_send_header.error_code = ERR_NONE;
    tcp_send_header.data_len = sizeof(tcp_request_file_t);

    ssize_t bytes_sent = 0;
    ssize_t n_sent = 0;

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

    tcp_request_file_t req_file;
    req_file.file_id = file_id;
    
    ssize_t bytes_send_req = 0;
    ssize_t n_send_req = 0;

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

    ssize_t bytes_read = 0;
    ssize_t n_read = 0;

    tcp_header_t tcp_receive_header;

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

    // check header
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

    // need to get the info for location of file (tcp_file_location_t)
    tcp_file_location_t rec_file;
    ssize_t bytes_read_file = 0;
    ssize_t n_read_file = 0;

    while (bytes_read_file < sizeof(tcp_file_location_t)){

        n_read_file = recv(tcp_sock, (uint8_t*) &(rec_file) + bytes_read_file, sizeof(tcp_file_location_t) - bytes_read_file, 0);
        if (n_read_file < 0){

            ERROR_PRINT();
            return 1;

        }
        else if (n_read_file == 0){
            ERROR_PRINT();
            return 1;
        }

        bytes_read_file +=n_read_file;
    }

    // fill in later
    if (file_id != rec_file.file_id){
        ERROR_PRINT();
        return 1;
    }

    // need to do UDP peer to peer now
    struct sockaddr_in peer;
    peer.sin_port = rec_file.peer_port;
    peer.sin_addr.s_addr = rec_file.peer_ip;
    peer.sin_family = AF_INET;
    
    // building UDP header
    udp_header_t udp_head;
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
        ERROR_PRINT();
        return 1;
    }

    // fill in later
    if (udp_bytes_sent != total_len){
        ERROR_PRINT();
        return 1;
    }

    // now we need to read from the socket

    // variable declarations for receiving
    uint16_t max_udp_packet_size = sizeof(udp_header_t) + CHUNK_SIZE;
    uint8_t recv_buff[max_udp_packet_size];
    struct sockaddr_in sender_addr;
    socklen_t sender_len = sizeof(sender_addr);

    // receiving the length from the udp socket
    ssize_t rec_len = recvfrom(udp_sock, recv_buff, max_udp_packet_size,0,(struct sockaddr*) &sender_addr, &sender_len);

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

    if (rec_len < sizeof(udp_header_t) + rec_udp_header->data_len) {
        // header says there's N bytes of data, but packet is shorter
        ERROR_PRINT();
        return 1;
    }     

    // fill in later
    int ver_checksum = verify_checksum(recv_buff, rec_len, rec_udp_header->checksum);
    if (ver_checksum == 0){
        ERROR_PRINT();
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
    if (rec_udp_header->data_len != sizeof(udp_file_start_t)){
        ERROR_PRINT();
        return 1;
    }

    // write comment later
    udp_file_start_t* start = (udp_file_start_t*) (recv_buff + sizeof(udp_header_t));
    sr_receiver_t receiver;

    // need to print requesting info
    INFO_PRINT("Requesting file_id=%u from server", file_id);
    INFO_PRINT("Peer location: %s:%u", inet_ntoa(peer.sin_addr), ntohs(peer.sin_port));
    // vars and structs for selective repeat:
    if (sr_receiver_init(&receiver, udp_sock, &peer, start->file_size, start->num_chunks, lossy_link) != 0){
        ERROR_PRINT();
        sr_receiver_cleanup(&receiver);
        return 1;
    }

    // new vars for selective repeat
    struct sockaddr_in sender_addr_sr;
    socklen_t sender_len_sr = sizeof(sender_addr_sr);

    INFO_PRINT("Starting file transfer: %s (%u bytes)", rec_file.filename, rec_file.file_size);
    // UDP selective repeat loop
    while(!sr_receiver_is_complete(&receiver)){


        ssize_t rec_len_sr = recvfrom(udp_sock, recv_buff, max_udp_packet_size, 0, (struct sockaddr*)&sender_addr_sr, &sender_len_sr);

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

        sr_receiver_handle_data(&receiver, recv_buff, rec_len_sr);
    }

    // print file
    INFO_PRINT("File transfer complete");

    // need to assemble the file via sr_receiver_get_file()
    uint32_t assembled_size = 0;
    uint8_t* assembled_file = sr_receiver_get_file(&receiver, &assembled_size);

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

    return 0;

}

void quit(int tcp_sock, int udp_sock) { 



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
    }

    // if udp_port is 0 figure out where it was assigned to 
    if (udp_port == 0){
        struct sockaddr_in addr;
        socklen_t addr_len = sizeof(addr);
        getsockname(udp_fd, (struct sockaddr*)&addr, &addr_len);
        udp_port = ntohs(addr.sin_port);
        ERROR_PRINT("Bound the UDP socket to port %u", udp_port);
    }


    // configuring the loss rate
    float loss_rate = (argc >= 5) ? atof(argv[4]) : 0.0f;

    lossy_link_t lossy_link;
    lossy_init(&lossy_link, udp_fd, loss_rate, time(NULL));


    // code for getting the tcp port
    const char *server_ip = argv[1];
    uint16_t server_port = (uint16_t)atoi(argv[2]);

    int tcp_fd = create_tcp_socket();
    if (tcp_fd < 0) {
        ERROR_PRINT("TCP socket failed");
        return 1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);

    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) != 1) {
        ERROR_PRINT("ip address isn't valid: %s", server_ip);
        return 1;
    }

    if (connect(tcp_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ERROR_PRINT("couldn't connect to dir server: %s", strerror(errno));
        close(tcp_fd);
        return 1;
    }

    INFO_PRINT("conntect do dir server at %s:%u", server_ip, server_port);

    // implementing the user commands (share, list, get, quit)

    

    return 0;
}
