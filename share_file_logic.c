#include "share_file_logic.h"
#include "common.h"

#include <string.h>

static shared_file_t local_shared_files[MAX_SHARED_FILES];
static size_t local_shared_file_count = 0;

void add_shared_file(uint32_t file_id, uint32_t file_size, uint8_t hash[32], char* file_name){

    if (local_shared_file_count >= MAX_SHARED_FILES){
        ERROR_PRINT("Table is full, can't add");
        return;
    }

    shared_file_t* entry = &local_shared_files[local_shared_file_count++];

    entry->file_id = file_id;
    entry->file_size = file_size;

    memcpy(entry->hash, hash, 32);
    strncpy(entry->file_name, file_name, sizeof(entry->file_name) - 1);
    entry->file_name[sizeof(entry->file_name) - 1] = '\0';
}

shared_file_t *find_shared_file_by_id(uint32_t file_id){

    for (size_t i = 0; i < local_shared_file_count; i++){

        if (local_shared_files->file_id == file_id){
            return &local_shared_files[i];
        }
    }

    return NULL;
}

