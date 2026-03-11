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
#include <stdlib.h>
#include "i18n.h"
#include "cecup.h"
#include "util.c"
#include "ipc.c"

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_aux 1
#elif !defined(TESTING_aux)
#define TESTING_aux 0
#endif

#define UI_INTERVAL_MS 100

static void
free_update_data(Message *message) {
    g_mutex_lock(&cecup.ui_arena_mutex);
    arena_pop(cecup.ui_arena, message->filepath);
    arena_pop(cecup.ui_arena, message);
    g_mutex_unlock(&cecup.ui_arena_mutex);
    return;
}

static void
free_task_list(TaskList *tasks) {
    if (tasks == NULL) {
        return;
    }

    g_mutex_lock(&cecup.ui_arena_mutex);

    for (int32 i = 0; i < tasks->count; i += 1) {
        Message *task = tasks->items[i];

        if (task->link_target) {
            arena_pop(cecup.ui_arena, task->link_target);
        }
        if (task->message) {
            arena_pop(cecup.ui_arena, task->message);
        }
        arena_pop(cecup.ui_arena, task);
    }

    g_mutex_unlock(&cecup.ui_arena_mutex);
    free(tasks);
    return;
}

static TaskList *
get_target_tasks(int32 side, char *clicked_path,
                 enum CecupAction clicked_action) {
    TaskList *tasks;
    int64 tasks_size = STRUCT_ARRAY_SIZE(tasks, Message *, cecup.rows_len);

    tasks = xmalloc(tasks_size);
    memset64(tasks, 0, tasks_size);
    tasks->count = 0;

    for (int32 i = 0; i < cecup.rows_len; i += 1) {
        CecupRow *row = cecup.rows[i];
        char *file_path;
        int32 path_len;
        enum CecupAction action;
        Message *task;

        if (!(row->selected)) {
            continue;
        }

        if (side == SIDE_LEFT) {
            file_path = row->src_path;
            path_len = row->src_path_len;
            action = row->src_action;
        } else {
            file_path = row->dst_path;
            path_len = row->dst_path_len;
            action = row->dst_action;
        }

        if (file_path == NULL) {
            continue;
        }

        g_mutex_lock(&cecup.ui_arena_mutex);
        task = xarena_push(cecup.ui_arena, ALIGN16(SIZEOF(*task)));
        memset64(task, 0, SIZEOF(*task));

        task->filepath_len = path_len;
        task->filepath = xarena_push(cecup.ui_arena, ALIGN16(path_len + 1));
        memcpy64(task->filepath, file_path, path_len + 1);

        if (row->link_target) {
            task->link_target_len = row->link_target_len;
            task->link_target = xarena_push(cecup.ui_arena,
                                            ALIGN16(task->link_target_len + 1));
            memcpy64(task->link_target, row->link_target,
                     task->link_target_len + 1);
        }
        g_mutex_unlock(&cecup.ui_arena_mutex);

        task->action = action;
        task->side = side;

        tasks->items[tasks->count] = task;
        tasks->count += 1;
    }

    if ((tasks->count == 0) && clicked_path) {
        Message *task;
        int32 path_len;

        g_mutex_lock(&cecup.ui_arena_mutex);
        task = xarena_push(cecup.ui_arena, ALIGN16(SIZEOF(*task)));
        memset64(task, 0, SIZEOF(*task));

        path_len = strlen32(clicked_path);
        task->filepath_len = path_len;
        task->filepath = xarena_push(cecup.ui_arena, ALIGN16(path_len + 1));
        memcpy64(task->filepath, clicked_path, path_len + 1);

        g_mutex_unlock(&cecup.ui_arena_mutex);

        task->action = clicked_action;
        task->side = side;

        tasks->items[tasks->count] = task;
        tasks->count += 1;
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
        COMPARE(row_a->size_raw, row_b->size_raw);
        break;
    case COL_MTIME_RAW:
        COMPARE(row_a->mtime_raw, row_b->mtime_raw);
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
refresh_ui_list(void) {
    int32 count_new = 0;
    int32 count_hard = 0;
    int32 count_update = 0;
    int32 count_equal = 0;
    int32 count_delete = 0;
    int32 count_ignore = 0;
    int64 count_selected = 0;
    int64 total_size_bytes = 0;
    int32 current_store_count;

    char pretty_size[16];
    char stats_text[256];
    char button_label[64];

    bool show_new
        = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_new));
    bool show_link
        = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_hard));
    bool show_update
        = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_update));
    bool show_equal
        = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_equal));
    bool show_delete
        = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_delete));
    bool show_ignore
        = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_ignore));

    cecup.rows_visible_len = 0;
    for (int32 i = 0; i < cecup.rows_len; i += 1) {
        CecupRow *row = cecup.rows[i];
        bool visible = false;

        if (row->selected) {
            count_selected += 1;
        }

        switch (row->src_action) {
        case UI_ACTION_NEW:
            visible = show_new;
            count_new += 1;
            total_size_bytes += row->size_raw;
            break;
        case UI_ACTION_HARDLINK:
        case UI_ACTION_SYMLINK:
            visible = show_link;
            count_hard += 1;
            total_size_bytes += row->size_raw;
            break;
        case UI_ACTION_UPDATE:
            visible = show_update;
            count_update += 1;
            total_size_bytes += row->size_raw;
            break;
        case UI_ACTION_EQUAL:
            visible = show_equal;
            count_equal += 1;
            break;
        case UI_ACTION_DELETED:
            visible = show_delete;
            count_delete += 1;
            break;
        case UI_ACTION_IGNORE:
            visible = show_ignore;
            count_ignore += 1;
            break;
        case UI_ACTION_DELETE:
        case NUM_UI_ACTIONS:
        default:
            error("Invalid row->src_action: %u\n", row->src_action);
            fatal(EXIT_FAILURE);
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

    current_store_count
        = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(cecup.store), NULL);

    if (cecup.rows_visible_len > current_store_count) {
        for (int32 i = 0; i < (cecup.rows_visible_len - current_store_count);
             i += 1) {
            GtkTreeIter iter;
            gtk_list_store_append(cecup.store, &iter);
        }
    } else if (cecup.rows_visible_len < current_store_count) {
        for (int32 i = 0; i < (current_store_count - cecup.rows_visible_len);
             i += 1) {
            GtkTreeIter iter;
            if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(cecup.store),
                                              &iter)) {
                gtk_list_store_remove(cecup.store, &iter);
            }
        }
    }

    for (int32 i = 0; i < cecup.rows_visible_len; i += 1) {
        GtkTreeIter iter;
        CecupRow *row = cecup.rows_visible[i];
        if (gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(cecup.store), &iter,
                                          NULL, i)) {
            gtk_list_store_set(cecup.store, &iter, COL_SELECTED, row->selected,
                               COL_ROW_PTR, row, -1);
        }
    }

    gtk_widget_queue_draw(cecup.l_tree);
    gtk_widget_queue_draw(cecup.r_tree);
    return;
}

static gboolean
refresh_ui_timeout_callback(void *data) {
    (void)data;
    refresh_ui_list();
    cecup.refresh_id = 0;
    return G_SOURCE_REMOVE;
}

static gboolean
update_ui_handler(void *data) {
    Message *message = data;

    switch (message->type) {
    case DATA_TYPE_LOG:
    case DATA_TYPE_LOG_ERROR: {
        GtkTextIter end;

        gtk_text_buffer_get_end_iter(cecup.log_buffer, &end);
        if (message->type == DATA_TYPE_LOG_ERROR) {
            GtkTextTagTable *table;

            table = gtk_text_buffer_get_tag_table(cecup.log_buffer);
            if (gtk_text_tag_table_lookup(table, "err_red") == NULL) {
                gtk_text_buffer_create_tag(cecup.log_buffer, "err_red",
                                           "foreground", "red", NULL);
            }
            gtk_text_buffer_insert_with_tags_by_name(
                cecup.log_buffer, &end, message->message, -1, "err_red", NULL);
        } else {
            gtk_text_buffer_insert(cecup.log_buffer, &end, message->message,
                                   -1);
        }

        gtk_text_view_scroll_to_mark(
            GTK_TEXT_VIEW(cecup.log_view),
            gtk_text_buffer_get_insert(cecup.log_buffer), 0.0, FALSE, 0.0, 0.0);
        break;
    }
    case DATA_TYPE_PROGRESS_RSYNC:
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(cecup.progress_rsync),
                                      message->fraction);
        break;
    case DATA_TYPE_PROGRESS_PREVIEW:
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(cecup.progress_preview),
                                      message->fraction);
        break;
    case DATA_TYPE_TREE_ROW: {
        CecupRow *row;
        char *src_background_color = "#FFFFFF";
        char *dst_background_color = "#FFFFFF";
        char *src_path_final = message->filepath;
        char *dst_path_final = message->filepath;
        enum CecupAction src_action = message->action;
        enum CecupAction dst_action = message->action;

        switch (message->action) {
        case UI_ACTION_NEW:
            src_background_color = "#D4EDDA";
            dst_path_final = NULL;
            break;
        case UI_ACTION_UPDATE:
            src_background_color = "#CCE5FF";
            dst_background_color = "#CCE5FF";
            break;
        case UI_ACTION_HARDLINK:
            src_background_color = "#E2D1F9";
            dst_background_color = "#E2D1F9";
            dst_path_final = NULL;
            break;
        case UI_ACTION_SYMLINK:
            src_background_color = "#FFD1F9";
            dst_background_color = "#FFD1F9";
            dst_path_final = NULL;
            break;
        case UI_ACTION_EQUAL:
            src_background_color = "#F0F0F0";
            dst_background_color = "#F0F0F0";
            break;
        case UI_ACTION_IGNORE:
        case UI_ACTION_DELETE:
            if (message->reason == UI_REASON_IGNORED) {
                src_background_color = "#FFF3CD";
                src_action = UI_ACTION_IGNORE;

                if (gtk_toggle_button_get_active(
                        GTK_TOGGLE_BUTTON(cecup.delete_excluded))) {
                    dst_background_color = "#FFF3CD";
                    dst_action = UI_ACTION_DELETE;
                } else {
                    dst_background_color = "#F0F0F0";
                    dst_action = UI_ACTION_IGNORE;
                }
            } else {
                dst_background_color = "#F8D7DA";
                src_action = UI_ACTION_DELETED;
                dst_action = UI_ACTION_DELETE;
                src_path_final = NULL;
            }
            break;
        case UI_ACTION_DELETED:
        case NUM_UI_ACTIONS:
        default:
            error("Invalid message->action: %u\n", message->action);
            fatal(EXIT_FAILURE);
        }

        g_mutex_lock(&cecup.row_arena_mutex);
        row = xarena_push(cecup.row_arena, ALIGN16(SIZEOF(*row)));
        memset64(row, 0, SIZEOF(*row));
        row->src_action = src_action;
        row->dst_action = dst_action;

        bytes_pretty(row->size_text, message->size);
        row->size_raw = message->size;

        if (message->mtime > 0) {
            time_t t = (time_t)message->mtime;
            struct tm *tm_info = localtime(&t);

            STRFTIME(row->mtime_text, "%Y-%m-%d %H:%M:%S", tm_info);
            row->mtime_raw = message->mtime;
        } else {
            strcpy(row->mtime_text, _("Unknown modification time"));
            row->mtime_raw = 0;
        }

        row->src_color = src_background_color;
        row->dst_color = dst_background_color;
        row->reason = message->reason;

        if (message->link_target) {
            row->link_target_len = message->link_target_len;
            row->link_target = message->link_target;
        }

        if (message->ignore_pattern) {
            row->ignore_pattern_len = message->ignore_pattern_len;
            row->ignore_pattern = message->ignore_pattern;
        }

        if (src_path_final) {
            row->src_path_len = message->filepath_len;
        } else {
            row->src_path_len = 0;
        }
        if (dst_path_final) {
            row->dst_path_len = message->filepath_len;
        } else {
            row->dst_path_len = 0;
        }

        if (src_path_final == NULL) {
            row->src_path = NULL;
            row->dst_path = message->filepath;
        } else if (dst_path_final == NULL) {
            row->src_path = message->filepath;
            row->dst_path = NULL;
        } else {
            row->src_path = message->filepath;
            row->dst_path = message->filepath;
        }

        if (cecup.rows_len >= cecup.rows_capacity) {
            cecup.rows_capacity *= 2;
            cecup.rows = xrealloc(cecup.rows,
                                  cecup.rows_capacity*SIZEOF(CecupRow *));
            cecup.rows_visible = xrealloc(
                cecup.rows_visible, cecup.rows_capacity*SIZEOF(CecupRow *));
        }
        cecup.rows[cecup.rows_len] = row;
        cecup.rows_len += 1;
        g_mutex_unlock(&cecup.row_arena_mutex);

        if (cecup.refresh_id == 0) {
            cecup.refresh_id = g_timeout_add(UI_INTERVAL_MS,
                                             refresh_ui_timeout_callback, NULL);
        }
        break;
    }
    case DATA_TYPE_REMOVE_TREE_ROW: {
        g_mutex_lock(&cecup.row_arena_mutex);
        for (int32 i = 0; i < cecup.rows_len; i += 1) {
            CecupRow *row = cecup.rows[i];
            if ((row->src_path_len == message->filepath_len && row->src_path
                 && strcmp(row->src_path, message->filepath) == 0)
                || (row->dst_path_len == message->filepath_len && row->dst_path
                    && strcmp(row->dst_path, message->filepath) == 0)) {
                for (int32 j = i; j < (cecup.rows_len - 1); j += 1) {
                    cecup.rows[j] = cecup.rows[j + 1];
                }
                cecup.rows_len -= 1;
                arena_pop(cecup.row_arena, row);
                break;
            }
        }
        g_mutex_unlock(&cecup.row_arena_mutex);
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
        refresh_ui_list();
        gtk_widget_set_sensitive(cecup.sync_button, TRUE);
        gtk_widget_set_sensitive(cecup.preview_button, TRUE);
        gtk_widget_set_sensitive(cecup.stop_button, FALSE);
        break;
    case DATA_TYPE_CLEAR_TREES:
        if (cecup.refresh_id != 0) {
            g_source_remove(cecup.refresh_id);
            cecup.refresh_id = 0;
        }
        g_mutex_lock(&cecup.row_arena_mutex);
        arena_reset(cecup.row_arena);
        g_mutex_unlock(&cecup.row_arena_mutex);

        cecup.rows_len = 0;
        cecup.rows_visible_len = 0;
        gtk_list_store_clear(cecup.store);
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(cecup.progress_rsync),
                                      0.0);
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(cecup.progress_preview),
                                      0.0);
        break;
    case DATA_TYPE_REGENERATE_PREVIEW:
        on_preview_clicked(NULL, NULL);
        break;
    default:
        break;
    }

    g_mutex_lock(&cecup.ui_arena_mutex);
    if (message->message) {
        arena_pop(cecup.ui_arena, message->message);
    }
    arena_pop(cecup.ui_arena, message);
    g_mutex_unlock(&cecup.ui_arena_mutex);
    return G_SOURCE_REMOVE;
}

static void
cecup_get_dirs(void) {
    char *full_src;
    char *full_dst;

    cecup.src_base = (char *)gtk_entry_get_text(GTK_ENTRY(cecup.src_entry));
    cecup.dst_base = (char *)gtk_entry_get_text(GTK_ENTRY(cecup.dst_entry));

    save_config();

    if ((strlen32(cecup.src_base) <= 0) || (strlen32(cecup.dst_base) <= 0)) {
        ipc_send_log_error("Error: Invalid source and/or destination\n");
        return;
    }

    if ((full_src = realpath(cecup.src_base, NULL)) == NULL) {
        ipc_send_log_error("Error getting full path of %s: %s.\n",
                           cecup.src_base, strerror(errno));
        return;
    }
    if ((full_dst = realpath(cecup.dst_base, NULL)) == NULL) {
        ipc_send_log_error("Error getting full path of %s: %s.\n",
                           cecup.dst_base, strerror(errno));
        return;
    }

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

#if TESTING_aux
#include <assert.h>
#include <string.h>

int
main(void) {
    ASSERT(true);
    exit(EXIT_SUCCESS);
}

#endif

#endif /* AUX_C */
