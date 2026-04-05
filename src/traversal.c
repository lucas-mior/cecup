/*
 * Copyright (C) 2025 Mior, Lucas;
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the*License,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#if !defined(TRAVERSAL_C)
#define TRAVERSAL_C

#include "cecup.h"

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_traversal 1
#elif !defined(TESTING_traversal)
#define TESTING_traversal 0
#endif
#if !defined(TESTING)
#define TESTING 0
#endif

static void
traversal_allocate(Traversal *traversal) {
    int32 capacity = INITIAL_CAPACITY;

    traversal->arena = arena_create(SIZEMB(64));

    traversal->map = hash_create_fs_map(INITIAL_CAPACITY);
    traversal->inode_map = hash_create_inode_map(INITIAL_CAPACITY);

    traversal->stats = xmalloc(capacity*SIZEOF(*(traversal->stats)));
    traversal->patterns = xmalloc(capacity*SIZEOF(*(traversal->patterns)));
    traversal->symlink_targets = xmalloc(capacity*SIZEOF(*(traversal->symlink_targets)));
    traversal->paths = xmalloc(capacity*SIZEOF(*(traversal->paths)));

    traversal->paths_lens = xmalloc(capacity*SIZEOF(*(traversal->paths_lens)));
    traversal->symlink_targets_lens = xmalloc(capacity*SIZEOF(*(traversal->symlink_targets_lens)));
    traversal->patterns_lens = xmalloc(capacity*SIZEOF(*(traversal->patterns_lens)));
    traversal->row_ids = xmalloc(capacity*SIZEOF(*(traversal->row_ids)));

    traversal->ncapacity = capacity;
    traversal->nfiles = 0;
    return;
}

static void
traversal_clean(Traversal *traversal) {

    arena_reset(traversal->arena);
    hash_zero_fs_map(traversal->map);
    hash_zero_inode_map(traversal->inode_map);

    if (DEBUGGING) {
        dont_read(traversal->stats,
                  traversal->ncapacity*SIZEOF(*(traversal->stats)));
        dont_read(traversal->paths,
                  traversal->ncapacity*SIZEOF(*(traversal->paths)));
        dont_read(traversal->symlink_targets,
                  traversal->ncapacity*SIZEOF(*(traversal->symlink_targets)));
        dont_read(traversal->patterns,
                  traversal->ncapacity*SIZEOF(*(traversal->patterns)));
        dont_read(traversal->paths_lens,
                  traversal->ncapacity*SIZEOF(*(traversal->paths_lens)));
        dont_read(traversal->symlink_targets_lens,
                  traversal->ncapacity*SIZEOF(*(traversal->symlink_targets_lens)));
        dont_read(traversal->patterns_lens,
                  traversal->ncapacity*SIZEOF(*(traversal->patterns_lens)));
    }

    traversal->file_count = 0;
    traversal->nfiles = 0;

    return;
}

static void
traversal_free(Traversal *traversal) {
    int32 capacity = traversal->ncapacity;

    hash_destroy_fs_map(traversal->map);
    hash_destroy_inode_map(traversal->inode_map);

    free(traversal->stats, capacity*SIZEOF(*(traversal->stats)));

    free(traversal->patterns,        capacity*SIZEOF(*(traversal->patterns)));
    free(traversal->symlink_targets, capacity*SIZEOF(*(traversal->symlink_targets)));
    free(traversal->paths,           capacity*SIZEOF(*(traversal->paths)));

    free(traversal->paths_lens,           capacity*SIZEOF(*(traversal->paths_lens)));
    free(traversal->symlink_targets_lens, capacity*SIZEOF(*(traversal->symlink_targets_lens)));
    free(traversal->patterns_lens,        capacity*SIZEOF(*(traversal->patterns_lens)));

    free(traversal->row_ids, capacity*SIZEOF(*(traversal->row_ids)));

    arena_destroy(traversal->arena);

    return;
}

static int32
traversal_push(Traversal *traversal, struct stat *stat,
               char *path, int32 path_len,
               char *symlink_target, int32 symlink_target_len,
               char *matched_pattern, int32 matched_pattern_len) {
    struct stat stat_copy = *stat;
    int32 idx;

    if (traversal->nfiles >= traversal->ncapacity) {
        int32 old_capacity = traversal->ncapacity;
        traversal->ncapacity *= 2;

        traversal->stats = realloc(traversal->stats,
                                   old_capacity, traversal->ncapacity,
                                   SIZEOF(*(traversal->stats)));

        traversal->paths = realloc(traversal->paths,
                                   old_capacity, traversal->ncapacity,
                                   SIZEOF(*(traversal->paths)));
        traversal->symlink_targets = realloc(traversal->symlink_targets,
                                             old_capacity, traversal->ncapacity,
                                             SIZEOF(*(traversal->symlink_targets)));
        traversal->patterns = realloc(traversal->patterns,
                                      old_capacity, traversal->ncapacity,
                                      SIZEOF(*(traversal->patterns)));

        traversal->paths_lens = realloc(traversal->paths_lens,
                                        old_capacity, traversal->ncapacity,
                                        SIZEOF(*(traversal->paths_lens)));
        traversal->symlink_targets_lens = realloc(traversal->symlink_targets_lens,
                                                  old_capacity, traversal->ncapacity,
                                                  SIZEOF(*(traversal->symlink_targets_lens)));
        traversal->patterns_lens = realloc(traversal->patterns_lens,
                                           old_capacity, traversal->ncapacity,
                                           SIZEOF(*(traversal->patterns_lens)));

        traversal->row_ids = realloc(traversal->row_ids,
                                     old_capacity, traversal->ncapacity,
                                     SIZEOF(*(traversal->row_ids)));

        for (int32 i = old_capacity; i < traversal->ncapacity; i += 1) {
            traversal->row_ids[i] = -1;
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

    if (traversal->map) {
        hash_insert_fs_map(traversal->map, path, path_len, idx);
    }

    return idx;
}

static int32
traversal_symlink_get(Traversal *traversal, char *path, char **symlink_target) {
    char buffer[MAX_PATH_LENGTH];
    int64 symlink_target_len;

    if ((symlink_target_len = readlink(path, buffer, SIZEOF(buffer) - 1)) < 0) {
        LOG_ERROR(_("Error in readlink(%s): %s.\n"), path, strerror(errno));
        *symlink_target = NULL;
        return 0;
    } else {
        buffer[symlink_target_len] = '\0';
        *symlink_target = xarena_push(traversal->arena, symlink_target_len + 1);
        memcpy64(*symlink_target, buffer, symlink_target_len + 1);
    }
    return (int32)symlink_target_len;
}

static void
traversal_add_link(Traversal *traversal, struct stat stat, char *path, int32 path_len) {
    HardLinks hard_links;
    rapidhash128_t name_hash = rapidhash128(path, (size_t)path_len);

    if ((hash_lookup_inode_map(traversal->inode_map, &stat.st_ino, &hard_links))) {
        int32 old_capacity;

        hard_links.aggregate_hash_lo ^= name_hash.lo;
        hard_links.aggregate_hash_hi ^= name_hash.hi;

        if (hard_links.count >= hard_links.capacity) {
            old_capacity = hard_links.capacity;
            hard_links.capacity *= 2;
            hard_links.names = realloc(hard_links.names,
                                       old_capacity, hard_links.capacity,
                                       SIZEOF(*(hard_links.names)));
            hard_links.names_lens = realloc(hard_links.names_lens,
                                            old_capacity, hard_links.capacity,
                                            SIZEOF(*(hard_links.names_lens)));
        }

        hard_links.names[hard_links.count] = path;
        hard_links.names_lens[hard_links.count] = path_len;
        hard_links.count += 1;

        hash_overwrite_inode_map(traversal->inode_map, &stat.st_ino, hard_links);
    } else {
        hard_links.aggregate_hash_lo = name_hash.lo;
        hard_links.aggregate_hash_hi = name_hash.hi;
        hard_links.count = 1;
        hard_links.capacity = 4;
        hard_links.names = xmalloc(hard_links.capacity * SIZEOF(*(hard_links.names)));
        hard_links.names_lens = xmalloc(hard_links.capacity * SIZEOF(*(hard_links.names_lens)));
        hard_links.names[0] = path;
        hard_links.names_lens[0] = path_len;

        hash_insert_inode_map(traversal->inode_map, &stat.st_ino, hard_links);
    }

    return;
}

static void
traversal_unlink(Traversal *traversal, int32 idx) {
    if (S_ISREG(traversal->stats[idx].st_mode)
            && (traversal->stats[idx].st_nlink > 1)) {
        char *path = traversal->paths[idx];
        int32 path_len = traversal->paths_lens[idx];
        HardLinks hard_links;
        rapidhash128_t name_hash;
        ino_t *inode = &traversal->stats[idx].st_ino;

        if ((hash_lookup_inode_map(traversal->inode_map, inode, &hard_links))) {
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
                    hash_remove_inode_map(traversal->inode_map, inode);
                } else {
                    hash_overwrite_inode_map(traversal->inode_map, inode, hard_links);
                }
            }
        }
    }
    return;
}

#if (0 == TESTING_traversal) && TESTING
static inline void
traversal_functions_sink(void) {
    (void)traversal_functions_sink;
    return;
}
#endif

#if TESTING_traversal
#include <assert.h>
#include <string.h>
#include "work.c"
#include "assert.c"

int
main(void) {
    Traversal test_traversal;
    struct stat dummy_stat;
    int32 idx;
    HardLinks hl;

    memset64(&test_traversal, 0, SIZEOF(test_traversal));
    memset64(&dummy_stat, 0, SIZEOF(dummy_stat));

    traversal_allocate(&test_traversal);
    ASSERT(test_traversal.ncapacity == INITIAL_CAPACITY);

    dummy_stat.st_ino = 100;
    dummy_stat.st_mode = S_IFREG | 0644;
    dummy_stat.st_size = 1024;

    idx = traversal_push(&test_traversal, &dummy_stat, "file_1", 6, NULL, 0, NULL, 0);
    ASSERT_EQUAL(idx, 0);
    ASSERT_EQUAL(test_traversal.nfiles, 1);
    ASSERT_EQUAL((int32)test_traversal.stats[0].st_ino, 100);

    for (int32 i = 1; i < (INITIAL_CAPACITY + 5); i += 1) {
        char *name = xarena_push(test_traversal.arena, 16);
        snprintf(name, 16, "file_%d", i);
        traversal_push(&test_traversal, &dummy_stat, name, strlen32(name), NULL, 0, NULL, 0);
    }
    ASSERT(test_traversal.ncapacity > INITIAL_CAPACITY);
    ASSERT_EQUAL(test_traversal.nfiles, INITIAL_CAPACITY + 5);

    /* 3. Test HardLink 128-bit Logic */
    struct stat link_stat;
    memset64(&link_stat, 0, SIZEOF(link_stat));
    link_stat.st_ino = 999;
    link_stat.st_mode = S_IFREG | 0644;
    link_stat.st_nlink = 2;

    /* Add first link */
    traversal_add_link(&test_traversal, link_stat, "link_a", 6);
    ASSERT(hash_lookup_inode_map(test_traversal.inode_map, &link_stat.st_ino, &hl));
    ASSERT_EQUAL(hl.count, 1);

    uint64 first_lo = hl.aggregate_hash_lo;
    uint64 first_hi = hl.aggregate_hash_hi;

    /* Add second link and verify XOR sum changed */
    traversal_add_link(&test_traversal, link_stat, "link_b", 6);
    hash_lookup_inode_map(test_traversal.inode_map, &link_stat.st_ino, &hl);
    ASSERT_EQUAL(hl.count, 2);
    ASSERT(hl.aggregate_hash_lo != first_lo);
    ASSERT(hl.aggregate_hash_hi != first_hi);

    /* 4. Test traversal_unlink 128-bit restoration */
    /* Manually push link_a to the traversal arrays so unlink can find it */
    int32 link_idx = traversal_push(&test_traversal, &link_stat, "link_a", 6, NULL, 0, NULL, 0);

    traversal_unlink(&test_traversal, link_idx);

    /* After unlinking 'link_a', the hash should return to its state after 'link_b' alone */
    /* (Because A ^ B ^ A = B) */
    hash_lookup_inode_map(test_traversal.inode_map, &link_stat.st_ino, &hl);
    ASSERT_EQUAL(hl.count, 1);

    rapidhash128_t hash_b = rapidhash128("link_b", 6);
    ASSERT_EQUAL(hl.aggregate_hash_lo, hash_b.lo);
    ASSERT_EQUAL(hl.aggregate_hash_hi, hash_b.hi);

    /* 5. Clean and Free */
    traversal_clean(&test_traversal);
    ASSERT_EQUAL(test_traversal.nfiles, 0);

    traversal_free(&test_traversal);

    ASSERT(true);
    exit(EXIT_SUCCESS);
}

#endif /* TESTING_traversal */

#endif /* TRAVERSAL_C */
