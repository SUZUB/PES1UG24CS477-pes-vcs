// commit.h — Commit object interface
//
// A commit ties together a tree snapshot, parent history, author info,
// and a human-readable message.

#ifndef COMMIT_H
#define COMMIT_H

#include "pes.h"

typedef struct {
    ObjectID tree;          // Root tree hash (the project snapshot)
    ObjectID parent;        // Parent commit hash
    int has_parent;         // 0 for the initial commit, 1 otherwise
    char author[256];       // Author string (from PES_AUTHOR env var)
    uint64_t timestamp;     // Unix timestamp of commit creation
    char message[4096];     // Commit message
} Commit;

// Create a commit from the current index.
int commit_create(const char *message, ObjectID *commit_id_out);

// Parse raw commit object data into a Commit struct.
int commit_parse(const void *data, size_t len, Commit *commit_out);

// Serialize a Commit struct into raw bytes for object_write(OBJ_COMMIT, ...).
// Caller must free(*data_out).
int commit_serialize(const Commit *commit, void **data_out, size_t *len_out);

// Walk commit history starting from HEAD, following parent pointers.
typedef void (*commit_walk_fn)(const ObjectID *id, const Commit *commit, void *ctx);
int commit_walk(commit_walk_fn callback, void *ctx);

// ─── HEAD helpers ───────────────────────────────────────────────────────────

int head_read(ObjectID *id_out);
int head_update(const ObjectID *new_commit);

#endif // COMMIT_H
