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

#include "util.c"
#include "i18n.h"
#include "cecup.h"
#include "tree_model.c"

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_aux 1
#elif !defined(TESTING_aux)
#define TESTING_aux 0
#endif

#define UI_INTERVAL_MS 100

static void on_sort_changed(GtkSorter *sorter, GtkSorterChange change, void *data);

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
free_task_list(TaskList *tasks) {
    if (tasks == NULL) {
        return;
    }

    for (int32 i = 0; i < tasks->count; i += 1) {
        Task *task = tasks->items[i];

        if (task->link_target) {
            XFREE(task->link_target);
        }
        if (task->message) {
            XFREE(task->message);
        }
        XFREE(task);
    }

    XFREE(tasks);
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

        if (side == L) {
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
        XFREE(tasks);
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
    refresh_ui_list_locked(refresh_type, path_to_focus);
    return;
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

    struct timespec t0;
    struct timespec t1;

    clock_gettime(CLOCK_MONOTONIC_RAW, &t0);

    current_store_count = (int64)g_list_model_get_n_items(cecup.store);

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

    cecup.rows_visible_len = 0;
    for (int32 i = 0; i < cecup.rows_len; i += 1) {
        CecupRow *row = cecup.rows[i];
        bool visible = false;

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
        case ACTION_LAST:
        default:
            error("Invalid row->src_action: %u\n", row->src_action);
            fatal(EXIT_FAILURE);
        }

        if (visible && cecup.search_query && (cecup.search_query[0] != '\0')) {
            char *path;

            if (row->src_path) {
                path = row->src_path;
            } else {
                path = row->dst_path;
            }

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
        qsort64(cecup.rows_visible, cecup.rows_visible_len, SIZEOF(CecupRow *),
                cecup_row_compare);
    }

    cecup_list_model_update(CECUP_LIST_MODEL(cecup.store),
                            (int32)current_store_count,
                            cecup.rows_visible_len);

    if ((refresh_type & REFRESH_FINAL) && path_to_focus) {
        for (int32 i = 0; i < cecup.rows_visible_len; i += 1) {
            CecupRow *row = cecup.rows_visible[i];
            char *row_path;
            char row_full_rel[MAX_PATH_LENGTH];
            int32 row_rel_len;

            row = cecup.rows_visible[i];

            if (row->src_path) {
                row_path = row->src_path;
            } else {
                row_path = row->dst_path;
            }

            row_rel_len = SNPRINTF(row_full_rel, "/%s", row_path);
            normalize(row_full_rel, &row_rel_len);

            if (!strcmp(row_full_rel, path_to_focus)) {
                GtkSelectionModel *sel_l;
                GtkSelectionModel *sel_r;

                sel_l = gtk_column_view_get_model(GTK_COLUMN_VIEW(cecup.tree[L]));
                sel_r = gtk_column_view_get_model(GTK_COLUMN_VIEW(cecup.tree[R]));

                gtk_selection_model_select_item(sel_l, (uint32)i, TRUE);
                gtk_selection_model_select_item(sel_r, (uint32)i, TRUE);
                break;
            }
        }
    }

    clock_gettime(CLOCK_MONOTONIC_RAW, &t1);
    PRINT_TIMINGS(cecup.rows_visible_len, (char *)__func__, t0, t1);
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
    Message *message;
    MessageBatch *batch;
    GtkTextIter end;
    GtkTextTagTable *table;
    char *pattern;
    int32 pattern_len;
    int32 current_store_count;
    CecupRow *row;
    char *final_src_path;
    char *final_dst_path;
    int32 slash;
    time_t unix_timestamp;
    struct tm *time_information;

    message = data;

    switch (message->type) {
    case DATA_TYPE_LOG:
    case DATA_TYPE_LOG_CMD:
    case DATA_TYPE_LOG_ERROR:
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
            gtk_text_buffer_insert(cecup.log_buffer, &end, message->message, -1);
        }

        gtk_text_view_scroll_to_mark(
            GTK_TEXT_VIEW(cecup.log_view),
            gtk_text_buffer_get_insert(cecup.log_buffer), 0.0, FALSE, 0.0, 0.0);
        break;
    case DATA_TYPE_PROGRESS_PREVIEW:
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(cecup.progress_preview),
                                      message->fraction);
        break;
    case DATA_TYPE_TREE_UPDATE:
        if (cecup.refresh_id == 0) {
            cecup.refresh_id = g_timeout_add(UI_INTERVAL_MS,
                                             refresh_ui_timeout_callback, NULL);
        }
        break;
    case DATA_TYPE_REMOVE_ROW:
        pattern = message->src_path;
        pattern_len = message->path_len;

        for (int32 i = 0; i < cecup.rows_len;) {
            CecupRow *row_test;
            char *path_test;
            bool match;

            row_test = cecup.rows[i];
            match = false;

            if (row_test->src_path) {
                path_test = row_test->src_path;
            } else if (row_test->dst_path) {
                path_test = row_test->dst_path;
            } else {
                i += 1;
                continue;
            }

            if (pattern[pattern_len - 1] == '/') {
                if (BEGINS_WITH(path_test, pattern, pattern_len)) {
                    match = true;
                }
            } else {
                if (row_test->path_len == pattern_len) {
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

        if (cecup.refresh_id == 0) {
            cecup.refresh_id = g_timeout_add(UI_INTERVAL_MS,
                                             refresh_ui_timeout_callback, NULL);
        }
        break;
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
        g_mutex_lock(&cecup.arena_mutex);

        current_store_count = (int32)g_list_model_get_n_items(cecup.store);

        arena_reset(cecup.arena);
        cecup.rows_len = 0;
        cecup.rows_visible_len = 0;

        cecup_list_model_update(CECUP_LIST_MODEL(cecup.store), current_store_count, 0);

        g_mutex_unlock(&cecup.arena_mutex);

        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(cecup.progress_preview),
                                      0.0);
        break;
    case DATA_TYPE_REGENERATE_PREVIEW:
        on_preview_clicked(NULL, NULL);
        break;
    case DATA_TYPE_ADD_ROW:
        batch = (MessageBatch *)data;

        for (int32 i = 0; i < batch->count; i += 1) {
            message = batch->messages[i];
            final_src_path = NULL;
            final_dst_path = NULL;
            slash = 0;

            if (message->src_path) {
                if (message->is_dir) {
                    slash = 1;
                }

                final_src_path = xarena_push(cecup.arena, message->path_len + slash + 1);
                memcpy64(final_src_path, message->src_path, message->path_len + 1);

                if (message->is_dir) {
                    if (final_src_path[message->path_len - 1] != '/') {
                        final_src_path[message->path_len] = '/';
                        final_src_path[message->path_len + 1] = '\0';
                        message->path_len += 1;
                    }
                }

                if (message->dst_path) {
                    final_dst_path = final_src_path;
                }
            } else if (message->dst_path) {
                if (message->is_dir) {
                    slash = 1;
                }

                final_dst_path = xarena_push(cecup.arena, message->path_len + slash + 1);
                memcpy64(final_dst_path, message->dst_path, message->path_len + 1);

                if (message->is_dir) {
                    if (final_dst_path[message->path_len - 1] != '/') {
                        final_dst_path[message->path_len] = '/';
                        final_dst_path[message->path_len + 1] = '\0';
                        message->path_len += 1;
                    }
                }
            } else {
                error("Error: both src_path and dst_path are NULL. "
                      "(action=%s) (reason=%s)\n",
                      ACTION_str(message->action), REASON_str(message->reason));
                fatal(EXIT_FAILURE);
            }

            row = xarena_push(cecup.arena, SIZEOF(*row));
            memset64(row, 0, SIZEOF(*row));

            row->src_action = message->action;
            row->dst_action = message->action;
            row->reason = message->reason;

            switch (message->action) {
            case ACTION_IGNORE:
                row->src_action = ACTION_IGNORE;
                if (final_dst_path) {
                    if (message->delete_excluded) {
                        row->dst_action = ACTION_DELETE;
                    } else {
                        row->dst_action = ACTION_IGNORE;
                    }
                } else {
                    row->dst_action = ACTION_IGNORE;
                }
                break;
            case ACTION_DELETE:
                row->dst_action = ACTION_DELETE;
                row->src_action = ACTION_IGNORE;
                break;
            case ACTION_DELETED:
            case ACTION_EQUAL:
            case ACTION_HARDLINK:
            case ACTION_SYMLINK:
            case ACTION_NEW:
            case ACTION_UPDATE:
            case ACTION_LAST:
            default:
                break;
            }

            bytes_pretty(row->src_size_text, message->src_size);
            bytes_pretty(row->dst_size_text, message->dst_size);
            row->src_size_raw = message->src_size;
            row->dst_size_raw = message->dst_size;

            if (message->src_mtime > 0) {
                unix_timestamp = (time_t)message->src_mtime;
                time_information = localtime(&unix_timestamp);
                STRFTIME(row->src_mtime_text, "%Y-%m-%d %H:%M:%S", time_information);
                row->src_mtime_raw = message->src_mtime;
            }

            if (message->dst_mtime > 0) {
                unix_timestamp = (time_t)message->dst_mtime;
                time_information = localtime(&unix_timestamp);
                STRFTIME(row->dst_mtime_text, "%Y-%m-%d %H:%M:%S", time_information);
                row->dst_mtime_raw = message->dst_mtime;
            }

            if (message->link_target) {
                row->link_target_len = message->link_target_len;
                row->link_target = xarena_push(cecup.arena, row->link_target_len + 1);
                memcpy64(row->link_target, message->link_target, row->link_target_len + 1);
            }

            if (message->ignore_pattern) {
                row->ignore_pattern_len = message->ignore_pattern_len;
                row->ignore_pattern = xarena_push(cecup.arena, row->ignore_pattern_len + 1);
                memcpy64(row->ignore_pattern, message->ignore_pattern, row->ignore_pattern_len + 1);
            }

            row->src_path = final_src_path;
            row->dst_path = final_dst_path;
            row->path_len = message->path_len;

            if (cecup.rows_len >= cecup.rows_capacity) {
                cecup.rows_capacity *= 2;
                cecup.rows = xrealloc(cecup.rows, cecup.rows_capacity * SIZEOF(CecupRow *));
                cecup.rows_visible = xrealloc(cecup.rows_visible, cecup.rows_capacity * SIZEOF(CecupRow *));
            }

            cecup.rows[cecup.rows_len] = row;
            cecup.rows_len += 1;

            if ((cecup.rows_len % 100000) == 0) {
                cecup.ui_waiting = true;
                if (cecup.refresh_id == 0) {
                    cecup.refresh_id = g_timeout_add(UI_INTERVAL_MS,
                                                     refresh_ui_timeout_callback, NULL);
                }
            }

            if (message->message) {
                XFREE(message->message);
            }
            if (message->path_to_focus) {
                XFREE(message->path_to_focus);
            }
            if (message->src_path) {
                XFREE(message->src_path);
            }
            if (message->dst_path) {
                XFREE(message->dst_path);
            }
            if (message->link_target) {
                XFREE(message->link_target);
            }
            if (message->ignore_pattern) {
                XFREE(message->ignore_pattern);
            }
            XFREE(message);
        }

        XFREE(batch);
        return G_SOURCE_REMOVE;
    default:
        break;
    }

    if (message->message) {
        XFREE(message->message);
    }
    if (message->path_to_focus) {
        XFREE(message->path_to_focus);
    }
    if (message->src_path) {
        XFREE(message->src_path);
    }
    if (message->dst_path) {
        XFREE(message->dst_path);
    }
    if (message->link_target) {
        XFREE(message->link_target);
    }
    if (message->ignore_pattern) {
        XFREE(message->ignore_pattern);
    }
    XFREE(message);

    return G_SOURCE_REMOVE;
}

static void
cecup_reset_dir(int32 side) {
    if (side == L) {
        gtk_editable_set_text(GTK_EDITABLE(cecup.src_entry), "./");
    } else {
        gtk_editable_set_text(GTK_EDITABLE(cecup.dst_entry), "./");
    }
    return;
}

static void
cecup_get_dirs(void) {
    char *full_src;
    char *full_dst;
    char *tmp_src;
    char *tmp_dst;

    cecup.changed_dirs = true;

    tmp_src = (char *)gtk_editable_get_text(GTK_EDITABLE(cecup.src_entry));
    tmp_dst = (char *)gtk_editable_get_text(GTK_EDITABLE(cecup.dst_entry));

    save_config();

    if (strlen32(tmp_src) <= 0) {
        IPC_SEND_LOG_ERROR("Error: Invalid source directory.\n");
        cecup_reset_dir(L);
        return;
    }
    if (strlen32(tmp_dst) <= 0) {
        IPC_SEND_LOG_ERROR("Error: Invalid source directory.\n");
        cecup_reset_dir(R);
        return;
    }

    if ((full_src = realpath(tmp_src, NULL)) == NULL) {
        IPC_SEND_LOG_ERROR("Error getting full path of %s: %s.\n", tmp_src,
                           strerror(errno));
        cecup_reset_dir(L);
        return;
    }
    if ((full_dst = realpath(tmp_dst, NULL)) == NULL) {
        IPC_SEND_LOG_ERROR("Error getting full path of %s: %s.\n", tmp_dst,
                           strerror(errno));
        cecup_reset_dir(R);
        return;
    }

    XFREE(cecup.src_base);
    XFREE(cecup.dst_base);

    cecup.src_base = full_src;
    cecup.dst_base = full_dst;

    g_signal_handler_block(cecup.src_entry, cecup.src_entry_id);
    g_signal_handler_block(cecup.dst_entry, cecup.dst_entry_id);

    gtk_editable_set_text(GTK_EDITABLE(cecup.src_entry), cecup.src_base);
    gtk_editable_set_text(GTK_EDITABLE(cecup.dst_entry), cecup.dst_base);

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
                          gtk_editable_get_text(GTK_EDITABLE(cecup.src_entry)));
    g_key_file_set_string(key, "Paths", "dst",
                          gtk_editable_get_text(GTK_EDITABLE(cecup.dst_entry)));
    g_key_file_set_string(key, "Tools", "diff",
                          gtk_editable_get_text(GTK_EDITABLE(cecup.diff_entry)));
    g_key_file_set_string(key, "Tools", "term",
                          gtk_editable_get_text(GTK_EDITABLE(cecup.term_entry)));
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
        gtk_check_button_get_active(GTK_CHECK_BUTTON(cecup.check_fs)));
    g_key_file_set_boolean(
        key, "Options", "delete_after",
        gtk_check_button_get_active(GTK_CHECK_BUTTON(cecup.delete_after)));
    g_key_file_set_boolean(
        key, "Options", "delete_excluded",
        gtk_check_button_get_active(GTK_CHECK_BUTTON(cecup.delete_excluded)));

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
