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

static char *
item_path_get(int32 row_id) {
    int32 src_idx;
    int32 dst_idx;

    src_idx = cecup.rows_src[row_id];
    dst_idx = cecup.rows_dst[row_id];

    if (src_idx >= 0) {
        return cecup.traversal_src.paths[src_idx];
    } else if (dst_idx >= 0) {
        return cecup.traversal_dst.paths[dst_idx];
    } else {
        error("Error: src_path and dst_path are NULL.\n");
        fatal(EXIT_FAILURE);
    }
}

static int32
item_path_len_get(int32 row_id) {
    int32 src_idx;
    int32 dst_idx;

    src_idx = cecup.rows_src[row_id];
    dst_idx = cecup.rows_dst[row_id];

    if (src_idx >= 0) {
        return cecup.traversal_src.paths_lens[src_idx];
    }
    if (dst_idx >= 0) {
        return cecup.traversal_dst.paths_lens[dst_idx];
    }
    return 0;
}

static char *
item_path_side(int32 row_id, int32 side) {
    int32 idx;

    if (side == L) {
        idx = cecup.rows_src[row_id];
        if (idx >= 0) {
            return cecup.traversal_src.paths[idx];
        }
    } else {
        idx = cecup.rows_dst[row_id];
        if (idx >= 0) {
            return cecup.traversal_dst.paths[idx];
        }
    }
    return NULL;
}

static int64
item_size_side(int32 row_id, int32 side) {
    int32 idx;

    if (side == L) {
        idx = cecup.rows_src[row_id];
        if (idx >= 0) {
            if (!S_ISDIR(cecup.traversal_src.stats[idx].st_mode)) {
                return cecup.traversal_src.stats[idx].st_size;
            }
        }
    } else {
        idx = cecup.rows_dst[row_id];
        if (idx >= 0) {
            if (!S_ISDIR(cecup.traversal_dst.stats[idx].st_mode)) {
                return cecup.traversal_dst.stats[idx].st_size;
            }
        }
    }
    return -1;
}

static int64
item_mtime_side(int32 row_id, int32 side) {
    int32 idx;

    if (side == L) {
        idx = cecup.rows_src[row_id];
        if (idx >= 0) {
            return cecup.traversal_src.stats[idx].st_mtime;
        }
    } else {
        idx = cecup.rows_dst[row_id];
        if (idx >= 0) {
            return cecup.traversal_dst.stats[idx].st_mtime;
        }
    }
    return 0;
}

static char *
item_ignore_pattern_side(int32 row_id, int32 side) {
    int32 idx;

    if (side == L) {
        idx = cecup.rows_src[row_id];
        if (idx >= 0) {
            return cecup.traversal_src.patterns[idx];
        }
    } else {
        idx = cecup.rows_dst[row_id];
        if (idx >= 0) {
            return cecup.traversal_dst.patterns[idx];
        }
    }
    return NULL;
}

static int32
item_path_len_side(int32 row_id, int32 side) {
    int32 idx;

    if (side == L) {
        idx = cecup.rows_src[row_id];
        if (idx >= 0) {
            return (int32)cecup.traversal_src.paths_lens[idx];
        }
    } else {
        idx = cecup.rows_dst[row_id];
        if (idx >= 0) {
            return (int32)cecup.traversal_dst.paths_lens[idx];
        }
    }
    return 0;
}

static char *
item_link_target_side(int32 row_id, int32 side) {
    int32 idx;

    if (side == L) {
        idx = cecup.rows_src[row_id];
        if (idx >= 0) {
            return cecup.traversal_src.link_targets[idx];
        }
    } else {
        idx = cecup.rows_dst[row_id];
        if (idx >= 0) {
            return cecup.traversal_dst.link_targets[idx];
        }
    }
    return NULL;
}

static int32
item_link_target_len_side(int32 row_id, int32 side) {
    int32 idx;

    if (side == L) {
        idx = cecup.rows_src[row_id];
        if (idx >= 0) {
            return (int32)cecup.traversal_src.link_targets_lens[idx];
        }
    } else {
        idx = cecup.rows_dst[row_id];
        if (idx >= 0) {
            return (int32)cecup.traversal_dst.link_targets_lens[idx];
        }
    }
    return 0;
}

static void
item_get_actions_reasons(int32 row_id,
                         enum Action *action_src, enum Action *action_dst, enum Reason *reason) {
    int32 src_idx = cecup.rows_src[row_id];
    int32 dst_idx = cecup.rows_dst[row_id];
    bool delete_ignored = cecup.delete_ignored;
    bool delete_after = cecup.delete_after;

    *reason = 0;

    if ((cecup.traversal_src.stats == NULL)
         || (cecup.traversal_dst.stats == NULL)) {
        error("Function %s called while the traversal stats array is null.", __func__);
        error("This probably means that there is some race condition.\n");
        error("Or another bug.\n");
        fatal(EXIT_FAILURE);
    }

    if (src_idx < 0) {
        char *matched_dst;
        matched_dst = cecup.traversal_dst.patterns[dst_idx];
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
        char *pattern_src = cecup.traversal_src.patterns[src_idx];
        struct stat *stat_src = &cecup.traversal_src.stats[src_idx];
        bool is_symlink = S_ISLNK(stat_src->st_mode);
        bool is_hardlink = cecup.traversal_src.nlinks[src_idx] > 1;

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
        struct stat *stat_src = &cecup.traversal_src.stats[src_idx];
        struct stat *stat_dst = &cecup.traversal_dst.stats[dst_idx];
        char *pattern_src = cecup.traversal_src.patterns[src_idx];
        char *path_src = cecup.traversal_src.paths[src_idx];
        char *target_src = cecup.traversal_src.link_targets[src_idx];
        char *target_dst = cecup.traversal_dst.link_targets[dst_idx];
        int32 nlinks_src = cecup.traversal_src.nlinks[src_idx];
        int32 nlinks_dst = cecup.traversal_dst.nlinks[dst_idx];
        bool is_symlink = S_ISLNK(stat_src->st_mode);
        bool is_hardlink = cecup.traversal_src.nlinks[src_idx] > 1;
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

static int32
cecup_item_compare(const void *a, const void *b) {
    int32 item_a;
    int32 item_b;
    int64 result;
    char *path_a;
    char *path_b;
    int64 size_a;
    int64 size_b;
    int64 mtime_a;
    int64 mtime_b;
    enum Action src_act_a;
    enum Action dst_act_a;
    enum Action src_act_b;
    enum Action dst_act_b;
    enum Reason reason_a;
    enum Reason reason_b;

    item_a = *(int32 *)a;
    item_b = *(int32 *)b;

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

    switch (cecup.sort_col) {
    case COL_SRC_PATH:
        path_a = item_path_side(item_a, L);
        path_b = item_path_side(item_b, L);
        if (path_a == NULL && path_b == NULL) {
            result = 0;
        } else if (path_a == NULL) {
            result = -1;
        } else if (path_b == NULL) {
            result = 1;
        } else {
            result = strcmp(path_a, path_b);
        }
        break;
    case COL_DST_PATH:
        path_a = item_path_side(item_a, R);
        path_b = item_path_side(item_b, R);
        if (path_a == NULL && path_b == NULL) {
            result = 0;
        } else if (path_a == NULL) {
            result = -1;
        } else if (path_b == NULL) {
            result = 1;
        } else {
            result = strcmp(path_a, path_b);
        }
        break;
    case COL_SIZE_RAW:
        size_a = item_size_side(item_a, L);
        size_b = item_size_side(item_b, L);
        COMPARE(size_a, size_b);
        break;
    case COL_MTIME_RAW:
        mtime_a = item_mtime_side(item_a, L);
        mtime_b = item_mtime_side(item_b, L);
        COMPARE(mtime_a, mtime_b);
        break;
    case COL_DST_ACTION:
        item_get_actions_reasons(item_a, &src_act_a, &dst_act_a, &reason_a);
        item_get_actions_reasons(item_b, &src_act_b, &dst_act_b, &reason_b);
        COMPARE(dst_act_a, dst_act_b);
        break;
    case COL_MTIME_TEXT:
    case COL_ROW_ID:
    case COL_SELECTED:
    case COL_SIZE_TEXT:
    case COL_SRC_ACTION:
    case NUM_COLS:
    default:
        item_get_actions_reasons(item_a, &src_act_a, &dst_act_a, &reason_a);
        item_get_actions_reasons(item_b, &src_act_b, &dst_act_b, &reason_b);
        COMPARE(src_act_a, src_act_b);
        break;
    }

#undef COMPARE

    if (cecup.sort_order == GTK_SORT_DESCENDING) {
        result *= -1;
    }

    return (int32)result;
}

#if TESTING && (0 == TESTING_item)
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
