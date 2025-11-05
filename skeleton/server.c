#include "protocol.h"
#include "common.h"
#include <signal.h>
#include <poll.h>

#define MAX_CLIENTS 256
#define MAX_FILES 1024

/* Client information */
typedef struct {
    int fd;                     // TCP socket
    uint32_t client_id;
    struct sockaddr_in addr;    // Client's TCP address
    uint16_t udp_port;          // Client's UDP port for P2P
    int active;                 // 1 if client is registered
} client_t;

/* Shared file information */
typedef struct {
    uint32_t file_id;
    uint32_t file_size;
    uint8_t hash[32];
    char filename[256];
    uint32_t owner_id;          // Client ID that shared this file
} shared_file_t;

/* Global state */
static client_t clients[MAX_CLIENTS];
static shared_file_t files[MAX_FILES];
static uint32_t next_client_id = 1;
static uint32_t next_file_id = 1;
static int num_clients = 0;
static int num_files = 0;
static volatile int running = 1;

/* Signal handler for clean shutdown */
void sigint_handler(int sig) {
    (void)sig;
    running = 0;
}

/* Find client by ID */
client_t* find_client(uint32_t client_id) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].client_id == client_id) {
            return &clients[i];
        }
    }
    return NULL;
}

/* Find client by socket fd */
client_t* find_client_by_fd(int fd) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd == fd && clients[i].fd > 0) {
            return &clients[i];
        }
    }
    return NULL;
}

/* Find file by ID */
shared_file_t* find_file(uint32_t file_id) {
    for (int i = 0; i < num_files; i++) {
        if (files[i].file_id == file_id) {
            return &files[i];
        }
    }
    return NULL;
}

/* Remove client */
void remove_client(client_t *client) {
    if (!client || !client->active) return;

    INFO_PRINT("Client %u disconnected", client->client_id);

    // Remove all files shared by this client
    int files_removed = 0;
    for (int i = 0; i < num_files; i++) {
        if (files[i].owner_id == client->client_id) {
            INFO_PRINT("Removing file '%s' (ID: %u) from directory",
                       files[i].filename, files[i].file_id);
            // Shift remaining files down
            for (int j = i; j < num_files - 1; j++) {
                files[j] = files[j + 1];
            }
            num_files--;
            files_removed++;
            i--;  // Check this index again
        }
    }

    if (files_removed > 0) {
        INFO_PRINT("Removed %d file(s) from client %u", files_removed, client->client_id);
    }

    close(client->fd);
    client->fd = -1;
    client->active = 0;
    num_clients--;
}

/* Send TCP response */
int send_response(client_t *client, const void *data, size_t len) {
    ssize_t sent = send(client->fd, data, len, 0);
    if (sent < 0) {
        ERROR_PRINT("Failed to send to client %u: %s", client->client_id, strerror(errno));
        return -1;
    }
    if ((size_t)sent != len) {
        ERROR_PRINT("Partial send to client %u: %zd of %zu bytes", client->client_id, sent, len);
        return -1;
    }
    return 0;
}

/* Handle TCP_REGISTER */
void handle_register(client_t *client, const tcp_register_t *reg) {
    client->udp_port = ntohs(reg->udp_port);
    client->client_id = next_client_id++;
    client->active = 1;
    num_clients++;

    INFO_PRINT("Client %u registered (UDP port: %u)", client->client_id, client->udp_port);

    // Send acknowledgment
    uint8_t response[sizeof(tcp_header_t) + sizeof(tcp_register_ack_t)];
    tcp_header_t *hdr = (tcp_header_t *)response;
    tcp_register_ack_t *ack = (tcp_register_ack_t *)(response + sizeof(tcp_header_t));

    hdr->msg_type = TCP_REGISTER_ACK;
    hdr->error_code = ERR_NONE;
    hdr->data_len = htons(sizeof(tcp_register_ack_t));
    ack->client_id = htonl(client->client_id);

    send_response(client, response, sizeof(response));
}

/* Handle TCP_SHARE_FILE */
void handle_share_file(client_t *client, const tcp_share_file_t *share) {
    if (num_files >= MAX_FILES) {
        ERROR_PRINT("Maximum number of files reached");

        uint8_t response[sizeof(tcp_header_t)];
        tcp_header_t *hdr = (tcp_header_t *)response;
        hdr->msg_type = TCP_ERROR;
        hdr->error_code = ERR_INVALID_MSG;
        hdr->data_len = 0;
        send_response(client, response, sizeof(response));
        return;
    }

    // Add file to registry
    shared_file_t *file = &files[num_files++];
    file->file_id = next_file_id++;
    file->file_size = ntohl(share->file_size);
    memcpy(file->hash, share->hash, 32);
    strncpy(file->filename, share->filename, sizeof(file->filename) - 1);
    file->filename[sizeof(file->filename) - 1] = '\0';
    file->owner_id = client->client_id;

    char hash_str[65];
    hash_to_string(file->hash, hash_str);
    INFO_PRINT("Client %u shared file '%s' (ID: %u, size: %u, hash: %.16s...)",
               client->client_id, file->filename, file->file_id, file->file_size, hash_str);

    // Send acknowledgment
    uint8_t response[sizeof(tcp_header_t) + sizeof(tcp_share_ack_t)];
    tcp_header_t *hdr = (tcp_header_t *)response;
    tcp_share_ack_t *ack = (tcp_share_ack_t *)(response + sizeof(tcp_header_t));

    hdr->msg_type = TCP_SHARE_ACK;
    hdr->error_code = ERR_NONE;
    hdr->data_len = htons(sizeof(tcp_share_ack_t));
    ack->file_id = htonl(file->file_id);

    send_response(client, response, sizeof(response));
}

/* Handle TCP_LIST_FILES */
void handle_list_files(client_t *client) {
    INFO_PRINT("Client %u requested file list (%d files available)",
               client->client_id, num_files);

    size_t response_size = sizeof(tcp_header_t) + num_files * sizeof(file_info_t);
    uint8_t *response = malloc(response_size);
    if (!response) {
        ERROR_PRINT("Failed to allocate response buffer");
        return;
    }

    tcp_header_t *hdr = (tcp_header_t *)response;
    hdr->msg_type = TCP_FILE_LIST;
    hdr->error_code = ERR_NONE;
    hdr->data_len = htons(num_files * sizeof(file_info_t));

    file_info_t *infos = (file_info_t *)(response + sizeof(tcp_header_t));
    for (int i = 0; i < num_files; i++) {
        infos[i].file_id = htonl(files[i].file_id);
        infos[i].file_size = htonl(files[i].file_size);
        memcpy(infos[i].hash, files[i].hash, 32);
        infos[i].num_peers = 1;  // For now, each file has one peer
        strncpy(infos[i].filename, files[i].filename, sizeof(infos[i].filename) - 1);
        infos[i].filename[sizeof(infos[i].filename) - 1] = '\0';
    }

    send_response(client, response, response_size);
    free(response);
}

/* Handle TCP_REQUEST_FILE */
void handle_request_file(client_t *client, const tcp_request_file_t *req) {
    uint32_t file_id = ntohl(req->file_id);
    shared_file_t *file = find_file(file_id);

    if (!file) {
        ERROR_PRINT("Client %u requested unknown file ID %u", client->client_id, file_id);

        uint8_t response[sizeof(tcp_header_t)];
        tcp_header_t *hdr = (tcp_header_t *)response;
        hdr->msg_type = TCP_ERROR;
        hdr->error_code = ERR_FILE_NOT_FOUND;
        hdr->data_len = 0;
        send_response(client, response, sizeof(response));
        return;
    }

    // Find the peer that has this file
    client_t *owner = find_client(file->owner_id);
    if (!owner) {
        ERROR_PRINT("File owner (client %u) not found", file->owner_id);

        uint8_t response[sizeof(tcp_header_t)];
        tcp_header_t *hdr = (tcp_header_t *)response;
        hdr->msg_type = TCP_ERROR;
        hdr->error_code = ERR_NO_PEERS;
        hdr->data_len = 0;
        send_response(client, response, sizeof(response));
        return;
    }

    INFO_PRINT("Client %u requesting file '%s' from client %u",
               client->client_id, file->filename, owner->client_id);

    // Send file location
    uint8_t response[sizeof(tcp_header_t) + sizeof(tcp_file_location_t)];
    tcp_header_t *hdr = (tcp_header_t *)response;
    tcp_file_location_t *loc = (tcp_file_location_t *)(response + sizeof(tcp_header_t));

    hdr->msg_type = TCP_FILE_LOCATION;
    hdr->error_code = ERR_NONE;
    hdr->data_len = htons(sizeof(tcp_file_location_t));

    loc->file_id = htonl(file->file_id);
    loc->file_size = htonl(file->file_size);
    memcpy(loc->hash, file->hash, 32);
    loc->peer_ip = owner->addr.sin_addr.s_addr;  // Already in network byte order
    loc->peer_port = htons(owner->udp_port);
    strncpy(loc->filename, file->filename, sizeof(loc->filename) - 1);
    loc->filename[sizeof(loc->filename) - 1] = '\0';

    send_response(client, response, sizeof(response));
}

/* Handle TCP_UNREGISTER */
void handle_unregister(client_t *client) {
    INFO_PRINT("Client %u unregistering", client->client_id);
    remove_client(client);
}

/* Process client message */
void process_client_message(client_t *client, const uint8_t *data, size_t len) {
    if (len < sizeof(tcp_header_t)) {
        ERROR_PRINT("Message too short from client");
        return;
    }

    const tcp_header_t *hdr = (const tcp_header_t *)data;
    uint16_t data_len = ntohs(hdr->data_len);

    if (sizeof(tcp_header_t) + data_len != len) {
        ERROR_PRINT("Message length mismatch: expected %zu, got %zu",
                    sizeof(tcp_header_t) + data_len, len);
        return;
    }

    const uint8_t *payload = data + sizeof(tcp_header_t);

    switch (hdr->msg_type) {
        case TCP_REGISTER:
            if (data_len == sizeof(tcp_register_t)) {
                handle_register(client, (const tcp_register_t *)payload);
            }
            break;

        case TCP_SHARE_FILE:
            if (data_len == sizeof(tcp_share_file_t)) {
                handle_share_file(client, (const tcp_share_file_t *)payload);
            }
            break;

        case TCP_LIST_FILES:
            handle_list_files(client);
            break;

        case TCP_REQUEST_FILE:
            if (data_len == sizeof(tcp_request_file_t)) {
                handle_request_file(client, (const tcp_request_file_t *)payload);
            }
            break;

        case TCP_UNREGISTER:
            handle_unregister(client);
            break;

        default:
            ERROR_PRINT("Unknown message type %u from client", hdr->msg_type);
            break;
    }
}

/* Accept new client connection */
void accept_new_client(int listen_fd) {
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &addr_len);

    if (client_fd < 0) {
        ERROR_PRINT("Failed to accept connection: %s", strerror(errno));
        return;
    }

    // Find empty slot
    client_t *client = NULL;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i].active) {
            client = &clients[i];
            break;
        }
    }

    if (!client) {
        ERROR_PRINT("Maximum number of clients reached");
        close(client_fd);
        return;
    }

    client->fd = client_fd;
    client->addr = client_addr;
    client->active = 0;  // Not registered yet

    INFO_PRINT("New connection from %s:%d",
               inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
}

/* Main server loop */
int main(int argc, char *argv[]) {
    if (argc > 2) {
        fprintf(stderr, "Usage: %s [port]\n", argv[0]);
        fprintf(stderr, "  If port is omitted, a random available port will be used.\n");
        return 1;
    }

    uint16_t port = 0;  // 0 means let OS choose a random port
    if (argc == 2) {
        port = (uint16_t)atoi(argv[1]);
    }

    // Initialize
    memset(clients, 0, sizeof(clients));
    memset(files, 0, sizeof(files));

    signal(SIGINT, sigint_handler);
    signal(SIGPIPE, SIG_IGN);

    // Create and bind TCP socket
    int listen_fd = create_tcp_socket();
    if (listen_fd < 0) {
        return 1;
    }

    if (bind_socket(listen_fd, port) < 0) {
        close(listen_fd);
        return 1;
    }

    // Get the actual port if we bound to port 0
    if (port == 0) {
        struct sockaddr_in addr;
        socklen_t addr_len = sizeof(addr);
        if (getsockname(listen_fd, (struct sockaddr *)&addr, &addr_len) == 0) {
            port = ntohs(addr.sin_port);
        } else {
            ERROR_PRINT("Failed to get socket name: %s", strerror(errno));
            close(listen_fd);
            return 1;
        }
    }

    if (listen(listen_fd, 10) < 0) {
        ERROR_PRINT("Failed to listen: %s", strerror(errno));
        close(listen_fd);
        return 1;
    }

    INFO_PRINT("Directory server started on port %u", port);

    // Main event loop
    while (running) {
        // Build poll array: listen socket + active client sockets
        struct pollfd poll_fds[MAX_CLIENTS + 1];
        int poll_count = 0;

        // Add listen socket
        poll_fds[poll_count].fd = listen_fd;
        poll_fds[poll_count].events = POLLIN;
        poll_count++;

        // Add client sockets
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].fd > 0) {
                poll_fds[poll_count].fd = clients[i].fd;
                poll_fds[poll_count].events = POLLIN;
                poll_count++;
            }
        }

        // Poll with 1 second timeout
        int ready = poll(poll_fds, poll_count, 1000);

        if (ready < 0) {
            if (errno == EINTR) continue;
            ERROR_PRINT("poll failed: %s", strerror(errno));
            break;
        }

        if (ready == 0) continue;

        // Check for new connections on listen socket
        if (poll_fds[0].revents & POLLIN) {
            accept_new_client(listen_fd);
        }

        // Check existing clients
        for (int i = 1; i < poll_count; i++) {
            if (!(poll_fds[i].revents & POLLIN)) {
                continue;
            }

            // Find the client with this fd
            client_t *client = find_client_by_fd(poll_fds[i].fd);
            if (!client) {
                continue;
            }

            uint8_t buffer[4096];
            ssize_t nread = recv(client->fd, buffer, sizeof(buffer), 0);

            if (nread <= 0) {
                if (nread < 0) {
                    ERROR_PRINT("recv failed: %s", strerror(errno));
                }
                remove_client(client);
            } else {
                process_client_message(client, buffer, nread);
            }
        }
    }

    // Cleanup
    INFO_PRINT("Shutting down...");
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd > 0) {
            close(clients[i].fd);
        }
    }
    close(listen_fd);

    return 0;
}
