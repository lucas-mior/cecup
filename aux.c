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

#if !defined(AUX_C)
#define AUX_C

#include <gtk/gtk.h>
#include <glib/gmain.h>
#include <stdlib.h>

#include "i18n.h"
#include "cecup.h"
#include "util.c"

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_aux 1
#elif !defined(TESTING_aux)
#define TESTING_aux 0
#endif

#define UI_INTERVAL_MS 100

static void on_sort_changed(GtkTreeSortable *sortable, void *data);

static void
protect_interface_from_user(bool state) {
    gtk_widget_set_sensitive(cecup.preview_button, !state);
    gtk_widget_set_sensitive(cecup.sync_button, !state);
    gtk_widget_set_sensitive(cecup.fix_button, !state);
    gtk_widget_set_sensitive(cecup.ignore_button, !state);

    gtk_widget_set_sensitive(cecup.src_entry, !state);
    gtk_widget_set_sensitive(cecup.dst_entry, !state);
    gtk_widget_set_sensitive(cecup.invert_button, !state);

    gtk_widget_set_sensitive(cecup.stop_button, state);
    return;
}

static void
free_update_data(Message *message) {
    free(message->src_path);
    free(message);
    return;
}

static void
free_task_list(TaskList *tasks) {
    if (tasks == NULL) {
        return;
    }

    for (int32 i = 0; i < tasks->count; i += 1) {
        Task *task = tasks->items[i];

        if (task->link_target) {
            free(task->link_target);
        }
        if (task->message) {
            free(task->message);
        }
        free(task);
    }

    free(tasks);
    return;
}

static TaskList *
get_target_tasks(int32 side, char *clicked_path,
                 enum CecupAction clicked_action) {
    TaskList *tasks;
    int64 tasks_size = STRUCT_ARRAY_SIZE(tasks, Task *, cecup.rows_len);
    int32 count = 0;

    tasks = xmalloc(tasks_size);
    memset64(tasks, 0, tasks_size);

    for (int32 i = 0; i < cecup.rows_len; i += 1) {
        CecupRow *row = cecup.rows[i];
        char *filepath;
        int32 path_len;
        enum CecupAction action;
        Task *task;

        if (!(row->selected)) {
            continue;
        }

        if (side == SIDE_LEFT) {
            filepath = row->src_path;
            action = row->src_action;
        } else {
            filepath = row->dst_path;
            action = row->dst_action;
        }
        path_len = row->path_len;

        if (filepath == NULL) {
            continue;
        }

        task = xmalloc(SIZEOF(*task));
        memset64(task, 0, SIZEOF(*task));

        task->path_len = path_len;
        task->path = xmalloc(path_len + 1);
        memcpy64(task->path, filepath, path_len + 1);

        if (row->link_target) {
            task->link_target_len = row->link_target_len;
            task->link_target
                = xmalloc(task->link_target_len + 1);
            memcpy64(task->link_target, row->link_target,
                     task->link_target_len + 1);
        }

        task->action = action;
        task->side = side;

        tasks->items[count] = task;
        count += 1;
    }

    tasks = xrealloc(tasks, STRUCT_ARRAY_SIZE(tasks, Task *, count + 1));
    tasks->count = count;

    if ((tasks->count == 0) && clicked_path) {
        Task *task;
        int32 path_len;

        task = xmalloc(SIZEOF(*task));
        memset64(task, 0, SIZEOF(*task));

        path_len = strlen32(clicked_path);
        task->path_len = path_len;
        task->path = xmalloc(path_len + 1);
        memcpy64(task->path, clicked_path, path_len + 1);

        task->action = clicked_action;
        task->side = side;

        tasks->items[0] = task;
        tasks->count = 1;
    }

    if (tasks->count == 0) {
        free(tasks);
        return NULL;
    }

    return tasks;
}

static int32
cecup_row_compare(const void *a, const void *b) {
    CecupRow *row_a;
    CecupRow *row_b;
    int64 result;

    row_a = *(CecupRow **)a;
    row_b = *(CecupRow **)b;

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
        if (row_a->src_path == NULL && row_b->src_path == NULL) {
            result = 0;
        } else if (row_a->src_path == NULL) {
            result = -1;
        } else if (row_b->src_path == NULL) {
            result = 1;
        } else {
            result = strcmp(row_a->src_path, row_b->src_path);
        }
        break;
    case COL_DST_PATH:
        if (row_a->dst_path == NULL && row_b->dst_path == NULL) {
            result = 0;
        } else if (row_a->dst_path == NULL) {
            result = -1;
        } else if (row_b->dst_path == NULL) {
            result = 1;
        } else {
            result = strcmp(row_a->dst_path, row_b->dst_path);
        }
        break;
    case COL_SIZE_RAW:
        COMPARE(row_a->src_size_raw, row_b->src_size_raw);
        break;
    case COL_MTIME_RAW:
        COMPARE(row_a->src_mtime_raw, row_b->src_mtime_raw);
        break;
    case COL_DST_ACTION:
    case COL_MTIME_TEXT:
    case COL_ROW_PTR:
    case COL_SELECTED:
    case COL_SIZE_TEXT:
    case COL_SRC_ACTION:
    case NUM_COLS:
    default:
        COMPARE(row_a->src_action, row_b->src_action);
        break;
    }

#undef COMPARE

    if (cecup.sort_order == GTK_SORT_DESCENDING) {
        result *= -1;
    }

    return (int32)result;
}

static void
refresh_ui_list(enum RefreshType refresh_type, char *path_to_focus) {
    g_mutex_lock(&cecup.arena_mutex);
    refresh_ui_list_locked(refresh_type, path_to_focus);
    g_mutex_unlock(&cecup.arena_mutex);
}

static void
refresh_ui_list_locked(enum RefreshType refresh_type, char *path_to_focus) {
    int32 count_new = 0;
    int32 count_hard = 0;
    int32 count_update = 0;
    int32 count_equal = 0;
    int32 count_delete = 0;
    int32 count_ignore = 0;
    int64 count_selected = 0;
    int64 total_size_bytes = 0;
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
    GtkTreeIter iter;
    bool valid;

    show_new
        = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_new));
    show_link
        = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_hard));
    show_update
        = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_update));
    show_equal
        = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_equal));
    show_delete
        = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_delete));
    show_ignore
        = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_ignore));

    time_function;

    cecup.rows_visible_len = 0;
    for (int32 i = 0; i < cecup.rows_len; i += 1) {
        CecupRow *row = cecup.rows[i];
        bool visible = false;
        time_block("visible rows loop");

        if (row->selected) {
            count_selected += 1;
        }

        switch (row->src_action) {
        case ACTION_NEW:
            visible = show_new;
            count_new += 1;
            total_size_bytes += row->src_size_raw;
            break;
        case ACTION_HARDLINK:
        case ACTION_SYMLINK:
            visible = show_link;
            count_hard += 1;
            total_size_bytes += row->src_size_raw;
            break;
        case ACTION_UPDATE:
            visible = show_update;
            count_update += 1;
            total_size_bytes += row->src_size_raw;
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
            if (row->dst_action == ACTION_DELETE) {
                visible = show_delete;
                count_delete += 1;
            } else {
                visible = show_ignore;
                count_ignore += 1;
            }
            break;
        case ACTION_DELETE:
        default:
            error("Invalid row->src_action: %u\n", row->src_action);
            fatal(EXIT_FAILURE);
        }

        if (visible && cecup.search_query && (cecup.search_query[0] != '\0')) {
            char *path = row->src_path ? row->src_path : row->dst_path;
            if (strcasestr(path, cecup.search_query) == NULL) {
                visible = false;
            }
        }

        if (visible) {
            cecup.rows_visible[cecup.rows_visible_len] = row;
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
        time_block("sorting");
        if (refresh_type & (REFRESH_FINAL | REFRESH_FILTER_CHANGED)) {
            error("Sorting list...\n");
        }
        qsort64(cecup.rows_visible, cecup.rows_visible_len, SIZEOF(CecupRow *),
                cecup_row_compare);
        if (refresh_type & (REFRESH_FINAL | REFRESH_FILTER_CHANGED)) {
            error("Finished sorting.\n");
        }
    }

    g_signal_handlers_block_by_func(cecup.store, on_sort_changed, NULL);
    gtk_tree_view_set_model(GTK_TREE_VIEW(cecup.l_tree), NULL);
    gtk_tree_view_set_model(GTK_TREE_VIEW(cecup.r_tree), NULL);

    current_store_count
        = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(cecup.store), NULL);

    if (cecup.rows_visible_len > current_store_count) {
        time_block("rows > store count");
        for (int64 i = 0;
             i < (cecup.rows_visible_len - current_store_count);
             i += 1) {
            gtk_list_store_append(cecup.store, &iter);
        }
    } else if (cecup.rows_visible_len < current_store_count) {
        time_block("rows < store count");
        if (cecup.rows_visible_len == 0) {
            gtk_list_store_clear(cecup.store);
        } else {
            if (gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(cecup.store),
                                              &iter, NULL,
                                              (int)cecup.rows_visible_len)) {
                while (gtk_list_store_remove(cecup.store, &iter));
            }
        }
    }

    valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(cecup.store), &iter);
    for (int64 i = 0; i < cecup.rows_visible_len; i += 1) {
        CecupRow *row = cecup.rows_visible[i];
        CecupRow *old_row;
        time_block("store_set loop");

        if (valid) {
            gtk_tree_model_get(GTK_TREE_MODEL(cecup.store), &iter, COL_ROW_PTR, &old_row, -1);

            if (old_row != row) {
                gtk_list_store_set(cecup.store, &iter, COL_SELECTED, row->selected,
                                   COL_ROW_PTR, row, -1);
            }

            valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(cecup.store), &iter);
        }
    }

    gtk_tree_view_set_model(GTK_TREE_VIEW(cecup.l_tree), GTK_TREE_MODEL(cecup.store));
    gtk_tree_view_set_model(GTK_TREE_VIEW(cecup.r_tree), GTK_TREE_MODEL(cecup.store));
    g_signal_handlers_unblock_by_func(cecup.store, on_sort_changed, NULL);

    if ((refresh_type & REFRESH_FINAL) && path_to_focus) {
        for (int32 i = 0; i < cecup.rows_visible_len; i += 1) {
            CecupRow *row = cecup.rows_visible[i];
            char *row_path = row->src_path ? row->src_path : row->dst_path;
            char row_full_rel[MAX_PATH_LENGTH];
            int32 row_rel_len;

            row_rel_len = SNPRINTF(row_full_rel, "/%s", row_path);
            normalize(row_full_rel, &row_rel_len);

            if (!strcmp(row_full_rel, path_to_focus)) {
                GtkTreePath *tree_path = gtk_tree_path_new_from_indices(i, -1);
                gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(cecup.l_tree),
                                             tree_path, NULL, TRUE, 0.5, 0.0);
                gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(cecup.r_tree),
                                             tree_path, NULL, TRUE, 0.5, 0.0);
                gtk_tree_view_set_cursor(GTK_TREE_VIEW(cecup.l_tree),
                                         tree_path, NULL, FALSE);
                gtk_tree_view_set_cursor(GTK_TREE_VIEW(cecup.r_tree),
                                         tree_path, NULL, FALSE);
                gtk_tree_path_free(tree_path);
                break;
            }
        }
    }

    return;
}

static gboolean
refresh_ui_timeout_callback(void *data) {
    (void)data;
    refresh_ui_list(REFRESH_PARTIAL, NULL);
    cecup.refresh_id = 0;
    return G_SOURCE_REMOVE;
}

static gboolean
update_ui_handler(void *data) {
    Message *message = data;

    switch (message->type) {
    case DATA_TYPE_LOG:
    case DATA_TYPE_LOG_CMD:
    case DATA_TYPE_LOG_ERROR: {
        GtkTextIter end;
        GtkTextTagTable *table;

        gtk_text_buffer_get_end_iter(cecup.log_buffer, &end);
        table = gtk_text_buffer_get_tag_table(cecup.log_buffer);

        if (message->type == DATA_TYPE_LOG_ERROR) {
            if (gtk_text_tag_table_lookup(table, "err_red") == NULL) {
                gtk_text_buffer_create_tag(cecup.log_buffer, "err_red",
                                           "foreground", "red", NULL);
            }
            gtk_text_buffer_insert_with_tags_by_name(
                cecup.log_buffer, &end, message->message, -1, "err_red", NULL);
        } else if (message->type == DATA_TYPE_LOG_CMD) {
            if (gtk_text_tag_table_lookup(table, "err_blue") == NULL) {
                gtk_text_buffer_create_tag(cecup.log_buffer, "err_blue",
                                           "foreground", "blue", NULL);
            }
            gtk_text_buffer_insert_with_tags_by_name(
                cecup.log_buffer, &end, message->message, -1, "err_blue", NULL);
        } else {
            gtk_text_buffer_insert(cecup.log_buffer, &end, message->message,
                                   -1);
        }

        gtk_text_view_scroll_to_mark(
            GTK_TEXT_VIEW(cecup.log_view),
            gtk_text_buffer_get_insert(cecup.log_buffer), 0.0, FALSE, 0.0, 0.0);
        break;
    }
    case DATA_TYPE_PROGRESS_PREVIEW:
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(cecup.progress_preview),
                                      message->fraction);
        break;
    case DATA_TYPE_TREE_UPDATE: {
        g_mutex_lock(&cecup.arena_mutex);
        if (cecup.ui_waiting) {
            refresh_ui_list_locked(REFRESH_PARTIAL, NULL);
            cecup.ui_waiting = false;
            g_cond_signal(&cecup.ui_ready_cond);
        } else if (cecup.refresh_id == 0) {
            cecup.refresh_id = g_timeout_add(UI_INTERVAL_MS,
                                             refresh_ui_timeout_callback, NULL);
        }
        g_mutex_unlock(&cecup.arena_mutex);
        break;
    }
    case DATA_TYPE_REMOVE_ROW: {
        char *pattern = message->src_path;
        int32 pattern_len = message->path_len;

        g_mutex_lock(&cecup.arena_mutex);
        for (int32 i = 0; i < cecup.rows_len;) {
            CecupRow *row = cecup.rows[i];
            char *path_test;
            bool match = false;

            if (row->src_path) {
                path_test = row->src_path;
            } else if (row->dst_path) {
                path_test = row->dst_path;
            } else {
                i += 1;
                continue;
            }

            if (pattern[pattern_len - 1] == '/') {
                if (begins_with(path_test, pattern)) {
                    match = true;
                }
            } else {
                if (row->path_len == pattern_len) {
                    if (!memcmp64(path_test, pattern, pattern_len)) {
                        match = true;
                    }
                }
            }

            if (match) {
                IPC_SEND_LOG("Removing %s from list...\n", path_test);
                for (int32 j = i; j < (cecup.rows_len - 1); j += 1) {
                    cecup.rows[j] = cecup.rows[j + 1];
                }
                cecup.rows_len -= 1;
            } else {
                i += 1;
            }
        }
        g_mutex_unlock(&cecup.arena_mutex);

        if (cecup.refresh_id == 0) {
            cecup.refresh_id = g_timeout_add(UI_INTERVAL_MS,
                                             refresh_ui_timeout_callback, NULL);
        }
        break;
    }
    case DATA_TYPE_ENABLE_BUTTONS:
        if (cecup.refresh_id != 0) {
            g_source_remove(cecup.refresh_id);
            cecup.refresh_id = 0;
        }
        refresh_ui_list(REFRESH_FINAL, message->path_to_focus);
        protect_interface_from_user(false);
        break;
    case DATA_TYPE_CLEAR_TREES:
        if (cecup.refresh_id != 0) {
            g_source_remove(cecup.refresh_id);
            cecup.refresh_id = 0;
        }
        gtk_list_store_clear(cecup.store);
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(cecup.progress_preview),
                                      0.0);
        break;
    case DATA_TYPE_REGENERATE_PREVIEW:
        on_preview_clicked(NULL, NULL);
        break;
    default:
        break;
    }

    if (message->message) {
        free(message->message);
    }
    if (message->path_to_focus) {
        free(message->path_to_focus);
    }
    free(message);
    return G_SOURCE_REMOVE;
}

static void
cecup_get_dirs(void) {
    char *full_src;
    char *full_dst;
    char *tmp_src;
    char *tmp_dst;

    cecup.changed_dirs = true;

    tmp_src = (char *)gtk_entry_get_text(GTK_ENTRY(cecup.src_entry));
    tmp_dst = (char *)gtk_entry_get_text(GTK_ENTRY(cecup.dst_entry));

    save_config();

    if ((strlen32(tmp_src) <= 0) || (strlen32(tmp_dst) <= 0)) {
        IPC_SEND_LOG_ERROR("Error: Invalid source and/or destination\n");
        return;
    }

    if ((full_src = realpath(tmp_src, NULL)) == NULL) {
        IPC_SEND_LOG_ERROR("Error getting full path of %s: %s.\n", tmp_src,
                           strerror(errno));
        return;
    }
    if ((full_dst = realpath(tmp_dst, NULL)) == NULL) {
        IPC_SEND_LOG_ERROR("Error getting full path of %s: %s.\n", tmp_dst,
                           strerror(errno));
        return;
    }

    free(cecup.src_base);
    free(cecup.dst_base);

    cecup.src_base = full_src;
    cecup.dst_base = full_dst;

    g_signal_handler_block(cecup.src_entry, cecup.src_entry_id);
    g_signal_handler_block(cecup.dst_entry, cecup.dst_entry_id);

    gtk_entry_set_text(GTK_ENTRY(cecup.src_entry), cecup.src_base);
    gtk_entry_set_text(GTK_ENTRY(cecup.dst_entry), cecup.dst_base);

    g_signal_handler_unblock(cecup.src_entry, cecup.src_entry_id);
    g_signal_handler_unblock(cecup.dst_entry, cecup.dst_entry_id);

    cecup.src_base_len = strlen32(cecup.src_base);
    cecup.dst_base_len = strlen32(cecup.dst_base);
    return;
}

static void
save_config(void) {
    GKeyFile *key;
    char *out;
    gsize len;

    key = g_key_file_new();
    g_key_file_set_string(key, "Paths", "src",
                          gtk_entry_get_text(GTK_ENTRY(cecup.src_entry)));
    g_key_file_set_string(key, "Paths", "dst",
                          gtk_entry_get_text(GTK_ENTRY(cecup.dst_entry)));
    g_key_file_set_string(key, "Tools", "diff",
                          gtk_entry_get_text(GTK_ENTRY(cecup.diff_entry)));
    g_key_file_set_string(key, "Tools", "term",
                          gtk_entry_get_text(GTK_ENTRY(cecup.term_entry)));
    g_key_file_set_boolean(
        key, "Filters", "new",
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_new)));
    g_key_file_set_boolean(
        key, "Filters", "hard",
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_hard)));
    g_key_file_set_boolean(
        key, "Filters", "update",
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_update)));
    g_key_file_set_boolean(
        key, "Filters", "equal",
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_equal)));
    g_key_file_set_boolean(
        key, "Filters", "delete",
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_delete)));
    g_key_file_set_boolean(
        key, "Filters", "ignore",
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_ignore)));
    g_key_file_set_boolean(
        key, "Options", "check_fs",
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.check_fs)));
    g_key_file_set_boolean(
        key, "Options", "delete_after",
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.delete_after)));
    g_key_file_set_boolean(
        key, "Options", "delete_excluded",
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.delete_excluded)));

    out = g_key_file_to_data(key, &len, NULL);
    g_file_set_contents(cecup.config_path, out, (gssize)len, NULL);

    g_free(out);
    g_key_file_free(key);
    return;
}

#if TESTING_aux
#include <assert.h>
#include <string.h>

#include "on.c"

int
main(void) {
    ASSERT(true);
    exit(EXIT_SUCCESS);
}

#endif

#endif /* AUX_C */
