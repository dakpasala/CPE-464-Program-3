#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <fcntl.h>

/* Debug macros */
#define DEBUG_PRINT(fmt, ...) \
    do { fprintf(stderr, "[DEBUG] " fmt "\n", ##__VA_ARGS__); } while(0)

#define ERROR_PRINT(fmt, ...) \
    do { fprintf(stderr, "[ERROR] " fmt "\n", ##__VA_ARGS__); } while(0)

#define INFO_PRINT(fmt, ...) \
    do { fprintf(stdout, "[INFO] " fmt "\n", ##__VA_ARGS__); fflush(stdout); } while(0)

/* Network utilities */
int set_nonblocking(int fd);
int create_tcp_socket(void);
int create_udp_socket(void);
int bind_socket(int fd, uint16_t port);
uint64_t get_time_ms(void);

/* File utilities */
int read_file_to_buffer(const char *filepath, uint8_t **buffer, size_t *size);
int write_buffer_to_file(const char *filepath, const uint8_t *buffer, size_t size);
void compute_sha256(const uint8_t *data, size_t len, uint8_t hash[32]);

/* String utilities */
void hash_to_string(const uint8_t hash[32], char str[65]);

#endif // COMMON_H
