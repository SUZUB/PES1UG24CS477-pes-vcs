#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out) {
    const char *type_str;
    switch (type) {
        case OBJ_BLOB:   type_str = "blob"; break;
        case OBJ_TREE:   type_str = "tree"; break;
        case OBJ_COMMIT: type_str = "commit"; break;
        default: return -1;
    }

    // 1. Build Header
    char header[64];
    int header_len = snprintf(header, sizeof(header), "%s %zu", type_str, len) + 1;
    size_t total_len = (size_t)header_len + len;

    // 2. Combine Header + Data
    uint8_t *full_obj = malloc(total_len);
    memcpy(full_obj, header, header_len);
    memcpy(full_obj + header_len, data, len);

    // 3. Hash and check if exists
    compute_hash(full_obj, total_len, id_out);
    if (object_exists(id_out)) {
        free(full_obj);
        return 0;
    }

    // 4. Create Directory (.pes/objects/XX)
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(id_out, hex);
    char dir_path[512];
    snprintf(dir_path, sizeof(dir_path), "%s/%.2s", OBJECTS_DIR, hex);
    mkdir(dir_path, 0755);

    // 5. Atomic Write
    char obj_path[512];
    object_path(id_out, obj_path, sizeof(obj_path));
    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", obj_path);

    int fd = open(tmp_path, O_CREAT | O_WRONLY | O_TRUNC, 0444);
    if (fd < 0) { free(full_obj); return -1; }
    write(fd, full_obj, total_len);
    fsync(fd);
    close(fd);

    if (rename(tmp_path, obj_path) < 0) {
        unlink(tmp_path);
        free(full_obj);
        return -1;
    }

    free(full_obj);
    return 0;
}

int object_read(const ObjectID *id, ObjectType *type_out, size_t *len_out, void **data_out) {
    char path[512];
    object_path(id, path, sizeof(path));

    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *raw = malloc(file_size);
    if (fread(raw, 1, file_size, f) != (size_t)file_size) {
        fclose(f); free(raw); return -1;
    }
    fclose(f);

    // Verify Integrity
    ObjectID computed;
    compute_hash(raw, file_size, &computed);
    if (memcmp(computed.hash, id->hash, HASH_SIZE) != 0) {
        free(raw); return -1;
    }

    // Parse Header
    char *type_str = (char*)raw;
    if (strncmp(type_str, "blob", 4) == 0) *type_out = OBJ_BLOB;
    else if (strncmp(type_str, "tree", 4) == 0) *type_out = OBJ_TREE;
    else if (strncmp(type_str, "commit", 6) == 0) *type_out = OBJ_COMMIT;

    uint8_t *null_byte = memchr(raw, '\0', file_size);
    size_t header_size = (null_byte - raw) + 1;
    size_t data_size = file_size - header_size;

    void *data_copy = malloc(data_size + 1);
    memcpy(data_copy, raw + header_size, data_size);
    ((char*)data_copy)[data_size] = '\0';

    *data_out = data_copy;
    *len_out = data_size;
    free(raw);
    return 0;
}
