// object.c — Content-addressable object store
//
// Every piece of data (file contents, directory listings, commits) is stored
// as an "object" named by its SHA-256 hash. Objects are stored under
// .pes/objects/XX/YYYYYY... where XX is the first two hex characters of the
// hash (directory sharding).
//
// PROVIDED functions: compute_hash, object_path, object_exists, hash_to_hex, hex_to_hash
// TODO functions: object_write, object_read

#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <openssl/sha.h>

// ─── PROVIDED ────────────────────────────────────────────────────────────────

void hash_to_hex(const ObjectID *id, char *hex_out) {
    for (int i = 0; i < HASH_SIZE; i++) {
        sprintf(hex_out + i * 2, "%02x", id->hash[i]);
    }
    hex_out[HASH_HEX_SIZE] = '\0';
}

int hex_to_hash(const char *hex, ObjectID *id_out) {
    if (strlen(hex) < HASH_HEX_SIZE) return -1;
    for (int i = 0; i < HASH_SIZE; i++) {
        unsigned int byte;
        if (sscanf(hex + i * 2, "%2x", &byte) != 1) return -1;
        id_out->hash[i] = (uint8_t)byte;
    }
    return 0;
}

void compute_hash(const void *data, size_t len, ObjectID *id_out) {
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    SHA256_Update(&ctx, data, len);
    SHA256_Final(id_out->hash, &ctx);
}

// Get the filesystem path where an object should be stored.
// Format: .pes/objects/XX/YYYYYYYY...
// The first 2 hex chars form the shard directory; the rest is the filename.
void object_path(const ObjectID *id, char *path_out, size_t path_size) {
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(id, hex);
    snprintf(path_out, path_size, "%s/%.2s/%s", OBJECTS_DIR, hex, hex + 2);
}

int object_exists(const ObjectID *id) {
    char path[512];
    object_path(id, path, sizeof(path));
    return access(path, F_OK) == 0;
}

// ─── IMPLEMENTED ─────────────────────────────────────────────────────────────

/*
 * object_write — Store an object in the content-addressable store.
 *
 * HOW IT WORKS:
 *   Every object on disk looks like:  "<type> <size>\0<raw data bytes>"
 *   e.g., for a blob containing "Hello\n":
 *       "blob 6\0Hello\n"
 *       ^^^^^^^  ^^^^^^
 *       header   data
 *
 *   We hash the ENTIRE thing (header + data) with SHA-256.
 *   That hash becomes the object's filename, sharded into XX/YYYYYYYY...
 *
 *   Atomic write pattern:
 *     write to tmp file → fsync → rename to final path
 *   This ensures we never have a half-written object in the store.
 *
 * Returns 0 on success, -1 on error.
 */
int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out) {
    // Step 1: Choose the type string
    const char *type_str;
    switch (type) {
        case OBJ_BLOB:   type_str = "blob";   break;
        case OBJ_TREE:   type_str = "tree";   break;
        case OBJ_COMMIT: type_str = "commit"; break;
        default: return -1;
    }

    // Step 2: Build the full object = header + data
    // Header format: "<type> <size>\0"
    char header[64];
    int header_len = snprintf(header, sizeof(header), "%s %zu", type_str, len) + 1;
    // +1 to include the '\0' terminator that snprintf writes

    size_t total_len = (size_t)header_len + len;
    uint8_t *full_object = malloc(total_len);
    if (!full_object) return -1;

    memcpy(full_object, header, header_len);          // copy "blob 6\0"
    memcpy(full_object + header_len, data, len);      // copy actual file bytes

    // Step 3: Compute SHA-256 of the full object
    compute_hash(full_object, total_len, id_out);

    // Step 4: Deduplication — if it already exists, nothing to do
    if (object_exists(id_out)) {
        free(full_object);
        return 0;
    }

    // Step 5: Build the directory path (.pes/objects/XX/) and create it
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(id_out, hex);

    char dir_path[512];
    snprintf(dir_path, sizeof(dir_path), "%s/%.2s", OBJECTS_DIR, hex);
    mkdir(dir_path, 0755);  // ignore error if already exists

    // Step 6: Build final object path
    char obj_path[512];
    object_path(id_out, obj_path, sizeof(obj_path));

    // Step 7: Write to a temporary file first (atomic write pattern)
    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", obj_path);

    int fd = open(tmp_path, O_CREAT | O_WRONLY | O_TRUNC, 0444);
    if (fd < 0) {
        free(full_object);
        return -1;
    }

    // Write the full object (header + data) to the temp file
    ssize_t written = write(fd, full_object, total_len);
    free(full_object);

    if (written < 0 || (size_t)written != total_len) {
        close(fd);
        unlink(tmp_path);
        return -1;
    }

    // Step 8: fsync to make sure data is on disk before we rename
    fsync(fd);
    close(fd);

    // Step 9: Atomic rename — this is the "commit" operation
    // If the system crashes between write and rename, the tmp file is
    // incomplete but the final path is untouched. Safe!
    if (rename(tmp_path, obj_path) != 0) {
        unlink(tmp_path);
        return -1;
    }

    // Step 10: fsync the directory so the rename itself is durable
    int dir_fd = open(dir_path, O_RDONLY);
    if (dir_fd >= 0) {
        fsync(dir_fd);
        close(dir_fd);
    }

    return 0;
}

/*
 * object_read — Load and verify an object from the store.
 *
 * HOW IT WORKS:
 *   1. Find the file path from the hex hash
 *   2. Read the entire file into memory
 *   3. Recompute SHA-256 and compare to the filename (integrity check!)
 *      If they differ, the file was corrupted on disk.
 *   4. Parse the header: find the '\0', extract type string and size
 *   5. Return a pointer to just the data portion (after the '\0')
 *
 * The caller must free(*data_out) when done.
 * Returns 0 on success, -1 on error/corruption.
 */
int object_read(const ObjectID *id, ObjectType *type_out, void **data_out, size_t *len_out) {
    // Step 1: Get the file path from the hash
    char path[512];
    object_path(id, path, sizeof(path));

    // Step 2: Open and read the entire file
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    // Get file size
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size <= 0) {
        fclose(f);
        return -1;
    }

    uint8_t *raw = malloc((size_t)file_size);
    if (!raw) {
        fclose(f);
        return -1;
    }

    size_t bytes_read = fread(raw, 1, (size_t)file_size, f);
    fclose(f);

    if (bytes_read != (size_t)file_size) {
        free(raw);
        return -1;
    }

    // Step 3: Integrity check — recompute hash and compare to what we expected
    ObjectID computed;
    compute_hash(raw, bytes_read, &computed);
    if (memcmp(computed.hash, id->hash, HASH_SIZE) != 0) {
        // The stored file doesn't match its filename — it was corrupted!
        fprintf(stderr, "error: object corruption detected\n");
        free(raw);
        return -1;
    }

    // Step 4: Parse the header
    // Header format: "<type> <size>\0<data>"
    // Find the null byte that separates header from data
    uint8_t *null_byte = memchr(raw, '\0', bytes_read);
    if (!null_byte) {
        free(raw);
        return -1;
    }

    // Parse type string (before the space in header)
    char *header = (char *)raw;
    char *space = strchr(header, ' ');
    if (!space) {
        free(raw);
        return -1;
    }

    // Determine object type
    size_t type_len = (size_t)(space - header);
    if (strncmp(header, "blob", type_len) == 0 && type_len == 4) {
        *type_out = OBJ_BLOB;
    } else if (strncmp(header, "tree", type_len) == 0 && type_len == 4) {
        *type_out = OBJ_TREE;
    } else if (strncmp(header, "commit", type_len) == 0 && type_len == 6) {
        *type_out = OBJ_COMMIT;
    } else {
        free(raw);
        return -1;
    }

    // Parse declared size from header
    size_t declared_size = (size_t)strtoul(space + 1, NULL, 10);

    // Step 5: Extract the data portion (everything after the '\0')
    uint8_t *data_start = null_byte + 1;
    size_t data_len = bytes_read - (size_t)(data_start - raw);

    if (data_len != declared_size) {
        // Header claims a different size than what's actually there
        free(raw);
        return -1;
    }

    // Allocate a new buffer for just the data (caller will free this)
    uint8_t *data_copy = malloc(data_len + 1);  // +1 for safety null terminator
    if (!data_copy) {
        free(raw);
        return -1;
    }
    memcpy(data_copy, data_start, data_len);
    data_copy[data_len] = '\0';  // safe null terminator for text objects

    free(raw);

    *data_out = data_copy;
    *len_out = data_len;
    return 0;
}

