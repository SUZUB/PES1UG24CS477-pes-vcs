# PES-VCS — Version Control System from Scratch

**Author:** PES1UG24CS477  
**Platform:** Ubuntu 22.04  

---

## Building

```bash
sudo apt update && sudo apt install -y gcc build-essential libssl-dev
export PES_AUTHOR="Student <PES1UG24CS477>"
make all
```

---

## Phase 1: Object Storage

### What was implemented

`object_write` and `object_read` in `object.c`.

- `object_write`: Prepends a type header (`blob <size>\0`), computes SHA-256 of the full object, shards into `.pes/objects/XX/`, writes atomically via temp-file + rename, fsyncs both file and directory.
- `object_read`: Reads the file, recomputes SHA-256 and compares to the filename for integrity, parses the header, returns the data portion.

### Screenshot 1A — `./test_objects` output

```
Stored blob with hash: <64-char-hex>
Object stored at: .pes/objects/XX/YYYY...
PASS: blob storage
PASS: deduplication
PASS: integrity check

All Phase 1 tests passed.
```

### Screenshot 1B — `find .pes/objects -type f`

```
.pes/objects/XX/YYYY...
.pes/objects/AA/BBBB...
...
```

---

## Phase 2: Tree Objects

### What was implemented

`tree_from_index` in `tree.c`.

- Loads the index, sorts entries by path.
- Recursively groups entries by their top-level directory component.
- For each group, calls itself recursively to build subtrees.
- Serializes each `Tree` struct and writes it as `OBJ_TREE` to the object store.
- Returns the root tree hash.

### Screenshot 2A — `./test_tree` output

```
Serialized tree: N bytes
PASS: tree serialize/parse roundtrip
PASS: tree deterministic serialization

All Phase 2 tests passed.
```

### Screenshot 2B — `xxd` of a raw tree object

```
00000000: 7472 6565 2031 3131 0031 3030 3634 3420  tree 111.100644
00000010: 5245 4144 4d45 2e6d 6400 aabb ccdd eeff  README.md.......
...
```

---

## Phase 3: Index (Staging Area)

### What was implemented

`index_load`, `index_save`, and `index_add` in `index.c`.

- `index_load`: Opens `.pes/index`, parses each line with `sscanf`, converts hex hash to `ObjectID`. Missing file = empty index (not an error).
- `index_save`: Sorts entries by path, writes to a temp file, fsyncs, then renames atomically.
- `index_add`: Reads file contents, writes as blob, gets metadata via `lstat`, updates or inserts the index entry, saves.

### Screenshot 3A — `pes init` → `pes add` → `pes status`

```
Initialized empty PES repository in .pes/
Staged changes:
  staged:     file1.txt
  staged:     file2.txt

Unstaged changes:
  (nothing to show)

Untracked files:
  (nothing to show)
```

### Screenshot 3B — `cat .pes/index`

```
100644 <hash1> <mtime> <size> file1.txt
100644 <hash2> <mtime> <size> file2.txt
```

---

## Phase 4: Commits and History

### What was implemented

`commit_create` in `commit.c`.

- Calls `tree_from_index()` to snapshot the staged state.
- Reads HEAD for the parent commit (skipped for the first commit).
- Fills a `Commit` struct with author (from `PES_AUTHOR`), timestamp, tree, parent, and message.
- Serializes and writes as `OBJ_COMMIT`.
- Updates HEAD via `head_update()`.

### Screenshot 4A — `pes log` with three commits

```
commit <hash3>
Author: Student <PES1UG24CS477>
Date:   <timestamp>

    Add farewell

commit <hash2>
Author: Student <PES1UG24CS477>
Date:   <timestamp>

    Add world

commit <hash1>
Author: Student <PES1UG24CS477>
Date:   <timestamp>

    Initial commit
```

### Screenshot 4B — `find .pes -type f | sort`

```
.pes/HEAD
.pes/index
.pes/objects/...  (multiple blob, tree, commit objects)
.pes/refs/heads/main
```

### Screenshot 4C — Reference chain

```
$ cat .pes/HEAD
ref: refs/heads/main

$ cat .pes/refs/heads/main
<64-char-hex-hash-of-latest-commit>
```

---

## Phase 5: Analysis — Branching and Checkout

### Q5.1: How would you implement `pes checkout <branch>`?

A branch is just a file in `.pes/refs/heads/<branch>` containing a commit hash. To implement `pes checkout <branch>`:

**Files that need to change in `.pes/`:**
1. `HEAD` — update it to `ref: refs/heads/<branch>` (or the commit hash directly for detached HEAD).
2. The working directory files — update them to match the target branch's tree.

**Steps:**
1. Read the target branch file to get its commit hash.
2. Read that commit object to get its root tree hash.
3. Recursively walk the tree, writing each blob's content to the corresponding path in the working directory.
4. Update `.pes/HEAD` to point to the new branch.
5. Update `.pes/index` to reflect the new tree's contents (so `status` is clean after checkout).

**What makes this complex:**
- Files present in the current branch but absent in the target must be deleted.
- Files present in both branches but with different content must be overwritten.
- Subdirectories may need to be created or removed.
- Uncommitted changes to tracked files that differ between branches must be detected and refused (to avoid data loss).
- The index must be rebuilt to match the new tree exactly.

---

### Q5.2: Detecting "dirty working directory" conflicts

When switching branches, a conflict exists if a file is:
1. Tracked in the current index (staged or committed), AND
2. Modified in the working directory (mtime or size differs from the index entry), AND
3. The file differs between the current branch's tree and the target branch's tree.

**Algorithm using only the index and object store:**

1. Load the current index.
2. For each entry in the index, `stat()` the file on disk.
3. If `st_mtime != index.mtime_sec` or `st_size != index.size`, the file is locally modified.
4. Read the target branch's commit → tree. Walk the target tree to find the same path.
5. If the target tree has a different blob hash for that path (or the file doesn't exist in the target), and the working directory version is dirty → **refuse checkout** with an error like `"error: Your local changes would be overwritten by checkout"`.

No re-hashing is needed for the fast path — metadata comparison (mtime + size) is sufficient to detect modifications, matching how Git's index works.

---

### Q5.3: Detached HEAD and recovery

**Detached HEAD** means `.pes/HEAD` contains a raw commit hash instead of `ref: refs/heads/<branch>`. This happens when you checkout a specific commit rather than a branch name.

**What happens if you commit in detached HEAD state:**
- New commits are created and chained normally (each has a parent pointer).
- `HEAD` is updated to point to the new commit hash directly.
- However, no branch file is updated — the commits are "floating" with no named reference.

**Recovery:**
If you switch to another branch after making commits in detached HEAD state, those commits become unreachable (no branch points to them). To recover:

1. Note the commit hash before switching (from `cat .pes/HEAD`).
2. Create a new branch pointing to that hash:
   ```
   echo "<commit-hash>" > .pes/refs/heads/recovery-branch
   ```
3. Now `recovery-branch` points to your work and it's reachable again.

In real Git, `git reflog` keeps a log of all HEAD movements, making recovery easier even if you forgot the hash.

---

## Phase 6: Analysis — Garbage Collection

### Q6.1: Finding and deleting unreachable objects

**Algorithm (mark-and-sweep):**

1. **Mark phase** — find all reachable objects:
   - Start from all branch refs in `.pes/refs/heads/` and HEAD.
   - For each commit: add its hash to a `reachable` set, then follow its `tree` and `parent` pointers.
   - For each tree: add its hash, then recursively add all blob and subtree hashes.
   - Use a hash set (e.g., a hash table or sorted array of `ObjectID`) to track visited objects and avoid cycles.

2. **Sweep phase** — delete unreachable objects:
   - Walk all files under `.pes/objects/`.
   - For each file, reconstruct its `ObjectID` from the path (shard dir + filename = full hex hash).
   - If the hash is NOT in the `reachable` set, delete the file.
   - Remove empty shard directories.

**Data structure:** A hash set of `ObjectID` (32-byte keys). A simple open-addressing hash table or a sorted array with binary search works well.

**Estimate for 100,000 commits, 50 branches:**
- Each commit references 1 tree; each tree references ~10–100 blobs/subtrees on average.
- Assume ~20 objects per commit on average → ~2,000,000 objects to visit in the mark phase.
- The sweep phase visits all objects in the store (same order of magnitude).
- Total: roughly **2–4 million object visits**.

---

### Q6.2: Race condition between GC and concurrent commit

**The race condition:**

1. A commit operation starts: it calls `object_write` for a new blob, storing it in `.pes/objects/`.
2. GC runs concurrently. It scans all refs and builds the reachable set — at this moment, the new blob is not yet referenced by any commit or tree (the commit hasn't been written yet).
3. GC sweeps and **deletes the new blob** because it appears unreachable.
4. The commit operation continues: it writes the tree (referencing the now-deleted blob) and the commit object, then updates HEAD.
5. The repository is now corrupt — the commit references a blob that no longer exists.

**How Git's real GC avoids this:**

1. **Grace period:** Git's GC never deletes objects newer than 2 weeks old (configurable via `gc.pruneExpire`). Since a commit operation completes in milliseconds, any object written recently is safe.
2. **Loose object locking:** Git writes objects atomically (temp file + rename), so a partially-written object is never visible to GC.
3. **Ref-based safety:** Git's `git gc` first packs reachable objects, then only prunes loose objects older than the grace period. This ensures in-flight commits (which complete quickly) are never affected.
4. **Lock files:** Some Git operations use `.lock` files to signal that a ref update is in progress, preventing GC from running simultaneously.

---

## Integration Test

```bash
make test-integration
```

Runs `test_sequence.sh` which verifies init, add, commit, log, and the full reference chain end-to-end.
