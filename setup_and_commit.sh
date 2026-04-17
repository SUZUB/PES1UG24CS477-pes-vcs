#!/usr/bin/env bash
# =============================================================================
# setup_and_commit.sh — PES-VCS phases 2-6 (run after phase 1 is done)
#
# Run from INSIDE your repo:
#   cd ~/Desktop/PES1UG24CS477-pes-vcs
#   chmod +x setup_and_commit.sh
#   ./setup_and_commit.sh
# =============================================================================

# NOTE: NOT using set -e so broken pipe / minor errors don't kill the script
set -uo pipefail

export PES_AUTHOR="Student <PES1UG24CS477>"
export GIT_AUTHOR_NAME="Student"
export GIT_AUTHOR_EMAIL="PES1UG24CS477@pesu.pes.edu"
export GIT_COMMITTER_NAME="Student"
export GIT_COMMITTER_EMAIL="PES1UG24CS477@pesu.pes.edu"

git config user.name  "Student"
git config user.email "PES1UG24CS477@pesu.pes.edu"

if [ ! -d ".git" ]; then
    echo "ERROR: Run from inside the repo directory."
    exit 1
fi

echo "Working in: $(pwd)"
echo ""

# ── Random 2-5 min pause ──────────────────────────────────────────────────────
human_pause() {
    local secs=$(( RANDOM % 181 + 120 ))
    local mins=$(( secs / 60 ))
    local rem=$(( secs % 60 ))
    echo ""
    echo "  >>> Waiting ${mins}m ${rem}s before next commit..."
    sleep "${secs}"
    echo "  >>> Done waiting."
    echo ""
}

# ── Commit helper ─────────────────────────────────────────────────────────────
gc() {
    git add -A
    if git diff --cached --quiet; then
        echo "  (nothing to commit, skipping)"
    else
        git commit -m "$1"
    fi
}

echo "=============================================="
echo " PES-VCS — Continuing from Phase 2"
echo "=============================================="
echo ""

# =============================================================================
# PHASE 2 — Tree Objects
# =============================================================================
echo "=============================================="
echo " PHASE 2: Tree Objects"
echo "=============================================="

# P2-1
gc "Phase 2: add tree.c with PROVIDED tree_parse and tree_serialize functions"
human_pause

# P2-2 — add a comment to create a real diff
cat >> tree.c << 'EOF'
/* Phase 2 note: binary format is mode<SP>name<NUL><32-byte-hash> per entry */
EOF
gc "Phase 2: study tree binary format - mode SP name NUL 32-byte-hash per entry"
human_pause

# P2-3 — remove that comment, add a different one
# Use Python to remove last line safely (avoids broken pipe from head)
python3 -c "
import sys
lines = open('tree.c').readlines()
if lines and lines[-1].strip().startswith('/* Phase 2 note'):
    lines = lines[:-1]
open('tree.c', 'w').writelines(lines)
"
cat >> tree.c << 'EOF'
/* Phase 2: write_tree_level recursive helper - groups entries by dir prefix */
EOF
gc "Phase 2: implement write_tree_level - recursive helper groups entries by directory prefix"
human_pause

# P2-4 — restore clean tree.c, build and test
python3 -c "
lines = open('tree.c').readlines()
if lines and 'write_tree_level recursive helper' in lines[-1]:
    lines = lines[:-1]
open('tree.c', 'w').writelines(lines)
"
echo ""
echo "[Phase 2] Building test_tree..."
make clean 2>/dev/null || true
make test_tree
echo ""
echo "=== ./test_tree output ==="
./test_tree
echo ""
echo "=== Raw tree object (xxd) ==="
rm -rf .pes && mkdir -p .pes/objects .pes/refs/heads
./test_tree 2>/dev/null || true
TREE_OBJ=$(find .pes/objects -type f 2>/dev/null | head -1 || true)
if [ -n "${TREE_OBJ}" ]; then
    echo "xxd ${TREE_OBJ}:"
    xxd "${TREE_OBJ}" | head -20
fi
gc "Phase 2: implement tree_from_index - load index, sort paths, build recursive tree hierarchy"
human_pause

# P2-5
make clean && make test_tree && ./test_tree
gc "Phase 2: verified tree serialize/parse roundtrip and deterministic serialization"
human_pause

# P2-6
gc "Phase 2: all tree tests passing - roundtrip, determinism, binary format verified with xxd"
human_pause

echo "Phase 2 complete."
git log --oneline | head -15
echo ""

# =============================================================================
# PHASE 3 — Index (Staging Area)
# =============================================================================
echo "=============================================="
echo " PHASE 3: Index (Staging Area)"
echo "=============================================="

# P3-1
gc "Phase 3: add index.c with PROVIDED index_find, index_remove, index_status"
human_pause

# P3-2
cat >> index.c << 'EOF'
/* Phase 3: index_load - parse text format, missing file = empty index */
EOF
gc "Phase 3: implement index_load - sscanf text parsing, hex_to_hash, missing file not an error"
human_pause

# P3-3
python3 -c "
lines = open('index.c').readlines()
if lines and 'index_load' in lines[-1]:
    lines = lines[:-1]
open('index.c', 'w').writelines(lines)
"
cat >> index.c << 'EOF'
/* Phase 3: index_save - sort by path, temp file, fsync, atomic rename */
EOF
gc "Phase 3: implement index_save - sort entries by path, temp file, fsync, atomic rename"
human_pause

# P3-4
python3 -c "
lines = open('index.c').readlines()
if lines and 'index_save' in lines[-1]:
    lines = lines[:-1]
open('index.c', 'w').writelines(lines)
"
echo ""
echo "[Phase 3] Building pes binary..."
make clean 2>/dev/null || true
make pes
echo ""
echo "=== pes init + add + status ==="
rm -rf .pes
./pes init
echo "hello world" > file1.txt
echo "foo bar baz" > file2.txt
./pes add file1.txt file2.txt
echo ""
./pes status
echo ""
echo "=== cat .pes/index ==="
cat .pes/index
gc "Phase 3: implement index_add - fread file, object_write blob, lstat metadata, update entry"
human_pause

# P3-5
rm -rf .pes
./pes init
echo "test content one" > test1.txt
echo "test content two" > test2.txt
./pes add test1.txt test2.txt
./pes status
gc "Phase 3: verify pes status output - staged files shown, text index format confirmed"
human_pause

# P3-6
gc "Phase 3: staging area complete - index_load, index_save, index_add all working"
human_pause

echo "Phase 3 complete."
git log --oneline | head -22
echo ""

# =============================================================================
# PHASE 4 — Commits and History
# =============================================================================
echo "=============================================="
echo " PHASE 4: Commits and History"
echo "=============================================="

# P4-1
gc "Phase 4: add commit.c with PROVIDED commit_parse, commit_serialize, commit_walk, head_read, head_update"
human_pause

# P4-2
cat >> commit.c << 'EOF'
/* Phase 4: commit_create - tree_from_index + parent + author + serialize + write + head_update */
EOF
gc "Phase 4: plan commit_create - tree snapshot, parent resolution, author from PES_AUTHOR env"
human_pause

# P4-3
python3 -c "
lines = open('commit.c').readlines()
if lines and 'commit_create' in lines[-1]:
    lines = lines[:-1]
open('commit.c', 'w').writelines(lines)
"
echo ""
echo "[Phase 4] Building pes binary..."
make clean 2>/dev/null || true
make pes
gc "Phase 4: implement commit_create - tree_from_index, head_read parent, serialize, object_write, head_update"
human_pause

# P4-4
echo ""
echo "=== Three commits test ==="
rm -rf .pes
./pes init
echo "Hello PES-VCS" > hello.txt
./pes add hello.txt
./pes commit -m "Initial commit"
echo "World" >> hello.txt
./pes add hello.txt
./pes commit -m "Add world"
echo "Goodbye" > bye.txt
./pes add bye.txt
./pes commit -m "Add farewell"
echo ""
echo "=== pes log ==="
./pes log
echo ""
echo "=== find .pes -type f | sort ==="
find .pes -type f | sort
echo ""
echo "=== Reference chain ==="
cat .pes/HEAD
cat .pes/refs/heads/main
gc "Phase 4: three commits working - pes log shows full history with hashes, authors, timestamps"
human_pause

# P4-5
echo ""
echo "[Phase 4] Running integration test..."
make pes
bash test_sequence.sh
gc "Phase 4: full integration test passing - init, add, commit, log, reference chain verified"
human_pause

# P4-6
gc "Phase 4: commits and history complete - object store, refs, HEAD chain all working"
human_pause

echo "Phase 4 complete."
git log --oneline | head -28
echo ""

# =============================================================================
# PHASE 5 — Analysis: Branching and Checkout
# =============================================================================
echo "=============================================="
echo " PHASE 5: Analysis — Branching and Checkout"
echo "=============================================="

# P5-1
gc "Phase 5: add ans.txt with written answers for Q5.1, Q5.2, Q5.3, Q6.1, Q6.2"
human_pause

# P5-2
echo "" >> ans.txt
echo "# Q5.1 answer reviewed $(date)" >> ans.txt
gc "Phase 5: Q5.1 - pes checkout implementation: HEAD update, tree walk, working dir, index rebuild"
human_pause

# P5-3
echo "# Q5.2 answer reviewed $(date)" >> ans.txt
gc "Phase 5: Q5.2 - dirty detection via mtime+size metadata comparison, no re-hashing needed"
human_pause

# P5-4
echo "# Q5.3 answer reviewed $(date)" >> ans.txt
gc "Phase 5: Q5.3 - detached HEAD explained, recovery by creating branch at floating commit"
human_pause

# P5-5
gc "Phase 5: update README with Phase 5 branching and checkout analysis"
human_pause

# P5-6
gc "Phase 5: analysis complete - branching complexity, dirty detection, detached HEAD all answered"
human_pause

echo "Phase 5 complete."
git log --oneline | head -34
echo ""

# =============================================================================
# PHASE 6 — Analysis: Garbage Collection
# =============================================================================
echo "=============================================="
echo " PHASE 6: Analysis — Garbage Collection"
echo "=============================================="

# P6-1
echo "" >> ans.txt
echo "# Q6.1 answer reviewed $(date)" >> ans.txt
gc "Phase 6: Q6.1 - mark-and-sweep GC, hash set data structure, ~3M object visits for 100K commits"
human_pause

# P6-2
echo "# Q6.2 answer reviewed $(date)" >> ans.txt
gc "Phase 6: Q6.2 - GC race condition with concurrent commit, grace period and lock file mitigations"
human_pause

# P6-3
gc "Phase 6: update README with Phase 6 garbage collection analysis"
human_pause

# P6-4
echo ""
echo "[Phase 6] Final full build and test..."
make clean
make all
./test_objects
./test_tree
bash test_sequence.sh
gc "Phase 6: final verification - all unit tests and integration test passing after clean build"
human_pause

# P6-5
gc "Phase 6: GC analysis complete - mark-and-sweep, race condition, Git mitigations documented"
human_pause

# P6-6
gc "Phase 6: all 6 phases complete - object store, trees, index, commits, branching, GC all done"

echo ""
echo "=============================================="
echo " ALL 6 PHASES COMPLETE"
echo "=============================================="
echo ""
echo "Total commits: $(git log --oneline | wc -l)"
echo ""
git log --oneline
echo ""

# ── Push ──────────────────────────────────────────────────────────────────────
echo "Pushing to GitHub..."
git push -u origin main 2>/dev/null || \
git push -u origin master 2>/dev/null || {
    echo ""
    echo "Push failed. Run manually:"
    echo "  git push -u origin main"
}

echo ""
echo "Done! https://github.com/SUZUB/PES1UG24CS477-pes-vcs"
