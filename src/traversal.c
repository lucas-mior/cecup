// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(TRAVERSAL_C)
#define TRAVERSAL_C

#include "cbase.h"
#include "cecup.h"

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_traversal 1
#elif !defined(TESTING_traversal)
#define TESTING_traversal 0
#endif
#if !defined(TESTING)
#define TESTING 0
#endif

enum FsWalkInfo {
    FS_WALK_PRE_DIR,
    FS_WALK_POST_DIR,
    FS_WALK_FILE,
    FS_WALK_SYMLINK,
    FS_WALK_SYMLINK_BROKEN,
    FS_WALK_DEFAULT,
    FS_WALK_DOT,
    FS_WALK_CYCLE,
    FS_WALK_DIR_UNREADABLE,
    FS_WALK_STAT_ERROR,
    FS_WALK_ERROR,
    FS_WALK_INIT,
    FS_WALK_STAT_OK,
    FS_WALK_WHITEOUT,
};

typedef struct FsWalkEntry {
    char *path;
    char *access_path;
    char *name;
    int32 path_len;
    int32 name_len;
    int32 level;
    int32 error;
    enum FsWalkInfo info;
    struct stat *stat;
    void *native_entry;
} FsWalkEntry;

typedef struct FsWalk {
    FsWalkEntry entry;
#if CBASE_HAS_FTS
    FTS *handle;
#endif
} FsWalk;

static int32
fs_walk_len_cast(int64 length) {
    int32 result;

    if (length >= MAXOF(result)) {
        error("Path length is too large for FsWalk: %lld.\n", length);
        fatal(EXIT_FAILURE);
    }

    result = (int32)length;
    return result;
}

#if CBASE_HAS_FTS
static enum FsWalkInfo
fs_walk_info_from_fts(int32 info) {
    switch (info) {
    case FTS_D:
        return FS_WALK_PRE_DIR;
    case FTS_DP:
        return FS_WALK_POST_DIR;
    case FTS_F:
        return FS_WALK_FILE;
    case FTS_SL:
        return FS_WALK_SYMLINK;
    case FTS_SLNONE:
        return FS_WALK_SYMLINK_BROKEN;
    case FTS_DEFAULT:
        return FS_WALK_DEFAULT;
    case FTS_DOT:
        return FS_WALK_DOT;
    case FTS_DC:
        return FS_WALK_CYCLE;
    case FTS_DNR:
        return FS_WALK_DIR_UNREADABLE;
    case FTS_NS:
        return FS_WALK_STAT_ERROR;
    case FTS_ERR:
        return FS_WALK_ERROR;
    case FTS_INIT:
        return FS_WALK_INIT;
    case FTS_NSOK:
        return FS_WALK_STAT_OK;
    case FTS_W:
        return FS_WALK_WHITEOUT;
    default:
        return FS_WALK_DEFAULT;
    }
}
#endif

static bool
fs_walk_open(FsWalk *walk, char *path) {
#if CBASE_HAS_FTS
    char *paths[2];

    memset64(walk, 0, SIZEOF(*walk));

    paths[0] = path;
    paths[1] = NULL;

    walk->handle = fts_open(paths, FTS_PHYSICAL | FTS_NOCHDIR, NULL);
    return walk->handle != NULL;
#else
    memset64(walk, 0, SIZEOF(*walk));
    (void)path;
    errno = ENOSYS;
    return false;
#endif
}

static FsWalkEntry *
fs_walk_read(FsWalk *walk) {
#if CBASE_HAS_FTS
    FTSENT *entry;

    entry = fts_read(walk->handle);
    if (entry == NULL) {
        return NULL;
    }

    walk->entry.path = entry->fts_path;
    walk->entry.access_path = entry->fts_accpath;
    walk->entry.name = entry->fts_name;
    walk->entry.path_len = fs_walk_len_cast((int64)entry->fts_pathlen);
    walk->entry.name_len = fs_walk_len_cast((int64)entry->fts_namelen);
    walk->entry.level = fs_walk_len_cast((int64)entry->fts_level);
    walk->entry.error = entry->fts_errno;
    walk->entry.info = fs_walk_info_from_fts(entry->fts_info);
    walk->entry.stat = entry->fts_statp;
    walk->entry.native_entry = entry;

    return &walk->entry;
#else
    (void)walk;
    return NULL;
#endif
}

static int32
fs_walk_skip(FsWalk *walk, FsWalkEntry *entry) {
#if CBASE_HAS_FTS
    if (entry->native_entry == NULL) {
        errno = EINVAL;
        return -1;
    }

    return fts_set(walk->handle, (FTSENT *)entry->native_entry, FTS_SKIP);
#else
    (void)walk;
    (void)entry;
    errno = ENOSYS;
    return -1;
#endif
}

static int32
fs_walk_close(FsWalk *walk) {
#if CBASE_HAS_FTS
    return fts_close(walk->handle);
#else
    (void)walk;
    return 0;
#endif
}

static void
traversal_allocate(Traversal *traversal, int32 side) {
    int32 capacity = INITIAL_CAPACITY;
    char name[256];


    SNPRINTF(name, "traversal[%s]->arena", side_name(side));
    traversal->arena = arena_create(SIZEMB(64), name);

    SNPRINTF(name, "traversal[%s]->map", side_name(side));
    traversal->map = hash_create_fs_map(INITIAL_CAPACITY, name);

    SNPRINTF(name, "traversal[%s]->map", side_name(side));
    traversal->inode_map = hash_create_inode_map(INITIAL_CAPACITY, name);

    traversal->stats = malloc2(capacity*SIZEOF(*(traversal->stats)));
    traversal->patterns = malloc2(capacity*SIZEOF(*(traversal->patterns)));
    traversal->symlink_targets = malloc2(capacity*SIZEOF(*(traversal->symlink_targets)));
    traversal->paths = malloc2(capacity*SIZEOF(*(traversal->paths)));

    traversal->paths_lens = malloc2(capacity*SIZEOF(*(traversal->paths_lens)));
    traversal->symlink_targets_lens = malloc2(capacity*SIZEOF(*(traversal->symlink_targets_lens)));
    traversal->patterns_lens = malloc2(capacity*SIZEOF(*(traversal->patterns_lens)));
    traversal->row_ids = malloc2(capacity*SIZEOF(*(traversal->row_ids)));
    traversal->states = malloc2(capacity*SIZEOF(*(traversal->states)));

    traversal->capacity = capacity;
    traversal->file_count = 0;
    traversal->unknown_count = 0;
    traversal->nfiles = 0;
    traversal->root_unknown = false;
    return;
}

static void
hard_links_free(HardLinks *hard_links) {
    free2(hard_links->names,
          hard_links->capacity*SIZEOF(*(hard_links->names)));
    free2(hard_links->names_lens,
          hard_links->capacity*SIZEOF(*(hard_links->names_lens)));
    return;
}

static void
traversal_inode_map_clear(Traversal *traversal) {
    for (uint32 i = 0; i < traversal->inode_map->capacity; i += 1) {
        Bucket_inode_map *bucket = &traversal->inode_map->array[i];
        int8 slot_state = traversal->inode_map->slot_states[i];

        if (slot_state != HASH_SLOT_USED) {
            continue;
        }

        hard_links_free(&bucket->value);
    }

    hash_zero_inode_map(traversal->inode_map);
    return;
}

static void
traversal_clean(Traversal *traversal) {

    traversal_inode_map_clear(traversal);
    arena_reset(traversal->arena);
    hash_zero_fs_map(traversal->map);

    traversal->file_count = 0;
    traversal->unknown_count = 0;
    traversal->nfiles = 0;
    traversal->root_unknown = false;

    return;
}

static void
traversal_free(Traversal *traversal) {
    int32 capacity = traversal->capacity;

    traversal_inode_map_clear(traversal);
    hash_destroy_fs_map(traversal->map);
    hash_destroy_inode_map(traversal->inode_map);

    free2(traversal->stats, capacity*SIZEOF(*(traversal->stats)));

    free2(traversal->patterns,        capacity*SIZEOF(*(traversal->patterns)));
    free2(traversal->symlink_targets, capacity*SIZEOF(*(traversal->symlink_targets)));
    free2(traversal->paths,           capacity*SIZEOF(*(traversal->paths)));

    free2(traversal->paths_lens,           capacity*SIZEOF(*(traversal->paths_lens)));
    free2(traversal->symlink_targets_lens, capacity*SIZEOF(*(traversal->symlink_targets_lens)));
    free2(traversal->patterns_lens,        capacity*SIZEOF(*(traversal->patterns_lens)));

    free2(traversal->row_ids, capacity*SIZEOF(*(traversal->row_ids)));
    free2(traversal->states, capacity*SIZEOF(*(traversal->states)));

    arena_destroy(traversal->arena);

    return;
}

static int32
traversal_push_with_state(
    Traversal *traversal,
    struct stat *stat,
    char *path,
    int32 path_len,
    char *symlink_target,
    int32 symlink_target_len,
    char *matched_pattern,
    int32 matched_pattern_len,
    enum TraversalState state
) {
    struct stat stat_copy = {0};
    int32 idx;

    if (stat) {
        stat_copy = *stat;
    }

    if (traversal->nfiles >= traversal->capacity) {
        int32 old_capacity = traversal->capacity;
        traversal->capacity *= 2;

        traversal->stats = realloc2(traversal->stats,
                                    old_capacity, traversal->capacity,
                                    SIZEOF(*(traversal->stats)));

        traversal->paths = realloc2(traversal->paths,
                                    old_capacity, traversal->capacity,
                                    SIZEOF(*(traversal->paths)));
        traversal->symlink_targets = realloc2(traversal->symlink_targets,
                                              old_capacity, traversal->capacity,
                                              SIZEOF(*(traversal->symlink_targets)));
        traversal->patterns = realloc2(traversal->patterns,
                                       old_capacity, traversal->capacity,
                                       SIZEOF(*(traversal->patterns)));

        traversal->paths_lens = realloc2(traversal->paths_lens,
                                         old_capacity, traversal->capacity,
                                         SIZEOF(*(traversal->paths_lens)));
        traversal->symlink_targets_lens = realloc2(traversal->symlink_targets_lens,
                                                   old_capacity, traversal->capacity,
                                                   SIZEOF(*(traversal->symlink_targets_lens)));
        traversal->patterns_lens = realloc2(traversal->patterns_lens,
                                            old_capacity, traversal->capacity,
                                            SIZEOF(*(traversal->patterns_lens)));

        traversal->row_ids = realloc2(traversal->row_ids,
                                      old_capacity, traversal->capacity,
                                      SIZEOF(*(traversal->row_ids)));
        traversal->states = realloc2(traversal->states,
                                     old_capacity, traversal->capacity,
                                     SIZEOF(*(traversal->states)));

        for (int32 i = old_capacity; i < traversal->capacity; i += 1) {
            traversal->row_ids[i] = -1;
            traversal->states[i] = TRAVERSAL_STATE_PRESENT;
        }
    }

    idx = traversal->nfiles;
    traversal->nfiles += 1;

    memcpy64(&traversal->stats[idx], &stat_copy, SIZEOF(struct stat));

    traversal->paths[idx] = path;
    traversal->paths_lens[idx] = (int16)path_len;
    traversal->symlink_targets[idx] = symlink_target;
    traversal->symlink_targets_lens[idx] = (int16)symlink_target_len;
    traversal->patterns[idx] = matched_pattern;
    traversal->patterns_lens[idx] = (int16)matched_pattern_len;
    traversal->row_ids[idx] = -1;
    traversal->states[idx] = (int8)state;

    if (state != TRAVERSAL_STATE_PRESENT) {
        traversal->unknown_count += 1;
    }

    if (traversal->map) {
        hash_insert_fs_map(traversal->map, path, path_len, idx);
    }

    return idx;
}

static int32
traversal_push(Traversal *traversal, struct stat *stat,
               char *path, int32 path_len,
               char *symlink_target, int32 symlink_target_len,
               char *matched_pattern, int32 matched_pattern_len) {
    return traversal_push_with_state(traversal, stat,
                                     path, path_len,
                                     symlink_target, symlink_target_len,
                                     matched_pattern, matched_pattern_len,
                                     TRAVERSAL_STATE_PRESENT);
}

static bool
traversal_unknown_path_covers(char *unknown, int32 unknown_len,
                              char *path, int32 path_len) {
    if (path_len < unknown_len) {
        if ((unknown_len > 1)
            && (unknown[unknown_len - 1] == '/')
            && (path_len == (unknown_len - 1))
            && (memcmp64(path, unknown, path_len) == 0)) {
            return true;
        }

        return false;
    }
    if (memcmp64(path, unknown, unknown_len) != 0) {
        return false;
    }
    if (path_len == unknown_len) {
        return true;
    }
    if (unknown[unknown_len - 1] == '/') {
        return true;
    }
    if (path[unknown_len] == '/') {
        return true;
    }

    return false;
}

static bool
traversal_path_is_unknown(Traversal *traversal, char *path, int32 path_len) {
    int32 idx;

    if (traversal->unknown_count <= 0) {
        return false;
    }

    if (traversal->root_unknown) {
        return true;
    }

    if (hash_lookup_fs_map(traversal->map, path, path_len, &idx)) {
        if (traversal->states[idx] != TRAVERSAL_STATE_PRESENT) {
            return true;
        }
    }

    for (int32 i = 0; i < traversal->nfiles; i += 1) {
        if (traversal->states[i] != TRAVERSAL_STATE_UNKNOWN_SUBTREE) {
            continue;
        }
        if (traversal_unknown_path_covers(traversal->paths[i],
                                         traversal->paths_lens[i],
                                         path, path_len)) {
            return true;
        }
    }

    return false;
}

static void
traversal_root_unknown_record(Traversal *traversal) {
    if (!traversal->root_unknown) {
        traversal->unknown_count += 1;
    }

    traversal->root_unknown = true;
    return;
}

static void
traversal_unknown_record(
    Traversal *traversal,
    char *path,
    int32 path_len,
    enum TraversalState state
) {
    int32 idx;

    ASSERT(state != TRAVERSAL_STATE_PRESENT);

    if (hash_lookup_fs_map(traversal->map, path, path_len, &idx)) {
        if (traversal->states[idx] == TRAVERSAL_STATE_PRESENT) {
            traversal->unknown_count += 1;
        }
        traversal->states[idx] = (int8)state;
        return;
    }

    {
        char *path_alloc;

        path_alloc = xarena_push(traversal->arena, path_len + 1);
        memcpy64(path_alloc, path, path_len + 1);

        traversal_push_with_state(traversal, NULL,
                                  path_alloc, path_len,
                                  NULL, 0,
                                  NULL, 0,
                                  state);
    }
    return;
}

static int32
traversal_symlink_get(Traversal *traversal, char *path, char **symlink_target) {
    char buffer[MAX_PATH_LENGTH];
    int64 symlink_target_len;

    switch (symlink_target_len = readlink(path, buffer, SIZEOF(buffer) - 1)) {
    case -1:
        LOG_ERROR(_("Error in readlink(%s): %s.\n"), path, strerror(errno));
        *symlink_target = NULL;
        return 0;
    case SIZEOF(buffer) - 1:
        LOG_ERROR(_("readlink(%s) produces too long path.\n"), path);
        *symlink_target = NULL;
        return 0;
    default:
        buffer[symlink_target_len] = '\0';
        *symlink_target = xarena_push(traversal->arena, symlink_target_len + 1);
        memcpy64(*symlink_target, buffer, symlink_target_len + 1);
        return (int32)symlink_target_len;
    }
}

static void
traversal_add_link(
    Traversal *traversal,
    struct stat stat,
    char *path,
    int32 path_len
) {
    FileID file_id = file_id_from_stat(&stat);
    HardLinks hard_links;
    rapidhash128_t name_hash = rapidhash128(path, (size_t)path_len);

    if ((hash_lookup_inode_map(traversal->inode_map, &file_id, &hard_links))) {
        int32 old_capacity;

        hard_links.aggregate_hash_lo ^= name_hash.lo;
        hard_links.aggregate_hash_hi ^= name_hash.hi;

        if (hard_links.count >= hard_links.capacity) {
            old_capacity = hard_links.capacity;
            hard_links.capacity *= 2;
            hard_links.names = realloc2(hard_links.names,
                                        old_capacity, hard_links.capacity,
                                        SIZEOF(*(hard_links.names)));
            hard_links.names_lens = realloc2(hard_links.names_lens,
                                             old_capacity, hard_links.capacity,
                                             SIZEOF(*(hard_links.names_lens)));
        }

        hard_links.names[hard_links.count] = path;
        hard_links.names_lens[hard_links.count] = path_len;
        hard_links.count += 1;

        hash_overwrite_inode_map(traversal->inode_map, &file_id, hard_links);
    } else {
        hard_links.aggregate_hash_lo = name_hash.lo;
        hard_links.aggregate_hash_hi = name_hash.hi;
        hard_links.count = 1;
        hard_links.capacity = 4;
        hard_links.names = malloc2(
            hard_links.capacity*SIZEOF(*(hard_links.names)));
        hard_links.names_lens = malloc2(
            hard_links.capacity*SIZEOF(*(hard_links.names_lens)));
        hard_links.names[0] = path;
        hard_links.names_lens[0] = path_len;

        ASSERT(hash_insert_inode_map(traversal->inode_map,
                                     &file_id, hard_links));
    }

    return;
}

static void
traversal_unlink(Traversal *traversal, int32 idx) {
    char *path = traversal->paths[idx];
    int32 path_len = traversal->paths_lens[idx];
    FileID file_id = file_id_from_stat(&traversal->stats[idx]);
    HardLinks hard_links;
    rapidhash128_t name_hash;

    if ((hash_lookup_inode_map(traversal->inode_map, &file_id, &hard_links))) {
        int32 found_idx = -1;

        name_hash = rapidhash128(path, (size_t)path_len);

        for (int32 i = 0; i < hard_links.count; i += 1) {
            if (hard_links.names_lens[i] == path_len) {
                if (memcmp64(hard_links.names[i], path, path_len) == 0) {
                    found_idx = i;
                    break;
                }
            }
        }

        if (found_idx >= 0) {
            for (int32 i = found_idx; i < (hard_links.count - 1); i += 1) {
                hard_links.names[i] = hard_links.names[i + 1];
                hard_links.names_lens[i] = hard_links.names_lens[i + 1];
            }

            hard_links.count -= 1;
            hard_links.aggregate_hash_lo ^= name_hash.lo;
            hard_links.aggregate_hash_hi ^= name_hash.hi;

            if (hard_links.count == 0) {
                ASSERT(hash_remove_inode_map(traversal->inode_map, &file_id));
                hard_links_free(&hard_links);
            } else {
                hash_overwrite_inode_map(traversal->inode_map,
                                         &file_id, hard_links);
            }
        }
    }
    return;
}

static char *
traversal_path_get(int32 src_idx, int32 dst_idx) {
    char *path;

    if (src_idx >= 0) {
        path = cecup.traversal[L].paths[src_idx];
    } else if (dst_idx >= 0) {
        path = cecup.traversal[R].paths[dst_idx];
    } else {
        error("Error: both source and destination have invalid indices.\n");
        fatal(EXIT_FAILURE);
    }

    return path;
}

INLINE char *
traversal_path_side(int32 idx, int32 side) {
    if (idx >= 0) {
        return cecup.traversal[side].paths[idx];
    } else {
        return NULL;
    }
}

static int64
traversal_size_side(int32 idx, int32 side) {
    if (idx >= 0) {
        return cecup.traversal[side].stats[idx].st_size;
    } else {
        return -1;
    }
}

static int64
traversal_mtime_side(int32 idx, int32 side) {
    if (idx >= 0) {
        return cecup.traversal[side].stats[idx].st_mtime;
    } else {
        return 0;
    }
}

#if (0 == TESTING_traversal) && TESTING
static inline void
traversal_functions_sink(void) {
    (void)traversal_functions_sink;
    return;
}
#endif

#if TESTING_traversal
#define CBASE_IMPLEMENT
#include "cbase.h"

#include "work.c"

int
main(void) {
    Traversal test_traversal;
    struct stat dummy_stat;
    int32 idx;
    HardLinks hl;
    char *symlink_target;
    int32 symlink_len;
    struct stat link_stat;
    uint64 first_lo;
    uint64 first_hi;
    int32 link_idx;
    rapidhash128_t hash_b;
    FileID link_file_id;
    FileID other_file_id;
    char temp_dir[PATH_MAX];

    memset64(&test_traversal, 0, SIZEOF(test_traversal));
    memset64(&dummy_stat, 0, SIZEOF(dummy_stat));
    test_make_temp_dir(temp_dir, SIZEOF(temp_dir), "traversal");

    traversal_allocate(&test_traversal, L);
    ASSERT(test_traversal.capacity == INITIAL_CAPACITY);

    traversal_unknown_record(&test_traversal,
                             STRLIT("private"),
                             TRAVERSAL_STATE_UNKNOWN_SUBTREE);
    ASSERT(traversal_path_is_unknown(&test_traversal,
                                     STRLIT("private")));
    ASSERT(traversal_path_is_unknown(&test_traversal,
                                     STRLIT("private/file.txt")));
    ASSERT(!traversal_path_is_unknown(&test_traversal,
                                      STRLIT("private2/file.txt")));

    traversal_unknown_record(&test_traversal,
                             STRLIT("sealed/"),
                             TRAVERSAL_STATE_UNKNOWN_SUBTREE);
    ASSERT(traversal_path_is_unknown(&test_traversal, STRLIT("sealed")));

    traversal_unknown_record(&test_traversal,
                             STRLIT("leaf"),
                             TRAVERSAL_STATE_UNKNOWN);
    ASSERT(traversal_path_is_unknown(&test_traversal, STRLIT("leaf")));
    ASSERT(!traversal_path_is_unknown(&test_traversal, STRLIT("leaf/file")));

    traversal_clean(&test_traversal);
    ASSERT(!traversal_path_is_unknown(&test_traversal,
                                      STRLIT("private/file.txt")));

    dummy_stat.st_ino = 100;
    dummy_stat.st_mode = S_IFREG | 0644;
    dummy_stat.st_size = 1024;

    idx = traversal_push(&test_traversal, &dummy_stat,
                         "file_1", 6, NULL, 0, NULL, 0);
    ASSERT_EQUAL(idx, 0);
    ASSERT_EQUAL(test_traversal.nfiles, 1);
    ASSERT_EQUAL((int32)test_traversal.stats[0].st_ino, 100);

    for (int32 i = 1; i < (INITIAL_CAPACITY + 5); i += 1) {
        char *name = xarena_push(test_traversal.arena, 16);
        snprintf(name, 16, "file_%d", i);
        traversal_push(&test_traversal, &dummy_stat, name,
                       strlen32(name), NULL, 0, NULL, 0);
    }
    ASSERT(test_traversal.capacity > INITIAL_CAPACITY);
    ASSERT_EQUAL(test_traversal.nfiles, INITIAL_CAPACITY + 5);

    /* 2.5 Test traversal_symlink_get */
    symlink_target = NULL;
    symlink_len = 0;

    if (test_symlink_supported(temp_dir)) {
        char link_path[PATH_MAX];

        SNPRINTF(link_path, "%s/test_symlink", temp_dir);
        ASSERT(symlink("dummy_target.txt", link_path) == 0);
        symlink_len = traversal_symlink_get(&test_traversal,
                                            link_path, &symlink_target);
        ASSERT(symlink_len > 0);
        ASSERT_EQUAL(symlink_len, strlen32("dummy_target.txt"));
        ASSERT(symlink_target != NULL);
        ASSERT_EQUAL(symlink_target, "dummy_target.txt");
        remove(link_path);
    }

    symlink_len = traversal_symlink_get(&test_traversal,
                                         "non_existent_symlink",
                                         &symlink_target);
    ASSERT_EQUAL(symlink_len, 0);
    ASSERT_NULL(symlink_target);

    /* 3. Test HardLink 128-bit Logic */
    memset64(&link_stat, 0, SIZEOF(link_stat));
    link_stat.st_ino = 999;
    link_stat.st_mode = S_IFREG | 0644;
    link_stat.st_nlink = 2;
    link_file_id = file_id_from_stat(&link_stat);

    /* Add first link */
    traversal_add_link(&test_traversal, link_stat, "link_a", 6);
    ASSERT(hash_lookup_inode_map(test_traversal.inode_map, &link_file_id, &hl));
    ASSERT_EQUAL(hl.count, 1);

    first_lo = hl.aggregate_hash_lo;
    first_hi = hl.aggregate_hash_hi;

    /* Add second link and verify XOR sum changed */
    traversal_add_link(&test_traversal, link_stat, "link_b", 6);
    hash_lookup_inode_map(test_traversal.inode_map, &link_file_id, &hl);
    ASSERT_EQUAL(hl.count, 2);
    ASSERT(hl.aggregate_hash_lo != first_lo);
    ASSERT(hl.aggregate_hash_hi != first_hi);

    {
        struct stat other_stat = link_stat;
        HardLinks other_hl;

        other_stat.st_dev += 1;
        other_file_id = file_id_from_stat(&other_stat);
        traversal_add_link(&test_traversal, other_stat, "link_other", 10);

        ASSERT(hash_lookup_inode_map(test_traversal.inode_map,
                                     &other_file_id, &other_hl));
        ASSERT_EQUAL(other_hl.count, 1);
        hash_lookup_inode_map(test_traversal.inode_map, &link_file_id, &hl);
        ASSERT_EQUAL(hl.count, 2);
    }

    /* 4. Test traversal_unlink 128-bit restoration */
    /* Manually push link_a to the traversal arrays so unlink can find it */
    link_idx = traversal_push(&test_traversal, &link_stat,
                              "link_a", 6, NULL, 0, NULL, 0);

    traversal_unlink(&test_traversal, link_idx);

    /* After unlinking 'link_a', the hash should return to its state after */
    /* 'link_b' alone, because A ^ B ^ A = B. */
    hash_lookup_inode_map(test_traversal.inode_map, &link_file_id, &hl);
    ASSERT_EQUAL(hl.count, 1);

    hash_b = rapidhash128("link_b", 6);
    ASSERT_EQUAL(hl.aggregate_hash_lo, hash_b.lo);
    ASSERT_EQUAL(hl.aggregate_hash_hi, hash_b.hi);

    /* 5. Clean and Free */
    traversal_clean(&test_traversal);
    ASSERT_EQUAL(test_traversal.nfiles, 0);

    traversal_free(&test_traversal);
    test_remove_tree(temp_dir);

    ASSERT(true);
    exit(EXIT_SUCCESS);
}

#endif /* TESTING_traversal */

#endif /* TRAVERSAL_C */
