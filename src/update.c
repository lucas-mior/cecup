// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(UPDATE_C)
#define UPDATE_C

#include "gtk_include.h"
#include <glib/gmain.h>

#include "cbase.h"

#include "cecup.h"
#include "aux.c"
#include "list_model.c"
#include "ignore_patterns.c"
#include "item.c"
#include "traversal.c"

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_update 1
#elif !defined(TESTING_update)
#define TESTING_update 0
#endif
#if !defined(TESTING)
#define TESTING 0
#endif

#define UI_INTERVAL_MS 100

static bool update_row_ignore(Message *message);
static void update_ignored_helper(int32 side, int32 row_id, IgnorePattern *match);
static void update_list_from_rows(enum UpdateRowsType);
static void update_progress_bar(double fraction);
static void update_progress_info(char *text, char *tooltip);
static void update_stats_text(int32 count_selected, int64 total_size_bytes);

static bool update_rows(MessageBatch *batch);
static bool update_row_remove(char *path, int32 path_len, int32 side);
static bool update_row_transfer(char *path, int32 path_len);
static bool update_row_rename(char *path, int32 path_len,
                              char *dst_path, int32 dst_path_len, int32 side);

static bool
update_message_is_batch(enum MsgType type) {
    switch (type) {
    case MSG_BATCH_ROW_REMOVE:
    case MSG_BATCH_ROW_TRANSFER:
    case MSG_BATCH_ROW_RENAME:
        return true;
    case MSG_LOG:
    case MSG_LOG_ERROR:
    case MSG_LOG_CMD:
    case MSG_IGNORE_PATTERN:
    case MSG_WORK_FINISHED:
    case MSG_CLEAR_TREES:
    case MSG_PROGRESS:
    case MSG_LAST:
    default:
        return false;
    }
}

static void
update_message_batch_free(MessageBatch *batch, bool paths_owned) {
    if (paths_owned) {
        for (int32 i = 0; i < batch->count; i += 1) {
            free2(batch->paths[i], batch->paths_lens[i] + 1);
            if (batch->type == MSG_BATCH_ROW_RENAME) {
                free2(batch->dst_paths[i], batch->dst_paths_lens[i] + 1);
            }
        }
    }

    free2(batch->paths, batch->capacity*SIZEOF(*(batch->paths)));
    free2(batch->paths_lens, batch->capacity*SIZEOF(*(batch->paths_lens)));
    free2(batch->dst_paths, batch->capacity*SIZEOF(*(batch->dst_paths)));
    free2(batch->dst_paths_lens,
          batch->capacity*SIZEOF(*(batch->dst_paths_lens)));
    free2(batch, SIZEOF(*batch));
    return;
}

static void
update_message_drop(void *data) {
    Message *message = data;

    if (update_message_is_batch(message->type)) {
        update_message_batch_free(data, true);
    } else {
        free_message(message);
    }

    return;
}

static gboolean
update_ui_handler(void *data) {
    Message *message = data;
    GtkTextIter end;
    GtkTextIter start_line;
    GtkTextTagTable *table;
    int32 current_store_count;
    bool is_cr;
    bool buffer_ends_in_lf;
    bool update_list_needed = false;
    bool is_batch = false;

    if (DEBUGGING) {
        error("Handling message: %s\n", MSG_str(message->type));
    }

    if (cecup.window_destroying) {
        if (message->type == MSG_WORK_FINISHED) {
            work_thread_join_once();
        }

        update_message_drop(data);
        return G_SOURCE_REMOVE;
    }

    switch (message->type) {
    case MSG_BATCH_ROW_REMOVE:
    case MSG_BATCH_ROW_TRANSFER:
    case MSG_BATCH_ROW_RENAME:
    {
        MessageBatch *batch = data;
        is_batch = true;
        aux_invalidate_preview();
        if (update_rows(batch)) {
            update_list_needed = true;
        }
        update_message_batch_free(batch, false);
        break;
    }
    case MSG_LOG:
    case MSG_LOG_CMD:
    case MSG_LOG_ERROR:
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

        #pragma clang diagnostic push
        #pragma clang diagnostic ignored "-Wswitch-enum"
        switch (message->type) {
        case MSG_LOG_ERROR:
            if (gtk_text_tag_table_lookup(table, "err_red") == NULL) {
                gtk_text_buffer_create_tag(cecup.log_buffer, "err_red", "foreground", "red", NULL);
            }
            gtk_text_buffer_insert_with_tags_by_name(
                cecup.log_buffer, &end, message->text, -1, "err_red", NULL);
            break;
        case MSG_LOG_CMD:
            if (gtk_text_tag_table_lookup(table, "err_blue") == NULL) {
                gtk_text_buffer_create_tag(cecup.log_buffer, "err_blue", "foreground", "blue", NULL);
            }
            gtk_text_buffer_insert_with_tags_by_name(
                cecup.log_buffer, &end, message->text, -1, "err_blue", NULL);
            break;
        default:
            gtk_text_buffer_insert(cecup.log_buffer, &end, message->text, -1);
            break;
        }
        #pragma clang diagnostic pop

        gtk_text_buffer_get_end_iter(cecup.log_buffer, &end);
        gtk_text_buffer_place_cursor(cecup.log_buffer, &end);
        gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(cecup.log_view),
                                     gtk_text_buffer_get_insert(cecup.log_buffer), 0.0,
                                     FALSE, 0.0, 0.0);
        break;
    case MSG_PROGRESS:
        if (message->fraction >= 0.0) {
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(cecup.progress_bar), message->fraction);
        }
        if (message->text) {
            gtk_progress_bar_set_text(GTK_PROGRESS_BAR(cecup.progress_bar), message->text);
        }
        if (message->src_path) {
            gtk_widget_set_tooltip_text(cecup.progress_bar, message->src_path);
        }
        break;
    case MSG_IGNORE_PATTERN:
        update_list_needed = update_row_ignore(message);
        break;
    case MSG_WORK_FINISHED:
        update_list_from_rows(UPDATE_ROWS_COMPLETE);

        cecup.preview_dirty = !message->preview_clean;
        aux_protect_interface_from_user(false);

        if (DEBUGGING) {
            check_consistent_state();
        }

        work_thread_join_once();
        break;
    case MSG_CLEAR_TREES:
        xpthread_mutex_lock(&cecup.arena_mutex);

        current_store_count = (int32)g_list_model_get_n_items(cecup.store);

        for (int32 i = 0; i < cecup.traversal[L].nfiles; i += 1) {
            cecup.traversal[L].row_ids[i] = -1;
        }
        for (int32 i = 0; i < cecup.traversal[R].nfiles; i += 1) {
            cecup.traversal[R].row_ids[i] = -1;
        }

        cecup.rows_len = 0;
        cecup.rows_visible_len = 0;

        traversal_clean(&cecup.traversal[L]);
        traversal_clean(&cecup.traversal[R]);

        cecup_list_model_update(CECUP_LIST_MODEL(cecup.store), current_store_count, 0);

        xpthread_mutex_unlock(&cecup.arena_mutex);

        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(cecup.progress_bar), 0.0);
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(cecup.progress_bar), "");
        gtk_widget_set_tooltip_text(cecup.progress_bar, "");
        break;
    case MSG_LAST:
    default:
        LOG("Ignoring %s.\n", MSG_str(message->type));
        break;
    }

    if (!is_batch) {
        free_message(message);
    }

    if (update_list_needed) {
        update_list_from_rows(UPDATE_ROWS_COMPLETE);

        if (DEBUGGING) {
            check_consistent_state();
        }
    }

    return G_SOURCE_REMOVE;
}

static bool
update_rows(MessageBatch *batch) {
    bool changed = false;

    for (int32 i = 0; i < batch->count; i += 1) {

        #pragma clang diagnostic push
        #pragma clang diagnostic ignored "-Wswitch-enum"
        switch (batch->type) {
        case MSG_BATCH_ROW_REMOVE:
            if (update_row_remove(batch->paths[i], batch->paths_lens[i], batch->side)) {
                changed = true;
            }
            free2(batch->paths[i], batch->paths_lens[i] + 1);
            break;
        case MSG_BATCH_ROW_TRANSFER:
            if (update_row_transfer(batch->paths[i], batch->paths_lens[i])) {
                changed = true;
            }
            free2(batch->paths[i], batch->paths_lens[i] + 1);
            break;
        case MSG_BATCH_ROW_RENAME:
            if (update_row_rename(batch->paths[i], batch->paths_lens[i],
                                  batch->dst_paths[i], batch->dst_paths_lens[i],
                                  batch->side)) {
                changed = true;
            }
            free2(batch->paths[i], batch->paths_lens[i] + 1);
            free2(batch->dst_paths[i], batch->dst_paths_lens[i] + 1);
            break;
        default:
            TRAP();
        }
        #pragma clang diagnostic pop
    }

    return changed;
}

static bool
update_row_remove(char *path_removed, int32 path_removed_len, int32 side) {
    Traversal *traversal = &cecup.traversal[side];
    int32 idx;
    int32 row_id;

    if (path_removed == NULL || path_removed_len == 0) {
        error("Error: invalid arguments.\n");
        fatal(EXIT_FAILURE);
    }

    if ((!hash_lookup_fs_map(traversal->map, path_removed, path_removed_len, &idx))) {
        return false;
    }

    if ((row_id = traversal->row_ids[idx]) < 0) {
        return false;
    }

    traversal_unlink(traversal, idx);

    hash_remove_fs_map(traversal->map, traversal->paths[idx], traversal->paths_lens[idx]);

    traversal->row_ids[idx] = -1;
    cecup.rows[side][row_id] = -1;

    if ((cecup.rows[L][row_id] == -1) && (cecup.rows[R][row_id] == -1)) {
        for (int32 j = row_id; j < (cecup.rows_len - 1); j += 1) {
            int32 idx_src;
            int32 idx_dst;

            cecup.rows[L][j] = cecup.rows[L][j + 1];
            cecup.rows[R][j] = cecup.rows[R][j + 1];
            cecup.rows_selected[j] = cecup.rows_selected[j + 1];

            idx_src = cecup.rows[L][j];
            idx_dst = cecup.rows[R][j];

            if (idx_src >= 0) {
                cecup.traversal[L].row_ids[idx_src] = j;
            }
            if (idx_dst >= 0) {
                cecup.traversal[R].row_ids[idx_dst] = j;
            }
        }
        cecup.rows_len -= 1;
    }

    return true;
}

static bool
update_row_transfer(char *path_transfered, int32 path_transfered_len) {
    Traversal *traversal_src = &cecup.traversal[L];
    Traversal *traversal_dst = &cecup.traversal[R];
    int32 idx_src;
    int32 row_id;
    char full_path[MAX_PATH_LENGTH];
    struct stat stat;
    char *symlink_target = NULL;
    int32 symlink_target_len = 0;

    if ((path_transfered == NULL) || (path_transfered_len == 0)) {
        error("Error: invalid arguments.\n");
        if (DEBUGGING) {
            fatal(EXIT_FAILURE);
        }
        return false;
    }

    if ((!hash_lookup_fs_map(traversal_src->map, path_transfered, path_transfered_len, &idx_src))) {
        error("Warning: transfered file does not exist in source.\n");
        if (DEBUGGING) {
            fatal(EXIT_FAILURE);
        }
        return false;
    }
    if ((row_id = traversal_src->row_ids[idx_src]) < 0) {
        error("Warning: invalid source row of transfered file.\n");
        if (DEBUGGING) {
            fatal(EXIT_FAILURE);
        }
        return false;
    }

    SNPRINTF(full_path, "%s/%s", cecup.base[R], path_transfered);
    if (lstat(full_path, &stat) < 0) {
        error("Error in stat('%s'): %s.\n", full_path, strerror(errno));
        if (DEBUGGING) {
            fatal(EXIT_FAILURE);
        }
        return false;
    }

    if (cecup.rows[R][row_id] < 0) {
        int32 idx;

        if (hash_lookup_fs_map(traversal_dst->map, path_transfered, path_transfered_len, &idx)) {
            cecup.rows[R][row_id] = idx;
        } else {
            char *path = traversal_src->paths[idx_src];
            int32 path_len = traversal_src->paths_lens[idx_src];

            if (S_ISLNK(stat.st_mode)) {
                symlink_target_len = traversal_symlink_get(traversal_dst,
                                                           full_path, &symlink_target);
            }
            if (S_ISREG(traversal_src->stats[idx_src].st_mode)
                    && (traversal_src->stats[idx_src].st_nlink > 1)) {
                traversal_add_link(traversal_dst, stat, path, path_len);
            }

            traversal_push(traversal_dst, &stat,
                           path, path_len,
                           symlink_target, symlink_target_len,
                           traversal_src->patterns[idx_src],
                           traversal_src->patterns_lens[idx_src]);
            cecup.rows[R][row_id] = traversal_dst->nfiles - 1;
        }
        traversal_dst->row_ids[cecup.rows[R][row_id]] = row_id;
    } else {
        int32 idx = cecup.rows[R][row_id];

        if (S_ISREG(traversal_dst->stats[idx].st_mode)
                && (traversal_dst->stats[idx].st_nlink > 1)) {
            traversal_unlink(traversal_dst, idx);
        }

        if (S_ISLNK(stat.st_mode)) {
            symlink_target_len = traversal_symlink_get(traversal_dst, full_path, &symlink_target);
            traversal_dst->symlink_targets[idx] = symlink_target;
            traversal_dst->symlink_targets_lens[idx] = (int16)symlink_target_len;
        }
        memcpy64(&traversal_dst->stats[idx], &stat, SIZEOF(struct stat));

        if (S_ISREG(traversal_src->stats[idx_src].st_mode)
                && (traversal_src->stats[idx_src].st_nlink > 1)) {
            char *path = traversal_dst->paths[idx];
            int32 path_len = traversal_dst->paths_lens[idx];

            traversal_add_link(traversal_dst, stat, path, path_len);
        }

        traversal_dst->row_ids[idx] = row_id;
    }

    return true;
}

static bool
update_row_rename(char *old_path, int32 old_path_len,
                  char *new_path, int32 new_path_len, int32 side) {
    Traversal *traversal = &cecup.traversal[side];
    Traversal *other_traversal = &cecup.traversal[!side];
    int32 idx;
    int32 row_id;
    int32 other_idx;
    int32 new_idx;
    int32 merge_row_id;
    char *new_path_alloc;
    int32 is_dir = 0;

    if (!hash_lookup_fs_map(traversal->map, old_path, old_path_len, &idx)) {
        error("Didnt found %s on traversal hash map.\n", old_path);
        if (DEBUGGING) {
            fatal(EXIT_FAILURE);
        }
        return false;
    }

    if ((row_id = traversal->row_ids[idx]) < 0) {
        error("No row id for path %s.\n", old_path);
        if (DEBUGGING) {
            fatal(EXIT_FAILURE);
        }
        return false;
    }

    if (old_path[old_path_len - 1] == '/') {
        is_dir = 1;
    }

    new_path_alloc = xarena_push(traversal->arena, new_path_len + is_dir + 1);
    memcpy64(new_path_alloc, new_path, new_path_len + 1);

    if (is_dir && (new_path_alloc[new_path_len - 1] != '/')) {
        new_path_len += 1;
        new_path_alloc[new_path_len - 1] = '/';
        new_path_alloc[new_path_len] = '\0';
    }

    other_idx = cecup.rows[!side][row_id];

    hash_remove_fs_map(traversal->map, old_path, old_path_len);
    traversal->row_ids[idx] = -1;

    {
        IgnorePattern *pattern;
        char *p_match_str = NULL;
        int32 p_match_len = 0;

        pattern = ignore_patterns_match(new_path_alloc, new_path_len,
                                        S_ISDIR(traversal->stats[idx].st_mode),
                                        cecup.ignore_patterns, cecup.ignore_count);

        if (pattern) {
            p_match_str = pattern->str;
            p_match_len = pattern->len;
        }

        new_idx = traversal_push(traversal, &traversal->stats[idx],
                                 new_path_alloc, new_path_len,
                                 traversal->symlink_targets[idx],
                                 traversal->symlink_targets_lens[idx],
                                 p_match_str, p_match_len);
    }

    if (S_ISREG(traversal->stats[idx].st_mode)
        && (traversal->stats[idx].st_nlink > 1)) {
        FileID file_id = file_id_from_stat(&traversal->stats[idx]);
        HardLinks hard_links;

        if (hash_lookup_inode_map(traversal->inode_map,
                                  &file_id, &hard_links)) {
            hard_link_replace_node(&hard_links,
                                   old_path, old_path_len,
                                   new_path_alloc, new_path_len);
            hash_overwrite_inode_map(traversal->inode_map,
                                     &file_id, hard_links);
        }
    }

    merge_row_id = -1;
    {
        int32 m_idx;

        if (hash_lookup_fs_map(other_traversal->map, new_path_alloc, new_path_len, &m_idx)) {
            merge_row_id = other_traversal->row_ids[m_idx];
        }
    }

    if (merge_row_id >= 0) {
        /* Since we use renameat2(RENAME_NOREPLACE),
         * the new path should not exist on this side prior to the rename.
         * of course, someone else could have renamed the file,
         * but that is true of the whole program */
        ASSERT(cecup.rows[side][merge_row_id] == -1);

        cecup.rows[side][merge_row_id] = new_idx;
        traversal->row_ids[new_idx] = merge_row_id;

        if (other_idx >= 0) {
            cecup.rows[side][row_id] = -1;
        } else {
            for (int32 j = row_id; j < (cecup.rows_len - 1); j += 1) {
                int32 idx_src;
                int32 idx_dst;

                cecup.rows[L][j] = cecup.rows[L][j + 1];
                cecup.rows[R][j] = cecup.rows[R][j + 1];
                cecup.rows_selected[j] = cecup.rows_selected[j + 1];

                idx_src = cecup.rows[L][j];
                idx_dst = cecup.rows[R][j];

                if (idx_src >= 0) {
                    cecup.traversal[L].row_ids[idx_src] = j;
                }
                if (idx_dst >= 0) {
                    cecup.traversal[R].row_ids[idx_dst] = j;
                }
            }
            cecup.rows_len -= 1;
        }
    } else {
        if (other_idx >= 0) {
            cecup.rows[side][row_id] = new_idx;
            cecup.rows[!side][row_id] = -1;
            cecup.traversal[side].row_ids[new_idx] = row_id;

            if (side == L) {
                item_add(-1, other_idx);
            } else {
                item_add(other_idx, -1);
            }
        } else {
            cecup.rows[side][row_id] = new_idx;
            traversal->row_ids[new_idx] = row_id;
        }
    }

    return true;
}

static void
update_ignored_helper(int32 side, int32 row_id, IgnorePattern *match) {
    Traversal *traversal = &cecup.traversal[side];
    ASSERT(match);

    if (cecup.rows[side][row_id] >= 0) {
        int32 idx = cecup.rows[side][row_id];

        if (S_ISREG(traversal->stats[idx].st_mode)
                && (traversal->stats[idx].st_nlink > 1)) {
            traversal_unlink(traversal, idx);
        }

        traversal->patterns[idx] = match->str;
        traversal->patterns_lens[idx] = (int16)match->len;
    }
    return;
}

static bool
update_row_ignore(Message *message) {
    (void)message;

    aux_invalidate_preview();

    ignore_patterns_load();

    for (int32 row_id = 0; row_id < cecup.rows_len; row_id += 1) {
        char *path = item_path_get(row_id);
        int32 path_len = item_path_len_get(row_id);
        bool is_dir = false;
        IgnorePattern *match = NULL;

        if (path_len > 0) {
            if (path[path_len - 1] == '/') {
                is_dir = true;
            }
        }

        if (cecup.ignore_count > 0) {
            IgnorePattern *added_pattern = &cecup.ignore_patterns[cecup.ignore_count - 1];

            if (ignore_pattern_match_single(added_pattern, path, path_len, is_dir)) {
                match = added_pattern;
            }
        }

        if (match) {
            update_ignored_helper(L, row_id, match);
            update_ignored_helper(R, row_id, match);
        }
    }

    return true;
}

static void
update_list_from_rows(enum UpdateRowsType change) {
    int32 count_new = 0;
    int32 count_link = 0;
    int32 count_update = 0;
    int32 count_equal = 0;
    int32 count_delete = 0;
    int32 count_ignore = 0;
    int32 count_selected = 0;
    int32 current_store_count;

    int64 total_size_bytes = 0;

    struct timespec t0_rows_loop;
    struct timespec t1_rows_loop;

    bool show_new = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_new));
    bool show_link = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_link));
    bool show_update = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_update));
    bool show_equal = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_equal));
    bool show_delete = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_delete));
    bool show_ignore = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_ignore));

    static RowCache *cache_rows = NULL;
    static int32 cache_rows_capacity = 0;

    int32 limit;

    if (change == UPDATE_ROWS_COMPLETE) {
        limit = cecup.rows_len;
    } else {
        limit = cecup.rows_visible_len;
    }

    current_store_count = (int32)g_list_model_get_n_items(cecup.store);

    time_monotonic_precise(&t0_rows_loop);

    cache_rows = realloc2(cache_rows, cache_rows_capacity, cecup.rows_len, SIZEOF(*cache_rows));
    if (cache_rows_capacity == 0) {
        ASSERT_EQUAL((int32)change, (int32)UPDATE_ROWS_COMPLETE);
    }
    for (int32 i = cache_rows_capacity; i < cecup.rows_len; i += 1) {
        RowCache *cache_row = &cache_rows[i];
        cache_row->row_id = -1;
        cache_row->dst_action = 0;
        cache_row->src_action = 0;
        cache_row->key.i64 = 0;
    }
    cache_rows_capacity = cecup.rows_len;

    cecup.rows_visible_len = 0;

    for (int32 i = 0; i < limit; i += 1) {
        int32 row_id;
        enum Action src_action;
        enum Action dst_action;
        enum Reason reason;
        int64 size_src;
        int64 size_src_for_sum;
        bool is_visible = false;
        int32 idx_src;
        int32 idx_dst;

        if (change == UPDATE_ROWS_COMPLETE) {
            row_id = i;
            item_get_actions_reasons(row_id, &src_action, &dst_action, &reason);
        } else {
            row_id = cache_rows[i].row_id;
            src_action = cache_rows[i].src_action;
            dst_action = cache_rows[i].dst_action;
            ASSERT_MORE(row_id, -1);
        }

        idx_src = cecup.rows[L][row_id];
        idx_dst = cecup.rows[R][row_id];

        if (cecup.rows_selected[row_id]) {
            count_selected += 1;
            if (count_selected == 1) {
                total_size_bytes = 0;
            }
        }

        if ((size_src = traversal_size_side(idx_src, L)) < 0) {
            size_src = 0;
        }

        size_src_for_sum = size_src;
        if ((count_selected > 0) && !cecup.rows_selected[row_id]) {
            size_src_for_sum = 0;
        }

        switch (src_action) {
        case ACTION_NEW:
            count_new += 1;
            total_size_bytes += size_src_for_sum;
            is_visible = show_new;
            break;
        case ACTION_HARDLINK:
        case ACTION_SYMLINK:
            count_link += 1;
            total_size_bytes += size_src_for_sum;
            is_visible = show_link;
            break;
        case ACTION_UPDATE:
            count_update += 1;
            total_size_bytes += size_src_for_sum;
            is_visible = show_update;
            break;
        case ACTION_EQUAL:
            count_equal += 1;
            is_visible = show_equal;
            break;
        case ACTION_DELETED:
        case ACTION_DELETE:
            count_delete += 1;
            is_visible = show_delete;
            break;
        case ACTION_IGNORE:
            if (dst_action == ACTION_DELETE) {
                count_delete += 1;
                is_visible = show_delete;
            } else {
                count_ignore += 1;
                is_visible = show_ignore;
            }
            break;
        case ACTION_LAST:
        default:
            break;
        }

        if (!is_visible) {
            continue;
        }

        if (((change == UPDATE_ROWS_COMPLETE) || (change == UPDATE_ROWS_FILTER_OUT))
            && (cecup.search_query_len > 0)) {
            char *path = traversal_path_get(idx_src, idx_dst);
            if (strcasestr(path, cecup.search_query) == NULL) {
                continue;
            }
        }

        {
            int32 v_idx = cecup.rows_visible_len;
            char *path;
            cache_rows[v_idx].row_id = row_id;
            cache_rows[v_idx].src_action = src_action;
            cache_rows[v_idx].dst_action = dst_action;

            switch (cecup.sort_col) {
            case COL_SRC_PATH:
                if ((path = traversal_path_side(idx_src, L))) {
                    cache_rows[v_idx].key.ptr = path;
                } else {
                    cache_rows[v_idx].key.ptr = "";
                }
                break;
            case COL_DST_PATH:
                if ((path = traversal_path_side(idx_dst, R))) {
                    cache_rows[v_idx].key.ptr = path;
                } else {
                    cache_rows[v_idx].key.ptr = "";
                }
                break;
            case COL_SRC_SIZE:
                cache_rows[v_idx].key.i64 = size_src;
                break;
            case COL_DST_SIZE:
                cache_rows[v_idx].key.i64 = traversal_size_side(idx_dst, R);
                break;
            case COL_SRC_MTIME:
                cache_rows[v_idx].key.i64 = traversal_mtime_side(idx_src, L);
                break;
            case COL_DST_MTIME:
                cache_rows[v_idx].key.i64 = traversal_mtime_side(idx_dst, R);
                break;
            case COL_SRC_ACTION:
            case COL_SELECTED:
                cache_rows[v_idx].key.i64 = (int64)src_action;
                break;
            case COL_DST_ACTION:
                cache_rows[v_idx].key.i64 = (int64)dst_action;
                break;
            case COL_LAST:
            default:
                cache_rows[v_idx].key.i64 = 0;
                break;
            }

            cecup.rows_visible_len += 1;
        }
    }

    time_monotonic_precise(&t1_rows_loop);
    PRINT_TIMINGS(limit, t0_rows_loop, t1_rows_loop, "rows loop");

    if ((change != UPDATE_ROWS_SELECT) && (cecup.rows_visible_len > 0)) {
        struct timespec t0_sort;
        struct timespec t1_sort;
        time_monotonic_precise(&t0_sort);

        sort_item_functions[cecup.sort_col](cache_rows, (int64)cecup.rows_visible_len);

        (void)sort_item_functions;
        (void)compare_item_functions;

        for (int32 k = 0; k < cecup.rows_visible_len; k += 1) {
            cecup.rows_visible[k] = cache_rows[k].row_id;
        }

        time_monotonic_precise(&t1_sort);
        PRINT_TIMINGS(cecup.rows_visible_len, t0_sort, t1_sort, "sorting");
    }

    if (change == UPDATE_ROWS_COMPLETE) {
        char button_label[64];

        SNPRINTF(button_label, "%s %d", EMOJI_NEW, count_new);
        gtk_button_set_label(GTK_BUTTON(cecup.filter_new), button_label);

        SNPRINTF(button_label, "%s/%s %d", EMOJI_LINK, EMOJI_SYMLINK, count_link);
        gtk_button_set_label(GTK_BUTTON(cecup.filter_link), button_label);

        SNPRINTF(button_label, "%s %d", EMOJI_UPDATE, count_update);
        gtk_button_set_label(GTK_BUTTON(cecup.filter_update), button_label);

        SNPRINTF(button_label, "%s %d", EMOJI_EQUAL, count_equal);
        gtk_button_set_label(GTK_BUTTON(cecup.filter_equal), button_label);

        SNPRINTF(button_label, "%s %d", EMOJI_DELETE, count_delete);
        gtk_button_set_label(GTK_BUTTON(cecup.filter_delete), button_label);

        SNPRINTF(button_label, "%s %d", EMOJI_IGNORE, count_ignore);
        gtk_button_set_label(GTK_BUTTON(cecup.filter_ignore), button_label);

    }
    if ((change == UPDATE_ROWS_COMPLETE) || (change == UPDATE_ROWS_SELECT)) {
        update_stats_text(count_selected, total_size_bytes);
    }

    {
        struct timespec t0;
        struct timespec t1;
        time_monotonic_precise(&t0);

        cecup_list_model_update(CECUP_LIST_MODEL(cecup.store),
                                (int32)current_store_count, cecup.rows_visible_len);

        time_monotonic_precise(&t1);
        PRINT_TIMINGS(cecup.rows_visible_len, t0, t1, "cecup_list_model_update");
    }
    return;
}

static void
update_stats_text(int32 count_selected, int64 total_size_bytes) {
    char pretty_size[16];
    char stats_text[512];

    bytes_pretty(pretty_size, total_size_bytes);

    if (count_selected > 0) {
        SNPRINTF(stats_text,
                 _("<span foreground='red'>"
                   "Selected files: %d"
                   "</span>\nTotal Transfer Size: 📦 %s"),
                 count_selected, pretty_size);
    } else {
        SNPRINTF(stats_text,
                 _("Selected files: %d\n"
                   "Total Transfer Size: 📦 %s"),
                 count_selected, pretty_size);
    }

    gtk_label_set_markup(GTK_LABEL(cecup.stats_label), stats_text);
    return;
}

static void
update_progress_bar(double fraction) {
    static double last_fraction;

    if ((fraction > 0.0) && (last_fraction < 1.0)) {
        if ((fraction - last_fraction) < 0.001) {
            return;
        }
    }
    last_fraction = fraction;

    {
        Message *message = malloc2(SIZEOF(*message));
        *message = (Message){0};

        message->type = MSG_PROGRESS;
        message->fraction = fraction;

        g_idle_add(update_ui_handler, message);
    }
    return;
}

static void
update_progress_info(char *text, char *tooltip) {
    Message *message = malloc2(SIZEOF(*message));
    *message = (Message){0};

    message->type = MSG_PROGRESS;
    message->fraction = -1.0;

    if (text) {
        message->text_len = strlen32(text);
        message->text = malloc2(message->text_len + 1);
        memcpy64(message->text, text, message->text_len + 1);
    }

    if (tooltip) {
        message->src_path_len = strlen32(tooltip);
        message->src_path = malloc2(message->src_path_len + 1);
        memcpy64(message->src_path, tooltip, message->src_path_len + 1);
    }

    g_idle_add(update_ui_handler, message);
    return;
}

#if (0 == TESTING_update) && TESTING
static inline void
update_functions_sink(void) {
    (void)update_functions_sink;
    (void)update_progress_bar;
    (void)cecup_get_dirs;
    (void)get_target_tasks;
    (void)task_list_free;
    (void)free_message;
    return;
}
#endif

#if TESTING_update
#define CBASE_IMPLEMENT
#include "cbase.h"

#include "work.c"

#define MOCK_WIDGET(var, constructor) do { \
    var = constructor; \
    g_object_ref_sink(var); \
} while(0)

int
main(void) {
    int32 n = 3;
    Message msg = {0};
    bool res;

    if (!gtk_init_check()) {
        exit(EXIT_SUCCESS);
    }

    /* 1. Setup global infrastructure to prevent crashes in sub-calls */
    MOCK_WIDGET(cecup.gtk_window, gtk_window_new());
    MOCK_WIDGET(cecup.stop_button, gtk_button_new());
    MOCK_WIDGET(cecup.sync_button, gtk_button_new());
    MOCK_WIDGET(cecup.stats_label, gtk_label_new(NULL));
    MOCK_WIDGET(cecup.filter_new, gtk_toggle_button_new());
    MOCK_WIDGET(cecup.filter_link, gtk_toggle_button_new());
    MOCK_WIDGET(cecup.filter_update, gtk_toggle_button_new());
    MOCK_WIDGET(cecup.filter_equal, gtk_toggle_button_new());
    MOCK_WIDGET(cecup.filter_delete, gtk_toggle_button_new());
    MOCK_WIDGET(cecup.filter_ignore, gtk_toggle_button_new());
    MOCK_WIDGET(cecup.progress_bar, gtk_progress_bar_new());

    MOCK_WIDGET(cecup.tree[L], gtk_column_view_new(NULL));
    MOCK_WIDGET(cecup.tree[R], gtk_column_view_new(NULL));
    MOCK_WIDGET(cecup.dir_entry[L], gtk_entry_new());
    MOCK_WIDGET(cecup.dir_entry[R], gtk_entry_new());
    MOCK_WIDGET(cecup.diff_entry, gtk_entry_new());
    MOCK_WIDGET(cecup.term_entry, gtk_entry_new());
    MOCK_WIDGET(cecup.search_entry, gtk_entry_new());
    MOCK_WIDGET(cecup.check_fs_button, gtk_check_button_new());
    MOCK_WIDGET(cecup.delete_after_button, gtk_check_button_new());
    MOCK_WIDGET(cecup.delete_ignored_button, gtk_check_button_new());

    {
        GtkWidget *text_view;
        MOCK_WIDGET(text_view, gtk_text_view_new());
        cecup.log_view = text_view;
        cecup.log_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
    }

    {
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_window_set_child(GTK_WINDOW(cecup.gtk_window), box);
        gtk_box_append(GTK_BOX(box), cecup.stop_button);
        gtk_box_append(GTK_BOX(box), cecup.sync_button);
        gtk_box_append(GTK_BOX(box), cecup.stats_label);
        gtk_box_append(GTK_BOX(box), cecup.filter_new);
        gtk_box_append(GTK_BOX(box), cecup.filter_link);
        gtk_box_append(GTK_BOX(box), cecup.filter_update);
        gtk_box_append(GTK_BOX(box), cecup.filter_equal);
        gtk_box_append(GTK_BOX(box), cecup.filter_delete);
        gtk_box_append(GTK_BOX(box), cecup.filter_ignore);
        gtk_box_append(GTK_BOX(box), cecup.progress_bar);
        gtk_box_append(GTK_BOX(box), cecup.log_view);
        gtk_box_append(GTK_BOX(box), cecup.tree[L]);
        gtk_box_append(GTK_BOX(box), cecup.tree[R]);
        gtk_box_append(GTK_BOX(box), cecup.dir_entry[L]);
        gtk_box_append(GTK_BOX(box), cecup.dir_entry[R]);
        gtk_box_append(GTK_BOX(box), cecup.diff_entry);
        gtk_box_append(GTK_BOX(box), cecup.term_entry);
        gtk_box_append(GTK_BOX(box), cecup.search_entry);
        gtk_box_append(GTK_BOX(box), cecup.check_fs_button);
        gtk_box_append(GTK_BOX(box), cecup.delete_after_button);
        gtk_box_append(GTK_BOX(box), cecup.delete_ignored_button);
    }

    cecup.store = G_LIST_MODEL(cecup_list_model_new());

    cecup.rows_len = n;
    cecup.rows_visible_len = n;
    cecup.rows_capacity = n;
    cecup.rows[L] = malloc2(cecup.rows_capacity * SIZEOF(int32));
    cecup.rows[R] = malloc2(cecup.rows_capacity * SIZEOF(int32));
    cecup.rows_visible = malloc2(cecup.rows_capacity * SIZEOF(int32));
    cecup.rows_selected = malloc2(cecup.rows_capacity * SIZEOF(uint8));

    for (int32 side = 0; side < 2; side += 1) {
        Traversal *t = &cecup.traversal[side];

        t->nfiles = n;
        t->capacity = n;
        t->map = hash_create_fs_map(16, "t->map");
        t->inode_map = hash_create_inode_map(16, "t->inode_map");
        t->arena = arena_create(SIZEMB(1), "t->arena");

        t->paths = malloc2(t->capacity * SIZEOF(char *));
        t->paths_lens = malloc2(t->capacity * SIZEOF(int16));
        t->row_ids = malloc2(t->capacity * SIZEOF(int32));
        t->stats = malloc2(t->capacity * SIZEOF(struct stat));
        t->patterns = malloc2(t->capacity * SIZEOF(char *));
        t->patterns_lens = malloc2(t->capacity * SIZEOF(int16));
        t->symlink_targets = malloc2(t->capacity * SIZEOF(char *));
        t->symlink_targets_lens = malloc2(t->capacity * SIZEOF(int16));

        memset64(t->stats, 0, t->capacity * SIZEOF(struct stat));
        memset64(t->patterns, 0, t->capacity * SIZEOF(char *));
        memset64(t->patterns_lens, 0, t->capacity * SIZEOF(int16));
        memset64(t->symlink_targets, 0, t->capacity * SIZEOF(char *));
        memset64(t->symlink_targets_lens, 0, t->capacity * SIZEOF(int16));

        t->paths[0] = xarena_push(t->arena, 7); memcpy64(t->paths[0], "file_a", 7);
        t->paths_lens[0] = 6;
        t->paths[1] = xarena_push(t->arena, 7); memcpy64(t->paths[1], "file_b", 7);
        t->paths_lens[1] = 6;
        t->paths[2] = xarena_push(t->arena, 7); memcpy64(t->paths[2], "file_c", 7);
        t->paths_lens[2] = 6;

        for (int32 i = 0; i < n; i += 1) {
            hash_insert_fs_map(t->map, t->paths[i], t->paths_lens[i], i);
            t->row_ids[i] = i;
            cecup.rows[side][i] = i;
            cecup.rows_visible[i] = i;
            cecup.rows_selected[i] = 0;
            t->stats[i].st_mode = S_IFREG | 0644;
            t->stats[i].st_ino = (ino_t)(i + side * 100);
            t->stats[i].st_size = 1024;
            t->stats[i].st_nlink = 1;
        }
    }

    cecup.base_len[L] = 5;
    cecup.base[L] = xmemdup("/src/", 6);
    cecup.base_len[R] = 5;
    cecup.base[R] = xmemdup("/dst/", 6);

    /* --- Test update_row_remove --- */
    msg.type = MSG_BATCH_ROW_REMOVE;
    msg.side = L;
    msg.src_path = "file_a";
    msg.src_path_len = 6;

    /* Remove from one side only. Rows count shouldn't change. */
    res = update_row_remove(msg.src_path, msg.src_path_len, msg.side);
    ASSERT(res == true);
    ASSERT_EQUAL(cecup.rows[L][0], -1);
    ASSERT_EQUAL(cecup.rows[R][0], 0);
    ASSERT_EQUAL(cecup.rows_len, 3);

    /* Remove from the other side. Arrays must shift. */
    msg.side = R;
    res = update_row_remove(msg.src_path, msg.src_path_len, msg.side);
    ASSERT(res == true);
    ASSERT_EQUAL(cecup.rows_len, 2);

    /* Index 1 (file_b) should now be row 0 */
    ASSERT_EQUAL(cecup.rows[L][0], 1);
    ASSERT_EQUAL(cecup.traversal[L].row_ids[1], 0);

    /* --- Test update_row_rename --- */
    {
        /* Rename handles virtual state internally through traversal map */
        res = update_row_rename("file_b", 6, "file_d", 6, L);
        ASSERT(res == true);

        /* Old idx 1 must be removed, new idx assigned */
        ASSERT(cecup.traversal[L].row_ids[1] == -1);
        ASSERT(cecup.rows[L][0] > 1); /* it pushes a new item to traversal array */
    }

    /* --- Test update_row_ignore --- */
    strcpy(cecup.ignore_path, "test_ignore.conf");
    g_file_set_contents(cecup.ignore_path, "file_c\n", -1, NULL);

    {
        Message ignore_msg = {0};
        update_row_ignore(&ignore_msg);

        /* file_c is currently at row_id 1 after the previous shifts */
        ASSERT(cecup.traversal[L].patterns[cecup.rows[L][1]] != NULL);
        ASSERT(strcmp(cecup.traversal[L].patterns[cecup.rows[L][1]], "file_c") == 0);
    }

    /* --- Test update_list_from_rows (UI filtering logic) --- */
    cecup.sort_col = COL_SRC_SIZE;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup.filter_equal), TRUE);
    update_list_from_rows(UPDATE_ROWS_COMPLETE);

    /* --- Test update_stats_text --- */
    update_stats_text(1, 4096);
    update_stats_text(0, 0);

    /* --- Test update_progress_bar / state --- */
    update_progress_bar(0.5);
    update_progress_bar(0.50001); /* Should be ignored due to delta */
    update_progress_info("Processing...", "file_b");

    /* --- Test update_ui_handler --- */
    {
        Message *ui_msg;

        ui_msg = malloc2(SIZEOF(*ui_msg));
        *ui_msg = (Message){0};
        ui_msg->type = MSG_LOG;
        ui_msg->text = xmemdup("Test log\r", 10);
        ui_msg->text_len = 9;
        update_ui_handler(ui_msg);

        ui_msg = malloc2(SIZEOF(*ui_msg));
        *ui_msg = (Message){0};
        ui_msg->type = MSG_LOG_ERROR;
        ui_msg->text = xmemdup("Test error\n", 12);
        ui_msg->text_len = 11;
        update_ui_handler(ui_msg);

        ui_msg = malloc2(SIZEOF(*ui_msg));
        *ui_msg = (Message){0};
        ui_msg->type = MSG_LOG_CMD;
        ui_msg->text = xmemdup("Test cmd\n", 10);
        ui_msg->text_len = 9;
        update_ui_handler(ui_msg);

        ui_msg = malloc2(SIZEOF(*ui_msg));
        *ui_msg = (Message){0};
        ui_msg->type = MSG_PROGRESS;
        ui_msg->fraction = 0.8;
        ui_msg->text = xmemdup("Progress...", 12);
        ui_msg->text_len = 11;
        ui_msg->src_path = xmemdup("path/a", 7);
        ui_msg->src_path_len = 6;
        update_ui_handler(ui_msg);

        ui_msg = malloc2(SIZEOF(*ui_msg));
        *ui_msg = (Message){0};
        ui_msg->type = MSG_CLEAR_TREES;
        update_ui_handler(ui_msg);
        ASSERT_EQUAL(cecup.rows_len, 0);
    }

    /* --- Test update_rows (Batch processing) --- */
    {
        MessageBatch *batch = malloc2(SIZEOF(*batch));
        *batch = (MessageBatch){0};

        batch->type = MSG_BATCH_ROW_REMOVE;
        batch->count = 0;
        batch->capacity = 0;

        update_ui_handler(batch);
    }

    /* Cleanup */
    g_object_unref(cecup.gtk_window);
    g_object_unref(cecup.stop_button);
    g_object_unref(cecup.sync_button);
    g_object_unref(cecup.stats_label);
    g_object_unref(cecup.filter_new);
    g_object_unref(cecup.filter_link);
    g_object_unref(cecup.filter_update);
    g_object_unref(cecup.filter_equal);
    g_object_unref(cecup.filter_delete);
    g_object_unref(cecup.filter_ignore);
    g_object_unref(cecup.progress_bar);
    g_object_unref(cecup.log_view);
    g_object_unref(cecup.store);
    g_object_unref(cecup.tree[L]);
    g_object_unref(cecup.tree[R]);
    g_object_unref(cecup.dir_entry[L]);
    g_object_unref(cecup.dir_entry[R]);
    g_object_unref(cecup.diff_entry);
    g_object_unref(cecup.term_entry);
    g_object_unref(cecup.search_entry);
    g_object_unref(cecup.check_fs_button);
    g_object_unref(cecup.delete_after_button);
    g_object_unref(cecup.delete_ignored_button);

    unlink(cecup.ignore_path);
    if (cecup.ignore_patterns) {
        for(int32 i=0; i<cecup.ignore_count; i+=1) {
            free2(cecup.ignore_patterns[i].str, cecup.ignore_patterns[i].len + 1);
        }
        free2(cecup.ignore_patterns, cecup.ignore_capacity * SIZEOF(IgnorePattern));
    }

    for (int32 side = 0; side < 2; side += 1) {
        Traversal *t = &cecup.traversal[side];

        hash_destroy_fs_map(t->map);
        hash_destroy_inode_map(t->inode_map);
        arena_destroy(t->arena);

        free2(t->paths, t->capacity * SIZEOF(char *));
        free2(t->paths_lens, t->capacity * SIZEOF(int16));
        free2(t->row_ids, t->capacity * SIZEOF(int32));
        free2(t->stats, t->capacity * SIZEOF(struct stat));
        free2(t->patterns, t->capacity * SIZEOF(char *));
        free2(t->patterns_lens, t->capacity * SIZEOF(int16));
        free2(t->symlink_targets, t->capacity * SIZEOF(char *));
        free2(t->symlink_targets_lens, t->capacity * SIZEOF(int16));
    }

    free2(cecup.rows[L], cecup.rows_capacity * SIZEOF(int32));
    free2(cecup.rows[R], cecup.rows_capacity * SIZEOF(int32));
    free2(cecup.rows_visible, cecup.rows_capacity * SIZEOF(int32));
    free2(cecup.rows_selected, cecup.rows_capacity * SIZEOF(uint8));

    free2(cecup.base[L], 6);
    free2(cecup.base[R], 6);

    ASSERT(true);
    exit(EXIT_SUCCESS);
}
#endif

#endif /* UPDATE_C */
