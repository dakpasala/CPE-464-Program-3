#include "common.h"
#include <openssl/evp.h>

/* Set socket to non-blocking mode */
int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return -1;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/* Create TCP socket */
int create_tcp_socket(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        ERROR_PRINT("Failed to create TCP socket: %s", strerror(errno));
        return -1;
    }

    // Set SO_REUSEADDR
    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        ERROR_PRINT("setsockopt SO_REUSEADDR failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    return fd;
}

/* Create UDP socket */
int create_udp_socket(void) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        ERROR_PRINT("Failed to create UDP socket: %s", strerror(errno));
        return -1;
    }

    // Set SO_REUSEADDR
    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        ERROR_PRINT("setsockopt SO_REUSEADDR failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    return fd;
}

/* Bind socket to port */
int bind_socket(int fd, uint16_t port) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ERROR_PRINT("Failed to bind to port %u: %s", port, strerror(errno));
        return -1;
    }

    return 0;
}

/* Get current time in milliseconds */
uint64_t get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + (uint64_t)tv.tv_usec / 1000;
}

/* Read entire file into buffer */
int read_file_to_buffer(const char *filepath, uint8_t **buffer, size_t *size) {
    FILE *fp = fopen(filepath, "rb");
    if (!fp) {
        ERROR_PRINT("Failed to open file %s: %s", filepath, strerror(errno));
        return -1;
    }

    // Get file size
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (fsize < 0) {
        ERROR_PRINT("Failed to get file size: %s", strerror(errno));
        fclose(fp);
        return -1;
    }

    // Allocate buffer
    *buffer = malloc(fsize);
    if (!*buffer) {
        ERROR_PRINT("Failed to allocate buffer of size %ld", fsize);
        fclose(fp);
        return -1;
    }

    // Read file
    size_t nread = fread(*buffer, 1, fsize, fp);
    if (nread != (size_t)fsize) {
        ERROR_PRINT("Failed to read entire file: read %zu of %ld bytes", nread, fsize);
        free(*buffer);
        fclose(fp);
        return -1;
    }

    *size = fsize;
    fclose(fp);
    return 0;
}

/* Write buffer to file */
int write_buffer_to_file(const char *filepath, const uint8_t *buffer, size_t size) {
    FILE *fp = fopen(filepath, "wb");
    if (!fp) {
        ERROR_PRINT("Failed to open file %s for writing: %s", filepath, strerror(errno));
        return -1;
    }

    size_t written = fwrite(buffer, 1, size, fp);
    if (written != size) {
        ERROR_PRINT("Failed to write entire file: wrote %zu of %zu bytes", written, size);
        fclose(fp);
        return -1;
    }

    fclose(fp);
    return 0;
}

/* Compute SHA-256 hash */
void compute_sha256(const uint8_t *data, size_t len, uint8_t hash[32]) {
    EVP_MD_CTX *ctx = EVP_MD_CTX_create();
    if (!ctx) return;

    EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
    EVP_DigestUpdate(ctx, data, len);
    EVP_DigestFinal_ex(ctx, hash, NULL);
    EVP_MD_CTX_destroy(ctx);
}

/* Convert hash to hex string */
void hash_to_string(const uint8_t hash[32], char str[65]) {
    for (int i = 0; i < 32; i++) {
        sprintf(str + i * 2, "%02x", hash[i]);
    }
    str[64] = '\0';
}
