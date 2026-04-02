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

static char *
item_path_get(int32 row_id) {
    int32 src_idx;
    int32 dst_idx;

    src_idx = cecup.rows[L][row_id];
    dst_idx = cecup.rows[R][row_id];

    if (src_idx >= 0) {
        return cecup.traversal[L].paths[src_idx];
    } else if (dst_idx >= 0) {
        return cecup.traversal[R].paths[dst_idx];
    } else {
        error("Error: src_path and dst_path are NULL.\n");
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
    }
    return 0;
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
item_link_target_side(int32 row_id, int32 side) {
    int32 idx;

    if ((idx = cecup.rows[side][row_id]) >= 0) {
        return cecup.traversal[side].link_targets[idx];
    } else {
        return NULL;
    }
}

static int32
item_link_target_len_side(int32 row_id, int32 side) {
    int32 idx;

    if ((idx = cecup.rows[side][row_id]) >= 0) {
        return (int32)cecup.traversal[side].link_targets_lens[idx];
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
        bool is_hardlink = S_ISREG(stat_src->st_mode) && (cecup.traversal[L].nlinks[src_idx] > 1);

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
        char *target_src = cecup.traversal[L].link_targets[src_idx];
        char *target_dst = cecup.traversal[R].link_targets[dst_idx];
        int32 nlinks_src = cecup.traversal[L].nlinks[src_idx];
        int32 nlinks_dst = cecup.traversal[R].nlinks[dst_idx];
        bool is_symlink = S_ISLNK(stat_src->st_mode);
        bool is_hardlink = S_ISREG(stat_src->st_mode) && (cecup.traversal[L].nlinks[src_idx] > 1);
        bool is_dir = S_ISDIR(stat_src->st_mode);
        bool equal = false;
        bool attributes_differ = false;

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
                if (target_src && target_dst) {
                    if (strcmp(target_src, target_dst) == 0) {
                        equal = true;
                    }
                }
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

            if (is_hardlink) {
                if (target_dst == NULL) {
                    equal = false;
                    attributes_differ = true;
                    *reason |= REASON_HARDLINK_MISSING_LINK;
                } else if (
                        ((target_src == NULL) || strcmp(target_src, target_dst))
                        && strcmp(path_src, target_dst)
                        && (nlinks_src != nlinks_dst)
                        ) {
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
    int32 result = 0;
    char *path_a = entry_a->key.ptr;
    char *path_b = entry_b->key.ptr;

    if (path_a == NULL) {
        result = -1;
    } else if (path_b == NULL) {
        result = 1;
    } else {
        result = strcmp(path_a, path_b);
    }

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

#define i_key SortEntry
#define i_cmp(a,b) cecup_item_compare_string_key(a,b)
#define T compare_string
#include "stc/sort.h"

#define i_key SortEntry
#define i_cmp(a,b) cecup_item_compare_int_key(a,b)
#define T compare_int
#include "stc/sort.h"

static SortFunction compare_item_functions[] = {
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

int
main(void) {
    return 0;
}
#endif

#endif /* ITEM_C */
