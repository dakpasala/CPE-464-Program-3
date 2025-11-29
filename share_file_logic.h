#ifndef SHARE_FILE_LOGIC_H
#define SHARE_FILE_LOGIC_H

#include <stdint.h>
#include <stddef.h>

#define MAX_SHARED_FILES 1024

typedef struct {

    uint32_t file_id;
    uint32_t file_size;
    uint8_t hash[32];
    char file_name[256];

} shared_file_t;

// to add a file
void add_shared_file(uint32_t file_id, uint32_t file_size, uint8_t hash[32], char* file_name);

// look up file by file_id
shared_file_t *find_shared_file_by_id(uint32_t file_id);

#endif // SHARE_FILE_LOGIC_H