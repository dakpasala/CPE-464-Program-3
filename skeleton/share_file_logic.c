#include "share_file_logic.h"
#include "common.h"

#include <string.h>

static shared_file_t table[MAX_SHARED_FILES];
static int num_files = 0;

void add_shared_file(uint32_t file_id, uint32_t size, uint8_t hash[32], char *filename)
{
    if (num_files >= MAX_SHARED_FILES) {
        ERROR_PRINT("Shared file table full");
        return;
    }

    table[num_files].file_id = file_id;
    table[num_files].file_size = size;
    memcpy(table[num_files].hash, hash, 32);
    strncpy(table[num_files].file_name, filename, sizeof(table[num_files].file_name)-1);
    table[num_files].file_name[sizeof(table[num_files].file_name)-1] = '\0';
    num_files++;
}


shared_file_t* find_shared_file_by_id(uint32_t file_id)
{
    for (int i = 0; i < num_files; i++) {
        if (table[i].file_id == file_id) {
            return &table[i];
        }
    }
    return NULL;
}


