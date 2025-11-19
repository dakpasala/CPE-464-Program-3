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

int get(int sock, const char *filename) { 



}

void quit(int sock) { 



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
