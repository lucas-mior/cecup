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

#if !defined ITEM_C
#define ITEM_C

#include "cecup.h"
#include "rapidhash.h"

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_item 1
#elif !defined(TESTING_item)
#define TESTING_item 0
#endif
#if !defined(TESTING)
#define TESTING 0
#endif

typedef struct SortEntry {
    int32 row_id;
    union {
        int64 i64;
        char *ptr;
    } key;
} SortEntry;

static void
hard_link_replace_node(HardLinks *list,
                       char *old_path, int32 old_path_len,
                       char *new_path, int32 new_path_len) {
    uint64 old_hash;
    uint64 new_hash;

    old_hash = rapidhash(old_path, (size_t)old_path_len);
    new_hash = rapidhash(new_path, (size_t)new_path_len);

    list->aggregate_hash ^= old_hash;
    list->aggregate_hash ^= new_hash;

    for (int32 i = 0; i < list->count; i += 1) {
        if (list->names_lens[i] == old_path_len) {
            if (memcmp64(list->names[i], old_path, old_path_len) == 0) {
                list->names[i] = new_path;
                list->names_lens[i] = new_path_len;
                break;
            }
        }
    }

    return;
}

static HardLinks *
hard_link_remove(HardLinks *list, char *name, int32 name_len) {
    uint64 hash_val;
    int32 found_idx = -1;

    ASSERT(list);

    hash_val = rapidhash(name, (size_t)name_len);

    for (int32 i = 0; i < list->count; i += 1) {
        if (list->names_lens[i] == name_len) {
            if (memcmp64(list->names[i], name, name_len) == 0) {
                found_idx = i;
                break;
            }
        }
    }

    if (found_idx >= 0) {
        for (int32 i = found_idx; i < (list->count - 1); i += 1) {
            list->names[i] = list->names[i + 1];
            list->names_lens[i] = list->names_lens[i + 1];
        }
        list->count -= 1;
        list->aggregate_hash ^= hash_val;
    }

    if (list->count == 0) {
        return NULL;
    }

    return list;
}

static bool
hard_links_match(HardLinks *src, HardLinks *dst) {
    ASSERT(src);
    ASSERT(dst);

    if (src->count != dst->count) {
        return false;
    }

    if (src->aggregate_hash != dst->aggregate_hash) {
        return false;
    }

    return true;
}

static void
hard_link_copy_free(HardLinks *copy) {
    ASSERT(copy);

    for (int32 i = 0; i < copy->count; i += 1) {
        free(copy->names[i], copy->names_lens[i] + 1);
    }

    free(copy->names, copy->capacity * SIZEOF(*copy->names));
    free(copy, SIZEOF(*copy));

    return;
}

static char *
item_path_get(int32 row_id) {
    int32 src_idx = cecup.rows[L][row_id];
    int32 dst_idx = cecup.rows[R][row_id];

    if (src_idx >= 0) {
        return cecup.traversal[L].paths[src_idx];
    } else if (dst_idx >= 0) {
        return cecup.traversal[R].paths[dst_idx];
    } else {
        error("Error: both source and destination have invalid indices.\n");
        fatal(EXIT_FAILURE);
    }
}

static int32
item_path_len_get(int32 row_id) {
    int32 src_idx = cecup.rows[L][row_id];
    int32 dst_idx = cecup.rows[R][row_id];

    if (src_idx >= 0) {
        return cecup.traversal[L].paths_lens[src_idx];
    }
    if (dst_idx >= 0) {
        return cecup.traversal[R].paths_lens[dst_idx];
    } else {
        error("Error: both source and destination have invalid indices.\n");
        fatal(EXIT_FAILURE);
    }
}

INLINE char *
item_path_side(int32 row_id, int32 side) {
    int32 idx;

    if ((idx = cecup.rows[side][row_id]) >= 0) {
        return cecup.traversal[side].paths[idx];
    } else {
        return NULL;
    }
}

static int64
item_size_side(int32 row_id, int32 side) {
    int32 idx;

    if ((idx = cecup.rows[side][row_id]) >= 0) {
        return cecup.traversal[side].stats[idx].st_size;
    } else {
        return -1;
    }
}

static int64
item_mtime_side(int32 row_id, int32 side) {
    int32 idx;

    if ((idx = cecup.rows[side][row_id]) >= 0) {
        return cecup.traversal[side].stats[idx].st_mtime;
    } else {
        return 0;
    }
}

static char *
item_ignore_pattern_side(int32 row_id, int32 side) {
    int32 idx;

    if ((idx = cecup.rows[side][row_id]) >= 0) {
        return cecup.traversal[side].patterns[idx];
    } else {
        return NULL;
    }
}

static int32
item_path_len_side(int32 row_id, int32 side) {
    int32 idx;

    if ((idx = cecup.rows[side][row_id]) >= 0) {
        return (int32)cecup.traversal[side].paths_lens[idx];
    } else {
        return 0;
    }
}

static char *
item_symlink_target_side(int32 row_id, int32 side) {
    int32 idx;

    if ((idx = cecup.rows[side][row_id]) >= 0) {
        return cecup.traversal[side].symlink_targets[idx];
    } else {
        return NULL;
    }
}

static bool
item_hardlink_side(int32 row_id, int32 side, HardLinks *hard_links) {
    int32 idx;

    if ((idx = cecup.rows[side][row_id]) < 0) {
        return false;
    }

    if (cecup.traversal[side].stats[idx].st_nlink <= 1) {
        return false;
    }

    if (!hash_lookup_inode_map(cecup.traversal[side].inode_map,
                               &cecup.traversal[side].stats[idx].st_ino, hard_links)) {
        return false;
    }

    return true;
}

static int32
item_symlink_target_len_side(int32 row_id, int32 side) {
    int32 idx;

    if ((idx = cecup.rows[side][row_id]) >= 0) {
        return (int32)cecup.traversal[side].symlink_targets_lens[idx];
    } else {
        return 0;
    }
}

static void
item_get_actions_reasons(int32 row_id,
                         enum Action *action_src, enum Action *action_dst, enum Reason *reason) {
    int32 src_idx = cecup.rows[L][row_id];
    int32 dst_idx = cecup.rows[R][row_id];
    bool delete_ignored = cecup.delete_ignored;
    bool delete_after = cecup.delete_after;

    *reason = 0;

    if ((cecup.traversal[L].stats == NULL)
         || (cecup.traversal[R].stats == NULL)) {
        error("Function %s called while the traversal stats array is null.", __func__);
        error("This probably means that there is some race condition.\n");
        error("Or another bug.\n");
        fatal(EXIT_FAILURE);
    }

    if (src_idx < 0) {
        char *matched_dst;
        matched_dst = cecup.traversal[R].patterns[dst_idx];
        *reason |= REASON_MISSING;

        if (matched_dst) {
            *reason |= REASON_IGNORED;
        }

        *action_src = ACTION_IGNORE;

        if (delete_after || ((*reason & REASON_IGNORED) && delete_ignored)) {
            *action_dst = ACTION_DELETE;
        } else {
            *action_dst = ACTION_IGNORE;
        }

        return;
    }

    if (dst_idx < 0) {
        char *pattern_src = cecup.traversal[L].patterns[src_idx];
        struct stat *stat_src = &cecup.traversal[L].stats[src_idx];
        bool is_symlink = S_ISLNK(stat_src->st_mode);
        HardLinks hard_links;
        bool is_hardlink = item_hardlink_side(row_id, L, &hard_links);

        if (pattern_src) {
            *action_src = ACTION_IGNORE;
            *action_dst = ACTION_IGNORE;
            *reason |= REASON_IGNORED;
            return;
        }

        *reason |= REASON_NEW;

        if (is_hardlink) {
            *action_src = ACTION_HARDLINK;
            *action_dst = ACTION_HARDLINK;
            *reason |= REASON_HARDLINK;
        } else if (is_symlink) {
            *action_src = ACTION_SYMLINK;
            *action_dst = ACTION_SYMLINK;
            *reason |= REASON_SYMLINK;
        } else {
            *action_src = ACTION_NEW;
            *action_dst = ACTION_NEW;
        }

        return;
    }

    {
        struct stat *stat_src = &cecup.traversal[L].stats[src_idx];
        struct stat *stat_dst = &cecup.traversal[R].stats[dst_idx];
        char *pattern_src = cecup.traversal[L].patterns[src_idx];
        char *path_src = cecup.traversal[L].paths[src_idx];
        char *symlink_target_src = cecup.traversal[L].symlink_targets[src_idx];
        char *symlink_target_dst = cecup.traversal[R].symlink_targets[dst_idx];
        HardLinks hard_links_src = {0};
        HardLinks hard_links_dst = {0};
        bool is_hardlink;
        bool is_symlink = S_ISLNK(stat_src->st_mode);
        bool is_dir = S_ISDIR(stat_src->st_mode);
        bool equal = false;
        bool attributes_differ = false;

        is_hardlink = item_hardlink_side(row_id, L, &hard_links_src);
        item_hardlink_side(row_id, R, &hard_links_dst);

        if (pattern_src) {
            *action_src = ACTION_IGNORE;
            *reason |= REASON_IGNORED;

            if (delete_ignored) {
                *action_dst = ACTION_DELETE;
            } else {
                *action_dst = ACTION_IGNORE;
            }

            return;
        }

        if (is_symlink) {
            *reason |= REASON_SYMLINK;

            if (S_ISLNK(stat_dst->st_mode)) {
                if (symlink_target_src && symlink_target_dst) {
                    if (strcmp(symlink_target_src, symlink_target_dst) == 0) {
                        equal = true;
                    } else {
                        *reason |= REASON_SYMLINK_NOT_MATCH;
                    }
                } else {
                    *reason |= REASON_SYMLINK_NO_TARGET;
                }
            } else {
                *reason |= REASON_SYMLINK_NOT;
            }
        } else {
            if (is_hardlink) {
                *reason |= REASON_HARDLINK;
            }

            if (!is_dir) {
                if (stat_src->st_size != stat_dst->st_size) {
                    *reason |= REASON_SIZE;
                    attributes_differ = true;
                }
            }

            if (stat_src->st_mtime > stat_dst->st_mtime) {
                *reason |= REASON_MTIME_NEWER;
                attributes_differ = true;
            }

            if (stat_src->st_mtime < stat_dst->st_mtime) {
                *reason |= REASON_MTIME_OLDER;
                if (delete_after) {
                    attributes_differ = true;
                } else {
                    *action_src = ACTION_IGNORE;
                    *action_dst = ACTION_IGNORE;
                    return;
                }
            }

            if (is_dir) {
                if (stat_src->st_ctime > stat_dst->st_ctime) {
                    *reason |= REASON_CTIME;
                    attributes_differ = true;
                }
            }

            if (stat_src->st_uid != stat_dst->st_uid) {
                *reason |= REASON_OWNER;
                attributes_differ = true;
            }

            if (stat_src->st_gid != stat_dst->st_gid) {
                *reason |= REASON_GROUP;
                attributes_differ = true;
            }

            if ((stat_src->st_mode & 07777) != (stat_dst->st_mode & 07777)) {
                *reason |= REASON_PERM;
                attributes_differ = true;
            }

            ASSERT(path_src);
            if (is_hardlink) {
                if (hard_links_dst.count == 0) {
                    equal = false;
                    attributes_differ = true;
                    *reason |= REASON_HARDLINK_MISSING_LINK;
                } else if (!hard_links_match(&hard_links_src, &hard_links_dst)) {
                    equal = false;
                    attributes_differ = true;
                    *reason |= REASON_HARDLINK_NOT_MATCH;
                }
            }

            if (!attributes_differ) {
                equal = true;
            }
        }

        if (equal) {
            *action_src = ACTION_EQUAL;
            *action_dst = ACTION_EQUAL;
            *reason |= REASON_EQUAL;
        } else {
            if (is_hardlink) {
                *action_src = ACTION_HARDLINK;
                *action_dst = ACTION_HARDLINK;
            } else if (is_symlink) {
                *action_src = ACTION_SYMLINK;
                *action_dst = ACTION_SYMLINK;
            } else {
                *action_src = ACTION_UPDATE;
                *action_dst = ACTION_UPDATE;
            }
        }
    }

    return;
}

#define COMPARE(A, B) \
    do { \
        if (A > B) { \
            result = 1; \
        } else if (A < B) { \
            result = -1; \
        } else { \
            result = 0; \
        } \
    } while (0)

INLINE int32
cecup_item_compare_string_key(const void *a, const void *b) {
    SortEntry *entry_a = (SortEntry *)a;
    SortEntry *entry_b = (SortEntry *)b;
    int32 result;
    char *path_a = entry_a->key.ptr;
    char *path_b = entry_b->key.ptr;

    /* this assumes that paths are set to = "" if they are NULL from the list
     * see update_rows_from_list() */
    result = strcmp(path_a, path_b);

    if (cecup.sort_order == GTK_SORT_DESCENDING) {
        result *= -1;
    }

    return result;
}

INLINE int32
cecup_item_compare_int_key(const void *a, const void *b) {
    SortEntry *entry_a = (SortEntry *)a;
    SortEntry *entry_b = (SortEntry *)b;
    int32 result = 0;
    int64 key_a = entry_a->key.i64;
    int64 key_b = entry_b->key.i64;

    COMPARE(key_a, key_b);

    if (cecup.sort_order == GTK_SORT_DESCENDING) {
        result *= -1;
    }

    return result;
}

#undef COMPARE

typedef void(*SortFunction)(SortEntry *a, int64);
typedef int(*CompareFunction)(const void *a, const void *b);

#define i_key SortEntry
#define i_cmp(a,b) cecup_item_compare_string_key(a,b)
#define T compare_string
#include "stc/sort.h"

#define i_key SortEntry
#define i_cmp(a,b) cecup_item_compare_int_key(a,b)
#define T compare_int
#include "stc/sort.h"

static SortFunction sort_item_functions[] = {
    [COL_SELECTED]   = compare_int_sort,
    [COL_SRC_ACTION] = compare_int_sort,
    [COL_DST_ACTION] = compare_int_sort,
    [COL_SRC_PATH]   = compare_string_sort,
    [COL_DST_PATH]   = compare_string_sort,
    [COL_SIZE_TEXT]  = compare_int_sort,
    [COL_SIZE_RAW]   = compare_int_sort,
    [COL_MTIME_TEXT] = compare_int_sort,
    [COL_MTIME_RAW]  = compare_int_sort,
    [COL_ROW_ID]     = compare_int_sort,
    [NUM_COLS]       = compare_int_sort,
};

static CompareFunction compare_item_functions[] = {
    [COL_SELECTED]   = cecup_item_compare_int_key,
    [COL_SRC_ACTION] = cecup_item_compare_int_key,
    [COL_DST_ACTION] = cecup_item_compare_int_key,
    [COL_SRC_PATH]   = cecup_item_compare_string_key,
    [COL_DST_PATH]   = cecup_item_compare_string_key,
    [COL_SIZE_TEXT]  = cecup_item_compare_int_key,
    [COL_SIZE_RAW]   = cecup_item_compare_int_key,
    [COL_MTIME_TEXT] = cecup_item_compare_int_key,
    [COL_MTIME_RAW]  = cecup_item_compare_int_key,
    [COL_ROW_ID]     = cecup_item_compare_int_key,
    [NUM_COLS]       = cecup_item_compare_int_key,
};

#if (0 == TESTING_item) && TESTING
static inline void
item_functions_sink(void) {
    (void)item_functions_sink;
    (void)item_ignore_pattern_side;
}
#endif

#if TESTING_item

#include "list_model.c"
#include "update.c"
#include "work.c"
#include "on.c"
#include "assert.c"

int
main(void) {
    HardLinks hl1;
    HardLinks hl2;
    HardLinks hl3;
    char *names[3];
    int32 names_lens[3];
    char *name_to_remove;
    int32 name_to_remove_len;
    HardLinks *ret;
    SortEntry entry1;
    SortEntry entry2;
    int32 result;

    memset64(&hl1, 0, SIZEOF(hl1));
    memset64(&hl2, 0, SIZEOF(hl2));

    hl1.count = 2;
    hl1.aggregate_hash = 12345;

    hl2.count = 2;
    hl2.aggregate_hash = 12345;

    ASSERT(hard_links_match(&hl1, &hl2));

    hl2.aggregate_hash = 54321;
    ASSERT(!hard_links_match(&hl1, &hl2));

    names[0] = "file1.txt";
    names[1] = "file2.txt";
    names[2] = "file3.txt";

    names_lens[0] = strlen32(names[0]);
    names_lens[1] = strlen32(names[1]);
    names_lens[2] = strlen32(names[2]);

    memset64(&hl3, 0, SIZEOF(hl3));
    hl3.count = 3;
    hl3.capacity = 3;
    hl3.names = names;
    hl3.names_lens = names_lens;
    hl3.aggregate_hash = rapidhash(names[0], names_lens[0]) ^ rapidhash(names[1], names_lens[1]) ^ rapidhash(names[2], names_lens[2]);

    name_to_remove = "file2.txt";
    name_to_remove_len = strlen32(name_to_remove);

    ret = hard_link_remove(&hl3, name_to_remove, name_to_remove_len);
    ASSERT(ret != NULL);
    ASSERT_EQUAL(hl3.count, 2);
    ASSERT_EQUAL(hl3.names[0], "file1.txt");
    ASSERT_EQUAL(hl3.names[1], "file3.txt");

    memset64(&entry1, 0, SIZEOF(entry1));
    memset64(&entry2, 0, SIZEOF(entry2));

    entry1.key.i64 = 10;
    entry2.key.i64 = 20;

    cecup.sort_order = GTK_SORT_ASCENDING;
    result = cecup_item_compare_int_key(&entry1, &entry2);
    ASSERT_LESS(result, 0);

    cecup.sort_order = GTK_SORT_DESCENDING;
    result = cecup_item_compare_int_key(&entry1, &entry2);
    ASSERT_MORE(result, 0);

    entry1.key.ptr = "apple";
    entry2.key.ptr = "banana";

    cecup.sort_order = GTK_SORT_ASCENDING;
    result = cecup_item_compare_string_key(&entry1, &entry2);
    ASSERT_LESS(result, 0);

    cecup.sort_order = GTK_SORT_DESCENDING;
    result = cecup_item_compare_string_key(&entry1, &entry2);
    ASSERT_MORE(result, 0);

    exit(EXIT_SUCCESS);
}
#endif

#endif /* ITEM_C */
