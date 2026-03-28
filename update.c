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
    char *pattern = message->src_path;
    int32 pattern_len = message->path_len;
    int32 side = message->side;
    bool changed = false;
    Traversal *traversal;

    if (side == L) {
        traversal = &cecup.traversal_src;
    } else {
        traversal = &cecup.traversal_dst;
    }

    if (pattern == NULL || pattern_len == 0) {
        return;
    }

    for (int32 i = 0; i < cecup.rows_len; ) {
        CecupItem *item = cecup.rows[i];
        char *path = item_path_get(item);
        int32 path_len = item_path_len_get(item);
        bool match = false;

        if (pattern[pattern_len - 1] == '/') {
            if (BEGINS_WITH(path, pattern, pattern_len)) {
                match = true;
            }
        } else if (path_len == pattern_len) {
            if (memcmp64(path, pattern, pattern_len) == 0) {
                match = true;
            }
        }

        if (!match) {
            i += 1;
            continue;
        }

        changed = true;

        int32 *idx_ptr;
        if (side == L) {
            idx_ptr = &item->src_idx;
        } else {
            idx_ptr = &item->dst_idx;
        }

        if (*idx_ptr >= 0) {
            int32 idx = *idx_ptr;
            
            if (traversal->inode_map && S_ISREG(traversal->stats[idx].st_mode)) {
                if (traversal->stats[idx].st_nlink > 1) {
                    char inode_str[64];
                    int32 n = itoa2(inode_str, (long)traversal->stats[idx].st_ino);
                    hash_remove_inode_map(traversal->inode_map, inode_str, n);
                }
            }

            hash_remove_fs_map(traversal->map,
                               traversal->paths[idx],
                               traversal->paths_lens[idx]);
            memset64(&traversal->stats[idx], 0, SIZEOF(struct stat));
            *idx_ptr = -1;
        }

        if ((item->src_idx == -1) && (item->dst_idx == -1)) {
            for (int32 j = i; j < (cecup.rows_len - 1); j += 1) {
                cecup.rows[j] = cecup.rows[j + 1];
            }
            cecup.rows_len -= 1;
        } else {
            i += 1;
        }
    }

    if (changed) {
        /* This will call cecup_list_model_update, which now clears proxies */
        update_list_from_rows();
    }

    return;
}

static void
update_row_transfer(Message *message) {
    char *pattern;
    int32 pattern_len;
    bool changed;

    pattern = message->src_path;
    pattern_len = message->path_len;
    changed = false;

    if (pattern == NULL || pattern_len == 0) {
        return;
    }

    for (int32 i = 0; i < cecup.rows_len; i += 1) {
        CecupItem *item = cecup.rows[i];
        char *path;
        int32 path_len;
        bool match;

        path = item_path_get(item);
        path_len = item_path_len_get(item);
        match = false;

        if (pattern[pattern_len - 1] == '/') {
            if (BEGINS_WITH(path, pattern, pattern_len)) {
                match = true;
            }
        } else if (path_len == pattern_len) {
            if (memcmp64(path, pattern, pattern_len) == 0) {
                match = true;
            }
        }

        if (!match) {
            continue;
        }

        if (item->src_idx >= 0) {
            Traversal *src;
            Traversal *dst;

            src = &cecup.traversal_src;
            dst = &cecup.traversal_dst;

            if (item->dst_idx < 0) {
                int32 *lookup;

                if ((lookup = hash_lookup_fs_map(dst->map, path, path_len))) {
                    item->dst_idx = *lookup;
                } else {
                    item->dst_idx = traversal_push(dst,
                                                   src->paths[item->src_idx],
                                                   src->paths_lens[item->src_idx],
                                                   &src->stats[item->src_idx],
                                                   src->link_targets[item->src_idx],
                                                   src->link_targets_lens[item->src_idx],
                                                   src->matched_patterns[item->src_idx],
                                                   src->matched_patterns_lens[item->src_idx]);
                }
            } else {
                memcpy64(&dst->stats[item->dst_idx],
                         &src->stats[item->src_idx],
                         SIZEOF(struct stat));
            }

            changed = true;
        }
    }

    if (changed) {
        update_list_from_rows();
    }

    return;
}

static void
update_row_rename(Message *message) {
    char *old_pattern;
    int32 old_len;
    char *new_pattern;
    int32 new_len;
    int32 side;
    Traversal *tr;
    Traversal *other_tr;
    bool changed;
    int32 *matches;
    int32 n_matches;
    int32 original_nfiles;

    old_pattern = message->old_path;
    old_len = message->old_path_len;
    new_pattern = message->new_path;
    new_len = message->new_path_len;
    side = message->side;
    tr = (side == L) ? &cecup.traversal_src : &cecup.traversal_dst;
    other_tr = (side == L) ? &cecup.traversal_dst : &cecup.traversal_src;
    changed = false;

    if (old_pattern == NULL || (old_len == 0) || (new_pattern == NULL)) {
        return;
    }

    /* 1. Identify all affected indices (handling directories recursively) */
    original_nfiles = tr->nfiles;
    matches = xmalloc(original_nfiles * SIZEOF(int32));
    n_matches = 0;

    for (int32 idx = 0; idx < original_nfiles; idx += 1) {
        char *path = tr->paths[idx];
        int32 path_len = (int32)tr->paths_lens[idx];
        bool match = false;

        if (old_pattern[old_len - 1] == '/') {
            if (BEGINS_WITH(path, old_pattern, old_len)) {
                match = true;
            }
        } else if (path_len == old_len) {
            if (memcmp64(path, old_pattern, old_len) == 0) {
                match = true;
            }
        }

        if (match) {
            matches[n_matches] = idx;
            n_matches += 1;
        }
    }

    /* 2. Process renames and update row relationships */
    for (int32 i = 0; i < n_matches; i += 1) {
        int32 idx = matches[i];
        char *path = tr->paths[idx];
        int32 path_len = (int32)tr->paths_lens[idx];
        int32 suffix_len;
        int32 final_len;
        char *final_path;
        int32 *lookup_ptr;
        int32 other_idx = -1;
        bool linked = false;

        /* Break the link in any existing CecupItem using this index */
        for (int32 j = 0; j < cecup.rows_len; j += 1) {
            CecupItem *item = cecup.rows[j];
            int32 *idx_ptr = (side == L) ? &item->src_idx : &item->dst_idx;

            if (*idx_ptr == idx) {
                *idx_ptr = -1;
                break;
            }
        }

        /* Update Traversal path and hash map */
        hash_remove_fs_map(tr->map, path, path_len);

        suffix_len = path_len - old_len;
        final_len = new_len + suffix_len;
        final_path = xmalloc(final_len + 1);

        memcpy64(final_path, new_pattern, new_len);
        if (suffix_len > 0) {
            memcpy64(final_path + new_len, path + old_len, suffix_len);
        }
        final_path[final_len] = '\0';

        tr->paths[idx] = final_path;
        tr->paths_lens[idx] = (int16)final_len;
        hash_insert_fs_map(tr->map, final_path, final_len, idx);

        /* Re-link: Find if the new path exists on the other side */
        if ((lookup_ptr = hash_lookup_fs_map(other_tr->map, final_path, final_len))) {
            other_idx = *lookup_ptr;
        }

        if (other_idx >= 0) {
            /* Look for a row that has the other index but is currently unlinked on our side */
            for (int32 j = 0; j < cecup.rows_len; j += 1) {
                CecupItem *item = cecup.rows[j];
                int32 *side_idx = (side == L) ? &item->src_idx : &item->dst_idx;
                int32 *other_side_idx = (side == L) ? &item->dst_idx : &item->src_idx;

                if (*other_side_idx == other_idx && *side_idx == -1) {
                    *side_idx = idx;
                    linked = true;
                    break;
                }
            }
        }

        if (!linked) {
            if (side == L) {
                work_add_item(idx, -1);
            } else {
                work_add_item(-1, idx);
            }
        }

        changed = true;
    }

    if (changed) {
        /* 3. Cleanup: Remove rows that became empty (-1, -1) */
        for (int32 i = 0; i < cecup.rows_len; ) {
            CecupItem *item = cecup.rows[i];
            if (item->src_idx == -1 && item->dst_idx == -1) {
                for (int32 j = i; j < (cecup.rows_len - 1); j += 1) {
                    cecup.rows[j] = cecup.rows[j + 1];
                }
                cecup.rows_len -= 1;
            } else {
                i += 1;
            }
        }

        update_list_from_rows();
    }

    free(matches, original_nfiles * SIZEOF(int32));
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
