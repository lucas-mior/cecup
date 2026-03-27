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

#if !defined(UPDATE_C)
#define UPDATE_C

#include <gtk/gtk.h>
#include <glib/gmain.h>
#include <stdlib.h>

#include "util.c"
#include "i18n.h"

#include "cecup.h"
#include "aux.c"
#include "tree_model.c"
#include "ignore_patterns.c"

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_update 1
#elif !defined(TESTING_update)
#define TESTING_update 0
#endif

#define UI_INTERVAL_MS 100

static void update_list_from_rows(void);

static void
update_row_remove(Message *message) {
    char *pattern;
    int32 pattern_len;
    int32 deleted_side;

    pattern = message->src_path;
    pattern_len = message->path_len;
    deleted_side = message->side;

    for (int32 i = 0; i < cecup.rows_len;) {
        CecupItem *item = cecup.rows[i];
        char *path_test = item_path_get(item);
        int32 path_test_len = item_path_len_get(item);
        bool match = false;

        if (pattern[pattern_len - 1] == '/') {
            if (BEGINS_WITH(path_test, pattern, pattern_len)) {
                match = true;
            }
        } if (path_test_len == pattern_len) {
            if (!memcmp64(path_test, pattern, pattern_len)) {
                match = true;
            }
        }

        if (!match) {
            i += 1;
            continue;
        }

        bool remove_entirely;

        remove_entirely = false;

        if (deleted_side == L) {
            if (item->src_idx >= 0) {
                Traversal *traversal;

                traversal = &cecup.traversal_src;

                if (traversal->inode_map) {
                    if (S_ISREG(traversal->stats[item->src_idx].st_mode)) {
                        if (traversal->stats[item->src_idx].st_nlink > 1) {
                            char inode_str[64];
                            int32 n;

                            n = itoa2(inode_str, (long)traversal->stats[item->src_idx].st_ino);
                            hash_remove_inode_map(traversal->inode_map, inode_str, n);
                        }
                    }
                }

                hash_remove_fs_map(traversal->map, traversal->paths[item->src_idx],
                                   traversal->paths_lens[item->src_idx]);
                memset64(&traversal->stats[item->src_idx], 0, SIZEOF(struct stat));
                item->src_idx = -1;
            }
        } else {
            if (item->dst_idx >= 0) {
                Traversal *traversal;

                traversal = &cecup.traversal_dst;

                if (traversal->inode_map) {
                    if (S_ISREG(traversal->stats[item->dst_idx].st_mode)) {
                        if (traversal->stats[item->dst_idx].st_nlink > 1) {
                            char inode_str[64];
                            int32 n;

                            n = itoa2(inode_str, (long)traversal->stats[item->dst_idx].st_ino);
                            hash_remove_inode_map(traversal->inode_map, inode_str, n);
                        }
                    }
                }

                hash_remove_fs_map(traversal->map, traversal->paths[item->dst_idx],
                                   traversal->paths_lens[item->dst_idx]);
                memset64(&traversal->stats[item->dst_idx], 0, SIZEOF(struct stat));
                item->dst_idx = -1;
            }
        }

        if ((item->src_idx == -1) && (item->dst_idx == -1)) {
            remove_entirely = true;
        }

        if (remove_entirely) {
            LOG(_("Removing %s entirely from list...\n"), path_test);

            for (int32 k = 0; k < cecup.rows_visible_len; k += 1) {
                if (cecup.rows_visible[k] == item) {
                    for (int32 j = k; j < (cecup.rows_visible_len - 1); j += 1) {
                        cecup.rows_visible[j] = cecup.rows_visible[j + 1];
                    }
                    cecup.rows_visible_len -= 1;

                    cecup_list_model_row_removed(CECUP_LIST_MODEL(cecup.store), k);
                    break;
                }
            }

            for (int32 j = i; j < (cecup.rows_len - 1); j += 1) {
                cecup.rows[j] = cecup.rows[j + 1];
            }
            cecup.rows_len -= 1;
        } else {
            LOG(_("Updated %s state (missing on side %d)\n"),
                path_test, deleted_side);
            for (int32 k = 0; k < cecup.rows_visible_len; k += 1) {
                if (cecup.rows_visible[k] == item) {
                    cecup_list_model_row_changed(CECUP_LIST_MODEL(cecup.store), k);
                    break;
                }
            }
            i += 1;
        }
    }

    return;
}

static void
update_row_transfer(Message *message) {
    char *pattern;
    int32 pattern_len;

    pattern = message->src_path;
    pattern_len = message->path_len;

    for (int32 i = 0; i < cecup.rows_len; i += 1) {
        CecupItem *item = cecup.rows[i];
        char *path_test = item_path_get(item);
        int32 path_test_len = item_path_len_get(item);
        bool show_equal;
        char full_path[MAX_PATH_LENGTH];
        int32 *idx_ptr;

        if (path_test_len != pattern_len) {
            continue;
        }
        if (memcmp64(path_test, pattern, pattern_len)) {
            continue;
        }

        SNPRINTF(full_path, "%s/%s", cecup.dst_base, path_test);

        if ((idx_ptr
             = hash_lookup_fs_map(cecup.traversal_dst.map,
                                  path_test, pattern_len))) {
            int32 idx = *idx_ptr;
            struct stat *stat = &cecup.traversal_dst.stats[idx];
            int32 retry_count = 0;
            bool success = false;

            while (retry_count < 10) {
                if (lstat(full_path, stat) == 0) {
                    success = true;
                    break;
                }
                usleep(50000);
                retry_count += 1;
            }

            if (!success) {
                LOG_ERROR("Error in lstat(%s) after retries: %s.\n",
                          full_path, strerror(errno));
            } else if (S_ISLNK(stat->st_mode)) {
                char target[MAX_PATH_LENGTH];
                int64 target_len;

                if ((target_len = readlink(full_path, target, SIZEOF(target))) > 0) {
                    target[target_len] = '\0';
                    if (cecup.traversal_dst.link_targets[idx]) {
                        free(cecup.traversal_dst.link_targets[idx],
                             cecup.traversal_dst.link_targets_lens[idx] + 1);
                    }
                    cecup.traversal_dst.link_targets[idx] = xmemdup(target, target_len + 1);
                    cecup.traversal_dst.link_targets_lens[idx] = (int16)target_len;
                }
            } else if (S_ISREG(stat->st_mode) && (stat->st_nlink > 1)) {
                char inode_str[64];
                int32 n;
                int32 *first_idx_ptr;

                n = itoa2(inode_str, (long)stat->st_ino);
                first_idx_ptr = hash_lookup_inode_map(cecup.traversal_dst.inode_map,
                                                      inode_str, n);
                if (first_idx_ptr) {
                    int32 first_idx;

                    first_idx = *first_idx_ptr;
                    if (first_idx != idx) {
                        cecup.traversal_dst.link_targets[idx]
                            = cecup.traversal_dst.paths[first_idx];
                        cecup.traversal_dst.link_targets_lens[idx]
                            = cecup.traversal_dst.paths_lens[first_idx];
                    }
                } else {
                    hash_insert_inode_map(cecup.traversal_dst.inode_map,
                                          inode_str, n, idx);
                }
            }
        } else {
            struct stat stat;
            char *path_copy;
            int32 retry_count = 0;
            bool success = false;

            while (retry_count < 10) {
                if (lstat(full_path, &stat) == 0) {
                    success = true;
                    break;
                }
                usleep(50000);
                retry_count += 1;
            }

            if (!success) {
                LOG_ERROR("Error in lstat(%s) after retries: %s.\n",
                          full_path, strerror(errno));
            } else {
                char *link_target;
                int32 link_target_len;

                link_target = NULL;
                link_target_len = 0;

                if (S_ISLNK(stat.st_mode)) {
                    char target[MAX_PATH_LENGTH];
                    int64 target_len;

                    if ((target_len = readlink(full_path, target, SIZEOF(target))) > 0) {
                        target[target_len] = '\0';
                        link_target = xmemdup(target, target_len + 1);
                        link_target_len = (int32)target_len;
                    }
                } else if (S_ISREG(stat.st_mode) && (stat.st_nlink > 1)) {
                    char inode_str[64];
                    int32 n;
                    int32 *first_idx_ptr;

                    n = itoa2(inode_str, (long)stat.st_ino);
                    first_idx_ptr = hash_lookup_inode_map(cecup.traversal_dst.inode_map,
                                                          inode_str, n);
                    if (first_idx_ptr) {
                        int32 first_idx;

                        first_idx = *first_idx_ptr;
                        link_target = cecup.traversal_dst.paths[first_idx];
                        link_target_len = cecup.traversal_dst.paths_lens[first_idx];
                    } else {
                        hash_insert_inode_map(cecup.traversal_dst.inode_map,
                                              inode_str, n, cecup.traversal_dst.nfiles);
                    }
                }

                path_copy = xmemdup(path_test, pattern_len + 1);
                traversal_push(&cecup.traversal_dst,
                               path_copy, pattern_len,
                               &stat,
                               link_target, link_target_len,
                               NULL, 0);

                item->dst_idx = cecup.traversal_dst.nfiles - 1;
            }
        }

        show_equal = gtk_toggle_button_get_active(
            GTK_TOGGLE_BUTTON(cecup.filter_equal));

        if (show_equal) {
            for (int32 k = 0; k < cecup.rows_visible_len; k += 1) {
                if (cecup.rows_visible[k] == item) {
                    cecup_list_model_row_changed(CECUP_LIST_MODEL(cecup.store),
                                                 k);
                    break;
                }
            }
        } else {
            for (int32 k = 0; k < cecup.rows_visible_len; k += 1) {
                if (cecup.rows_visible[k] == item) {
                    for (int32 j = k; j < (cecup.rows_visible_len - 1); j += 1) {
                        cecup.rows_visible[j] = cecup.rows_visible[j + 1];
                    }
                    cecup.rows_visible_len -= 1;

                    cecup_list_model_row_removed(CECUP_LIST_MODEL(cecup.store),
                                                 k);
                    break;
                }
            }
        }
        break;
    }
    return;
}

static void
update_row_rename(Message *message) {
    char *old_path;
    char *new_path;
    int32 old_path_len;
    int32 new_path_len;
    int32 side;
    Traversal *traversal;
    int32 original_len;

    old_path = message->old_path;
    new_path = message->new_path;
    old_path_len = message->old_path_len;
    new_path_len = message->new_path_len;
    side = message->side;

    if (side == L) {
        traversal = &cecup.traversal_src;
    } else {
        traversal = &cecup.traversal_dst;
    }

    for (int32 i = 0; i < traversal->nfiles; i += 1) {
        char *path;
        int32 path_len;
        bool match;

        path = traversal->paths[i];
        path_len = traversal->paths_lens[i];
        match = false;

        if (old_path[old_path_len - 1] == '/') {
            if (BEGINS_WITH(path, old_path, old_path_len)) {
                match = true;
            }
        } else {
            if (path_len == old_path_len) {
                if (!memcmp64(path, old_path, old_path_len)) {
                    match = true;
                }
            }
        }

        if (match) {
            char *suffix;
            int32 suffix_len;
            char *updated_path;
            int32 updated_path_len;

            suffix = path + old_path_len;
            suffix_len = path_len - old_path_len;
            updated_path_len = new_path_len + suffix_len;
            updated_path = xmalloc(updated_path_len + 1);

            memcpy64(updated_path, new_path, new_path_len);
            memcpy64(updated_path + new_path_len, suffix, suffix_len + 1);

            if (traversal->map) {
                hash_remove_fs_map(traversal->map,
                                   path, path_len);
                hash_insert_fs_map(traversal->map,
                                   updated_path, updated_path_len, i);
            }

            for (int32 j = 0; j < traversal->nfiles; j += 1) {
                if (!S_ISLNK(traversal->stats[j].st_mode)) {
                    if (traversal->link_targets[j] == path) {
                        traversal->link_targets[j] = updated_path;
                        traversal->link_targets_lens[j] = (int16)updated_path_len;
                    }
                }
            }

            traversal->paths[i] = updated_path;
            traversal->paths_lens[i] = (int16)updated_path_len;
        }
    }

    original_len = cecup.rows_len;

    for (int32 i = 0; i < original_len;) {
        CecupItem *item;
        bool split_needed;
        int32 moved_idx;
        char *updated_path_str;
        int32 updated_path_len;

        item = cecup.rows[i];
        split_needed = false;
        moved_idx = -1;
        updated_path_str = NULL;
        updated_path_len = 0;

        if (side == L) {
            if (item->src_idx >= 0) {
                if (item->dst_idx >= 0) {
                    char *src_path;
                    char *dst_path;

                    src_path = cecup.traversal_src.paths[item->src_idx];
                    dst_path = cecup.traversal_dst.paths[item->dst_idx];

                    if (strcmp(src_path, dst_path) != 0) {
                        split_needed = true;
                        moved_idx = item->src_idx;
                        item->src_idx = -1;
                        updated_path_str = src_path;
                        updated_path_len = cecup.traversal_src.paths_lens[moved_idx];
                    }
                }
            }
        } else {
            if (item->src_idx >= 0) {
                if (item->dst_idx >= 0) {
                    char *src_path;
                    char *dst_path;

                    src_path = cecup.traversal_src.paths[item->src_idx];
                    dst_path = cecup.traversal_dst.paths[item->dst_idx];

                    if (strcmp(src_path, dst_path) != 0) {
                        split_needed = true;
                        moved_idx = item->dst_idx;
                        item->dst_idx = -1;
                        updated_path_str = dst_path;
                        updated_path_len = cecup.traversal_dst.paths_lens[moved_idx];
                    }
                }
            }
        }

        if (split_needed) {
            CecupItem *target_item;

            target_item = NULL;
            for (int32 j = 0; j < cecup.rows_len; j += 1) {
                CecupItem *r;
                char *r_path;

                r = cecup.rows[j];
                r_path = item_path_get(r);

                if (item_path_len_get(r) == updated_path_len) {
                    if (!memcmp64(r_path, updated_path_str, updated_path_len)) {
                        target_item = r;
                        break;
                    }
                }
            }

            if (target_item == NULL) {
                g_mutex_lock(&cecup.arena_mutex);
                target_item = xarena_push(cecup.arena, SIZEOF(*target_item));
                memset64(target_item, 0, SIZEOF(*target_item));
                target_item->src_idx = -1;
                target_item->dst_idx = -1;

                if (cecup.rows_len >= cecup.rows_capacity) {
                    if (cecup.rows_capacity == 0) {
                        cecup.rows_capacity = 1024;
                    } else {
                        cecup.rows_capacity *= 2;
                    }
                    cecup.rows = xrealloc(cecup.rows,
                                          cecup.rows_capacity*SIZEOF(CecupItem *));
                    cecup.rows_visible = xrealloc(cecup.rows_visible,
                                                  cecup.rows_capacity*SIZEOF(CecupItem *));
                }
                cecup.rows[cecup.rows_len] = target_item;
                cecup.rows_len += 1;
                g_mutex_unlock(&cecup.arena_mutex);
            }

            if (side == L) {
                target_item->src_idx = moved_idx;
            } else {
                target_item->dst_idx = moved_idx;
            }

            if (item->src_idx == -1) {
                if (item->dst_idx == -1) {
                    for (int32 p = i; p < (cecup.rows_len - 1); p += 1) {
                        cecup.rows[p] = cecup.rows[p + 1];
                    }
                    cecup.rows_len -= 1;
                    original_len -= 1;
                } else {
                    i += 1;
                }
            } else {
                i += 1;
            }
        } else {
            i += 1;
        }
    }

    update_list_from_rows();
    return;
}

static void
update_list_from_rows(void) {
    int32 count_new;
    int32 count_hard;
    int32 count_update;
    int32 count_equal;
    int32 count_delete;
    int32 count_ignore;
    int64 count_selected;
    int64 total_size_bytes;
    int64 current_store_count;
    char pretty_size[16];
    char stats_text[256];
    char button_label[64];
    bool show_new;
    bool show_link;
    bool show_update;
    bool show_equal;
    bool show_delete;
    bool show_ignore;
    struct timespec t0;
    struct timespec t1;

    count_new = 0;
    count_hard = 0;
    count_update = 0;
    count_equal = 0;
    count_delete = 0;
    count_ignore = 0;
    count_selected = 0;
    total_size_bytes = 0;

    clock_gettime(CLOCK_MONOTONIC_RAW, &t0);

    current_store_count = (int64)g_list_model_get_n_items(cecup.store);

    show_new = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_new));
    show_link = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_hard));
    show_update = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_update));
    show_equal = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_equal));
    show_delete = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_delete));
    show_ignore = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_ignore));

    cecup.rows_visible_len = 0;
    for (int32 i = 0; i < cecup.rows_len; i += 1) {
        CecupItem *item;
        enum Action src_act;
        enum Action dst_act;
        enum Reason reason;
        bool visible;
        int64 sz;

        item = cecup.rows[i];
        visible = false;

        if (item->selected) {
            count_selected += 1;
        }

        item_get_actions_reasons(item, &src_act, &dst_act, &reason);

        sz = item_size_side(item, L);
        if (sz < 0) {
            sz = 0;
        }

        switch (src_act) {
        case ACTION_NEW:
            visible = show_new;
            count_new += 1;
            total_size_bytes += sz;
            break;
        case ACTION_HARDLINK:
        case ACTION_SYMLINK:
            visible = show_link;
            count_hard += 1;
            total_size_bytes += sz;
            break;
        case ACTION_UPDATE:
            visible = show_update;
            count_update += 1;
            total_size_bytes += sz;
            break;
        case ACTION_EQUAL:
            visible = show_equal;
            count_equal += 1;
            break;
        case ACTION_DELETED:
            visible = show_delete;
            count_delete += 1;
            break;
        case ACTION_IGNORE:
            if (dst_act == ACTION_DELETE) {
                visible = show_delete;
                count_delete += 1;
            } else {
                visible = show_ignore;
                count_ignore += 1;
            }
            break;
        case ACTION_DELETE:
        case ACTION_LAST:
        default:
            error("Invalid src_action: %u\n", src_act);
            fatal(EXIT_FAILURE);
        }

        if (visible) {
            if (cecup.search_query) {
                if (cecup.search_query[0] != '\0') {
                    char *path;

                    path = item_path_get(item);
                    if (strcasestr(path, cecup.search_query) == NULL) {
                        visible = false;
                    }
                }
            }
        }

        if (visible) {
            cecup.rows_visible[cecup.rows_visible_len] = item;
            cecup.rows_visible_len += 1;
        }
    }

    SNPRINTF(button_label, "%s %d", EMOJI_NEW, count_new);
    gtk_button_set_label(GTK_BUTTON(cecup.filter_new), button_label);

    SNPRINTF(button_label, "%s/%s %d", EMOJI_LINK, EMOJI_SYMLINK, count_hard);
    gtk_button_set_label(GTK_BUTTON(cecup.filter_hard), button_label);

    SNPRINTF(button_label, "%s %d", EMOJI_UPDATE, count_update);
    gtk_button_set_label(GTK_BUTTON(cecup.filter_update), button_label);

    SNPRINTF(button_label, "%s %d", EMOJI_EQUAL, count_equal);
    gtk_button_set_label(GTK_BUTTON(cecup.filter_equal), button_label);

    SNPRINTF(button_label, "%s %d", EMOJI_DELETE, count_delete);
    gtk_button_set_label(GTK_BUTTON(cecup.filter_delete), button_label);

    SNPRINTF(button_label, "%s %d", EMOJI_IGNORE, count_ignore);
    gtk_button_set_label(GTK_BUTTON(cecup.filter_ignore), button_label);

    bytes_pretty(pretty_size, total_size_bytes);
    SNPRINTF(stats_text, _("Selected files: %lld\nTotal Transfer Size: 📦 %s"),
             (llong)count_selected, pretty_size);
    gtk_label_set_text(GTK_LABEL(cecup.stats_label), stats_text);

    if (cecup.rows_visible_len > 0) {
        struct timespec t0_sort;
        struct timespec t1_sort;
        clock_gettime(CLOCK_MONOTONIC_RAW, &t0_sort);

        qsort64(cecup.rows_visible, cecup.rows_visible_len, SIZEOF(CecupItem *),
                cecup_item_compare);

        clock_gettime(CLOCK_MONOTONIC_RAW, &t1_sort);
        PRINT_TIMINGS(cecup.rows_visible_len,
                      t0_sort, t1_sort, "sorting visible rows");
    }

    cecup_list_model_update(CECUP_LIST_MODEL(cecup.store),
                            (int32)current_store_count,
                            cecup.rows_visible_len);

    clock_gettime(CLOCK_MONOTONIC_RAW, &t1);
    PRINT_TIMINGS(cecup.rows_visible_len, t0, t1);
    return;
}

static void
update_row_ignore(Message *message) {
    (void)message;

    invalidate_preview();
    ignore_patterns_load();

    for (int32 i = 0; i < cecup.rows_len; i += 1) {
        CecupItem *item;
        char *path;
        int32 path_len;
        bool is_dir;
        IgnorePattern *match;

        item = cecup.rows[i];
        path = item_path_get(item);
        path_len = item_path_len_get(item);
        is_dir = false;

        if (path_len > 0) {
            if (path[path_len - 1] == '/') {
                is_dir = true;
            }
        }

        match = ignore_patterns_match(path, path_len, is_dir,
                                      cecup.ignore_patterns,
                                      cecup.ignore_count);

        if (match) {
            if (item->src_idx >= 0) {
                cecup.traversal_src.matched_patterns[item->src_idx] = match->str;
                cecup.traversal_src.matched_patterns_lens[item->src_idx] = (int16)match->len;
            }
            if (item->dst_idx >= 0) {
                cecup.traversal_dst.matched_patterns[item->dst_idx] = match->str;
                cecup.traversal_dst.matched_patterns_lens[item->dst_idx] = (int16)match->len;
            }
        }
    }

    update_list_from_rows();
    return;
}

static void cecup_list_model_row_removed(CecupListModel *self, int32 index);
static void cecup_list_model_row_changed(CecupListModel *self, int32 index);

static void
update_ui_process_message(Message *message) {
    GtkTextIter end;
    GtkTextIter start_line;
    GtkTextTagTable *table;
    int32 current_store_count;
    bool is_cr;
    bool buffer_ends_in_lf;

    switch (message->type) {
    case DATA_TYPE_LOG:
    case DATA_TYPE_LOG_CMD:
    case DATA_TYPE_LOG_ERROR:
        is_cr = false;

        if (message->text_len > 0) {
            if (message->text[message->text_len - 1] == '\r') {
                is_cr = true;
                message->text[message->text_len - 1] = '\0';
            }
        }

        gtk_text_buffer_get_end_iter(cecup.log_buffer, &end);

        buffer_ends_in_lf = true;
        if (gtk_text_iter_get_offset(&end) > 0) {
            GtkTextIter last_char;

            last_char = end;
            gtk_text_iter_backward_char(&last_char);
            if (gtk_text_iter_get_char(&last_char) != '\n') {
                buffer_ends_in_lf = false;
            }
        }

        if (is_cr || !buffer_ends_in_lf) {
            start_line = end;
            gtk_text_iter_set_line_offset(&start_line, 0);
            gtk_text_buffer_delete(cecup.log_buffer, &start_line, &end);
            gtk_text_buffer_get_end_iter(cecup.log_buffer, &end);
        }

        table = gtk_text_buffer_get_tag_table(cecup.log_buffer);

        if (message->type == DATA_TYPE_LOG_ERROR) {
            if (gtk_text_tag_table_lookup(table, "err_red") == NULL) {
                gtk_text_buffer_create_tag(cecup.log_buffer, "err_red",
                                           "foreground", "red", NULL);
            }
            gtk_text_buffer_insert_with_tags_by_name(
                cecup.log_buffer, &end, message->text, -1, "err_red", NULL);
        } else if (message->type == DATA_TYPE_LOG_CMD) {
            if (gtk_text_tag_table_lookup(table, "err_blue") == NULL) {
                gtk_text_buffer_create_tag(cecup.log_buffer, "err_blue",
                                           "foreground", "blue", NULL);
            }
            gtk_text_buffer_insert_with_tags_by_name(
                cecup.log_buffer, &end, message->text, -1, "err_blue", NULL);
        } else {
            gtk_text_buffer_insert(cecup.log_buffer,
                                   &end, message->text, -1);
        }

        gtk_text_view_scroll_to_mark(
            GTK_TEXT_VIEW(cecup.log_view),
            gtk_text_buffer_get_insert(cecup.log_buffer), 0.0, FALSE, 0.0, 0.0);
        break;
    case DATA_TYPE_PROGRESS_PREVIEW:
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(cecup.progress_preview),
                                      message->fraction);
        break;
    case DATA_TYPE_ROW_REMOVE:
        update_row_remove(message);
        break;
    case DATA_TYPE_ROW_RENAME:
        update_row_rename(message);
        break;
    case DATA_TYPE_ROW_TRANSFER:
        update_row_transfer(message);
        break;
    case DATA_TYPE_IGNORE_PATTERN:
        update_row_ignore(message);
        break;
    case DATA_TYPE_ENABLE_BUTTONS:
        if (cecup.refresh_id != 0) {
            g_source_remove(cecup.refresh_id);
            cecup.refresh_id = 0;
        }
        update_list_from_rows();

        if (message->preview_clean) {
            cecup.preview_dirty = false;
        } else {
            cecup.preview_dirty = true;
        }

        protect_interface_from_user(false);
        break;
    case DATA_TYPE_CLEAR_TREES:
        if (cecup.refresh_id != 0) {
            g_source_remove(cecup.refresh_id);
            cecup.refresh_id = 0;
        }
        g_mutex_lock(&cecup.arena_mutex);

        current_store_count = (int32)g_list_model_get_n_items(cecup.store);

        arena_reset(cecup.arena);
        cecup.rows_len = 0;
        cecup.rows_visible_len = 0;

        cecup_list_model_update(CECUP_LIST_MODEL(cecup.store),
                                current_store_count, 0);

        g_mutex_unlock(&cecup.arena_mutex);

        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(cecup.progress_preview),
                                      0.0);
        break;
    case DATA_TYPE_ADD_ROW:
    case DATA_TYPE_BATCH:
    case DATA_TYPE_LAST:
    default:
        LOG("Ignoring %s.\n", DATA_TYPE_str(message->type));
        break;
    }

    free_message(message);
    return;
}

static gboolean
update_ui_handler(void *data) {
    MessageBatch *batch;
    Message *message;

    message = data;

    if (message->type == DATA_TYPE_BATCH) {
        batch = data;
        for (int32 i = 0; i < batch->count; i += 1) {
            update_ui_process_message(batch->messages[i]);
        }
        free(batch, sizeof(MessageBatch) + (BATCH_SIZE * sizeof(Message *)));
    } else {
        update_ui_process_message(message);
    }

    return G_SOURCE_REMOVE;
}

static void
update_progress_bar(enum DataType type, double fraction) {
    Message *message;
    static double last_fractions[4] = {0.0, 0.0, 0.0, 0.0};
    int32 index;

    index = 0;

    if (type == DATA_TYPE_PROGRESS_PREVIEW) {
        index = 3;
    }

    if (fraction < 1.0) {
        if ((fraction - last_fractions[index]) < 0.001) {
            if ((fraction - last_fractions[index]) > -0.001) {
                return;
            }
        }
    }
    last_fractions[index] = fraction;

    message = xmalloc(SIZEOF(*message));
    memset64(message, 0, SIZEOF(*message));

    message->type = type;
    message->fraction = fraction;
    g_idle_add(update_ui_handler, message);
    return;
}

#if 0 == TESTING_update
static inline void
update_functions_sink(void) {
    (void)update_progress_bar;
    (void)cecup_get_dirs;
    (void)get_target_tasks;
    (void)free_task_list;
    (void)free_message;
    return;
}
#endif

#if TESTING_update
#include <assert.h>
#include <string.h>

int
main(void) {
    (void)update_progress_bar;
    (void)cecup_get_dirs;
    (void)get_target_tasks;
    (void)free_task_list;
    (void)free_message;
    ASSERT(true);
    exit(EXIT_SUCCESS);
}

#endif

#endif /* UPDATE_C */
