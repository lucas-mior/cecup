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

#if !defined(HOOKS_C)
#define HOOKS_C

#include <gtk/gtk.h>
#include <stdlib.h>
#include "i18n.h"
#include "cecup.h"
#include "util.c"
#include "rsync.c"
#include "config.c"

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_hooks 1
#elif !defined(TESTING_hooks)
#define TESTING_hooks 0
#endif

#define UI_INTERVAL_MS 100

static void
free_update_data(UIUpdateData *ui_update_data) {
    g_mutex_lock(&cecup.ui_arena_mutex);
    arena_pop(cecup.ui_arena, ui_update_data->filepath);
    arena_pop(cecup.ui_arena, ui_update_data);
    g_mutex_unlock(&cecup.ui_arena_mutex);
    return;
}

static void
free_task_list(GPtrArray *tasks) {
    if (tasks == NULL) {
        return;
    }

    g_mutex_lock(&cecup.ui_arena_mutex);

    for (int32 i = 0; i < (int32)tasks->len; i += 1) {
        UIUpdateData *task = (UIUpdateData *)g_ptr_array_index(tasks, i);

        if (task->filepath) {
            arena_pop(cecup.ui_arena, task->filepath);
        }
        if (task->src_base) {
            arena_pop(cecup.ui_arena, task->src_base);
        }
        if (task->dst_base) {
            arena_pop(cecup.ui_arena, task->dst_base);
        }
        if (task->term_cmd) {
            arena_pop(cecup.ui_arena, task->term_cmd);
        }
        if (task->diff_tool) {
            arena_pop(cecup.ui_arena, task->diff_tool);
        }
        if (task->message) {
            arena_pop(cecup.ui_arena, task->message);
        }
        if (task->link_target) {
            arena_pop(cecup.ui_arena, task->link_target);
        }
        arena_pop(cecup.ui_arena, task);
    }

    g_mutex_unlock(&cecup.ui_arena_mutex);
    g_ptr_array_unref(tasks);
    return;
}

static GPtrArray *
get_target_tasks(int32 side, char *clicked_path,
                 enum CecupAction clicked_action) {
    GPtrArray *tasks;
    char *shared_src;
    char *shared_dst;
    int64 src_len;
    int64 dst_len;

    tasks = g_ptr_array_new();
    shared_src = (char *)gtk_entry_get_text(GTK_ENTRY(cecup.src_entry));
    shared_dst = (char *)gtk_entry_get_text(GTK_ENTRY(cecup.dst_entry));
    src_len = strlen64(shared_src);
    dst_len = strlen64(shared_dst);

    for (int32 i = 0; i < cecup.rows_count; i += 1) {
        CecupRow *row = cecup.rows[i];
        char *file_path;
        int64 path_len;
        enum CecupAction action;
        UIUpdateData *task;

        if (!(row->selected)) {
            continue;
        }

        if (side == 0) {
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

        task->filepath_length = path_len;
        task->filepath = xarena_push(cecup.ui_arena, ALIGN16(path_len + 1));
        memcpy64(task->filepath, file_path, path_len + 1);

        task->src_base_len = src_len;
        task->src_base = xarena_push(cecup.ui_arena, ALIGN16(src_len + 1));
        memcpy64(task->src_base, shared_src, src_len + 1);

        task->dst_base_len = dst_len;
        task->dst_base = xarena_push(cecup.ui_arena, ALIGN16(dst_len + 1));
        memcpy64(task->dst_base, shared_dst, dst_len + 1);

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
        g_ptr_array_add(tasks, task);
    }

    if ((tasks->len) == 0 && clicked_path) {
        UIUpdateData *task;
        int64 path_len;

        g_mutex_lock(&cecup.ui_arena_mutex);
        task = xarena_push(cecup.ui_arena, ALIGN16(SIZEOF(*task)));
        memset64(task, 0, SIZEOF(*task));

        path_len = strlen64(clicked_path);
        task->filepath_length = path_len;
        task->filepath = xarena_push(cecup.ui_arena, ALIGN16(path_len + 1));
        memcpy64(task->filepath, clicked_path, path_len + 1);

        task->src_base_len = src_len;
        task->src_base = xarena_push(cecup.ui_arena, ALIGN16(src_len + 1));
        memcpy64(task->src_base, shared_src, src_len + 1);

        task->dst_base_len = dst_len;
        task->dst_base = xarena_push(cecup.ui_arena, ALIGN16(dst_len + 1));
        memcpy64(task->dst_base, shared_dst, dst_len + 1);
        g_mutex_unlock(&cecup.ui_arena_mutex);

        task->action = clicked_action;
        task->side = side;
        g_ptr_array_add(tasks, task);
    }

    if (tasks->len == 0) {
        g_ptr_array_unref(tasks);
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
        result = (int64)row_a->size_raw - (int64)row_b->size_raw;
        break;
    case COL_MTIME_RAW:
        result = (int64)row_a->mtime_raw - (int64)row_b->mtime_raw;
        break;
    case COL_DST_ACTION:
    case COL_DST_COLOR:
    case COL_MTIME_TEXT:
    case COL_REASON:
    case COL_ROW_PTR:
    case COL_SELECTED:
    case COL_SIZE_TEXT:
    case COL_SRC_ACTION:
    case COL_SRC_COLOR:
    case NUM_COLS:
    default:
        result = (int64)row_a->src_action - (int64)row_b->src_action;
        break;
    }

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
    int64 total_size_bytes = 0;
    int32 current_store_count;

    char pretty_size[16];
    char stats_text[128];
    char button_label[64];

    bool show_new
        = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_new));
    bool show_hard
        = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_hard));
    bool show_update
        = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_update));
    bool show_equal
        = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_equal));
    bool show_delete
        = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_delete));
    bool show_ignore
        = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_ignore));

    cecup.visible_count = 0;
    for (int32 i = 0; i < cecup.rows_count; i += 1) {
        CecupRow *row = cecup.rows[i];
        bool visible = false;

        switch (row->src_action) {
        case UI_ACTION_NEW:
            visible = show_new;
            count_new += 1;
            total_size_bytes += row->size_raw;
            break;
        case UI_ACTION_HARDLINK:
        case UI_ACTION_SYMLINK:
            visible = show_hard;
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
        case UI_ACTION_NONE:
        case UI_ACTION_DELETE:
        case NUM_UI_ACTIONS:
        default:
            error("Invalid row->src_action: %u\n", row->src_action);
            fatal(EXIT_FAILURE);
        }

        if (visible) {
            cecup.visible_rows[cecup.visible_count] = row;
            cecup.visible_count += 1;
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
    SNPRINTF(stats_text, _("Total Transfer Size: 📦 %s"), pretty_size);
    gtk_label_set_text(GTK_LABEL(cecup.stats_label), stats_text);

    if (cecup.visible_count > 0) {
        qsort64(cecup.visible_rows, cecup.visible_count, SIZEOF(CecupRow *),
                cecup_row_compare);
    }

    current_store_count
        = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(cecup.store), NULL);

    if (cecup.visible_count > current_store_count) {
        for (int32 i = 0; i < (cecup.visible_count - current_store_count);
             i += 1) {
            GtkTreeIter iter;
            gtk_list_store_append(cecup.store, &iter);
        }
    } else if (cecup.visible_count < current_store_count) {
        for (int32 i = 0; i < (current_store_count - cecup.visible_count);
             i += 1) {
            GtkTreeIter iter;
            if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(cecup.store),
                                              &iter)) {
                gtk_list_store_remove(cecup.store, &iter);
            }
        }
    }

    for (int32 i = 0; i < cecup.visible_count; i += 1) {
        GtkTreeIter iter;
        CecupRow *row = cecup.visible_rows[i];
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

static void
on_menu_apply(GtkWidget *m, void *data) {
    UIUpdateData *ui_update_data = data;
    GPtrArray *tasks;

    (void)m;

    if ((tasks
         = get_target_tasks(ui_update_data->side, ui_update_data->filepath,
                            ui_update_data->action))) {
        cecup.cancel_sync = 0;
        gtk_widget_set_sensitive(cecup.preview_button, FALSE);
        gtk_widget_set_sensitive(cecup.sync_button, FALSE);
        gtk_widget_set_sensitive(cecup.stop_button, TRUE);
        g_thread_new("bulk_sync", bulk_sync_worker, tasks);
    }

    free_update_data(ui_update_data);
    return;
}

static void
on_menu_open(GtkWidget *m, void *data) {
    UIUpdateData *ui_update_data = data;
    GPtrArray *tasks;
    char cmd[8192];

    (void)m;

    if ((tasks
         = get_target_tasks(ui_update_data->side, ui_update_data->filepath,
                            ui_update_data->action))) {
        for (int32 i = 0; i < (int32)tasks->len; i += 1) {
            UIUpdateData *task;
            char full_path[MAX_PATH_LENGTH];
            char *base_path;
            char *escaped;

            task = (UIUpdateData *)g_ptr_array_index(tasks, i);
            if (ui_update_data->side == 0) {
                base_path = task->src_base;
            } else {
                base_path = task->dst_base;
            }

            SNPRINTF(full_path, "%s/%s", base_path, task->filepath);
            escaped = shell_escape(full_path);
            SNPRINTF(cmd, "xdg-open '%s' &", escaped);
            system(cmd);
            free(escaped);
        }
        free_task_list(tasks);
    }

    free_update_data(ui_update_data);
    return;
}

static void
on_menu_open_dir(GtkWidget *m, void *data) {
    UIUpdateData *ui_update_data = data;
    GPtrArray *tasks;

    (void)m;

    if ((tasks
         = get_target_tasks(ui_update_data->side, ui_update_data->filepath,
                            ui_update_data->action))) {
        for (int32 i = 0; i < (int32)tasks->len; i += 1) {
            UIUpdateData *task;
            char full_path[MAX_PATH_LENGTH];
            char *dir_path;
            char *base_path;

            task = (UIUpdateData *)g_ptr_array_index(tasks, i);
            if (ui_update_data->side == 0) {
                base_path = task->src_base;
            } else {
                base_path = task->dst_base;
            }

            SNPRINTF(full_path, "%s/%s", base_path, task->filepath);
            if ((dir_path = g_path_get_dirname(full_path))) {
                char *command[] = {
                    "xdg-open",
                    dir_path,
                    NULL,
                };
                util_command_launch(LENGTH(command), command);
                g_free(dir_path);
            }
        }
        free_task_list(tasks);
    }

    free_update_data(ui_update_data);
    return;
}

static void
on_menu_copy_relative(GtkWidget *m, void *data) {
    UIUpdateData *ui_update_data = data;
    GPtrArray *tasks;
    char buffer[1048576];
    char *write_pointer;
    int64 remaining_capacity;

    (void)m;
    write_pointer = buffer;
    remaining_capacity = (int64)sizeof(buffer) - 1;

    if ((tasks
         = get_target_tasks(ui_update_data->side, ui_update_data->filepath,
                            ui_update_data->action))) {
        for (int32 i = 0; i < (int32)tasks->len; i += 1) {
            UIUpdateData *task;
            int64 path_length;

            task = (UIUpdateData *)g_ptr_array_index(tasks, i);
            path_length = task->filepath_length;

            if ((i > 0) && (remaining_capacity > 0)) {
                *write_pointer = '\n';
                write_pointer += 1;
                remaining_capacity -= 1;
            }

            if (remaining_capacity >= path_length) {
                memcpy64(write_pointer, task->filepath, path_length);
                write_pointer += path_length;
                remaining_capacity -= path_length;
            }
        }
        *write_pointer = '\0';
        gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),
                               buffer, -1);
        free_task_list(tasks);
    }

    free_update_data(ui_update_data);
    return;
}

static void
on_menu_copy_full(GtkWidget *m, void *data) {
    UIUpdateData *ui_update_data = data;
    GPtrArray *tasks;
    char *buffer;
    char *write_pointer;
    int64 remaining_capacity;

    (void)m;
    buffer = xmalloc(1048576);
    write_pointer = buffer;
    remaining_capacity = (int64)sizeof(buffer) - 1;

    if ((tasks
         = get_target_tasks(ui_update_data->side, ui_update_data->filepath,
                            ui_update_data->action))) {
        for (int32 i = 0; i < (int32)tasks->len; i += 1) {
            UIUpdateData *task;
            char raw_path[MAX_PATH_LENGTH];
            char full_path[MAX_PATH_LENGTH];
            char *base_path;
            int64 path_length;

            task = (UIUpdateData *)g_ptr_array_index(tasks, i);
            if (ui_update_data->side == 0) {
                base_path = task->src_base;
            } else {
                base_path = task->dst_base;
            }

            SNPRINTF(raw_path, "%s/%s", base_path, task->filepath);
            if (realpath(raw_path, full_path) == NULL) {
                if (raw_path[0] != '/') {
                    char cwd[MAX_PATH_LENGTH];

                    if (getcwd(cwd, SIZEOF(cwd))) {
                        SNPRINTF(full_path, "%s/%s", cwd, raw_path);
                    } else {
                        memcpy64(full_path, raw_path, MAX_PATH_LENGTH);
                    }
                } else {
                    memcpy64(full_path, raw_path, MAX_PATH_LENGTH);
                }
            }

            path_length = strlen64(full_path);

            if (i > 0 && remaining_capacity > 0) {
                *write_pointer = '\n';
                write_pointer += 1;
                remaining_capacity -= 1;
            }

            if (remaining_capacity >= path_length) {
                memcpy64(write_pointer, full_path, path_length);
                write_pointer += path_length;
                remaining_capacity -= path_length;
            }
        }
        *write_pointer = '\0';
        gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),
                               buffer, -1);
        free_task_list(tasks);
    }

    free_update_data(ui_update_data);
    free(buffer);
    return;
}

static void
on_menu_delete(GtkWidget *m, void *data) {
    UIUpdateData *ui_update_data = data;
    GPtrArray *tasks;
    GtkWidget *dialog;
    int32 count;

    (void)m;

    if ((tasks
         = get_target_tasks(ui_update_data->side, ui_update_data->filepath,
                            UI_ACTION_DELETE))) {
        count = (int32)tasks->len;
        dialog = gtk_message_dialog_new(
            GTK_WINDOW(cecup.gtk_window), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING,
            GTK_BUTTONS_YES_NO, _("Permanently delete %d item(s)?"), count);

        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES) {
            cecup.cancel_sync = 0;
            gtk_widget_set_sensitive(cecup.preview_button, FALSE);
            gtk_widget_set_sensitive(cecup.sync_button, FALSE);
            gtk_widget_set_sensitive(cecup.stop_button, TRUE);
            g_thread_new("bulk_delete", bulk_sync_worker, tasks);
        } else {
            free_task_list(tasks);
        }
        gtk_widget_destroy(dialog);
    }

    free_update_data(ui_update_data);
    return;
}

static void
on_menu_diff(GtkWidget *m, void *data) {
    UIUpdateData *ui_update_data = data;
    GPtrArray *tasks;
    char *diff_tool;
    char *term_cmd;
    int64 diff_len;
    int64 term_len;

    (void)m;
    diff_tool = (char *)gtk_entry_get_text(GTK_ENTRY(cecup.diff_entry));
    term_cmd = (char *)gtk_entry_get_text(GTK_ENTRY(cecup.term_entry));
    diff_len = strlen64(diff_tool);
    term_len = strlen64(term_cmd);

    if ((tasks
         = get_target_tasks(ui_update_data->side, ui_update_data->filepath,
                            ui_update_data->action))) {
        for (int32 i = 0; i < (int32)tasks->len; i += 1) {
            UIUpdateData *task;

            task = (UIUpdateData *)g_ptr_array_index(tasks, i);
            g_mutex_lock(&cecup.ui_arena_mutex);
            task->diff_tool_len = diff_len;
            task->diff_tool
                = xarena_push(cecup.ui_arena, ALIGN16(diff_len + 1));
            memcpy64(task->diff_tool, diff_tool, diff_len + 1);

            task->term_cmd_len = term_len;
            task->term_cmd = xarena_push(cecup.ui_arena, ALIGN16(term_len + 1));
            memcpy64(task->term_cmd, term_cmd, term_len + 1);
            g_mutex_unlock(&cecup.ui_arena_mutex);

            g_thread_new("diff_worker", diff_worker, task);
        }
        g_ptr_array_unref(tasks);
    }

    free_update_data(ui_update_data);
    return;
}

static void
on_menu_ignore_ext(GtkWidget *m, void *data) {
    UIUpdateData *ui_update_data = data;
    GPtrArray *tasks;
    FILE *fp;

    (void)m;

    do {
        if ((tasks
             = get_target_tasks(ui_update_data->side, ui_update_data->filepath,
                                ui_update_data->action))
            == NULL) {
            break;
        }
        if ((fp = fopen(cecup.ignore_path, "a")) == NULL) {
            dispatch_log_error("Error opening %s: %s.\n", cecup.ignore_path,
                               strerror(errno));
            break;
        }
        for (int32 i = 0; i < (int32)tasks->len; i += 1) {
            UIUpdateData *task;
            char *ext;

            task = (UIUpdateData *)g_ptr_array_index(tasks, i);
            if ((ext = strrchr(task->filepath, '.')) != NULL) {
                fprintf(fp, "\n*%s", ext);
            }
        }
        fclose(fp);
        on_preview_clicked(NULL, NULL);
        free_task_list(tasks);
    } while (0);

    free_update_data(ui_update_data);
    return;
}

static void
on_menu_ignore_dir(GtkWidget *m, void *data) {
    UIUpdateData *ui_update_data = data;
    GPtrArray *tasks;
    FILE *fp;

    (void)m;

    do {
        if ((tasks
             = get_target_tasks(ui_update_data->side, ui_update_data->filepath,
                                ui_update_data->action))
            == NULL) {
            break;
        }
        if ((fp = fopen(cecup.ignore_path, "a")) == NULL) {
            break;
        }
        for (int32 i = 0; i < (int32)tasks->len; i += 1) {
            UIUpdateData *task;
            char *dir;

            task = (UIUpdateData *)g_ptr_array_index(tasks, i);
            if ((dir = g_path_get_dirname(task->filepath)) != NULL) {
                if (strcmp(dir, ".") != 0) {
                    fprintf(fp, "\n/%s/", dir);
                }
                g_free(dir);
            }
        }
        fclose(fp);
        on_preview_clicked(NULL, NULL);
    } while (0);

    free_task_list(tasks);
    free_update_data(ui_update_data);
    return;
}

static void
on_config_changed(GtkWidget *widget, void *data) {
    (void)widget;
    (void)data;
    save_config();
    return;
}

static void
on_delete_after_toggled(GtkToggleButton *b, void *data) {
    (void)data;

    if (gtk_toggle_button_get_active(b)) {
        GtkWidget *dialog;

        dialog = gtk_message_dialog_new(
            GTK_WINDOW(cecup.gtk_window), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING,
            GTK_BUTTONS_OK,
            _("Warning: 'Sync 100%%' (delete-after) is enabled."
              " Files in the backup folder"
              " that do not exist in the source folder"
              " will be PERMANENTLY DELETED."));
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }

    save_config();
    on_preview_clicked(NULL, NULL);
    return;
}

static void
on_delete_excluded_toggled(GtkToggleButton *b, void *data) {
    (void)data;
    if (gtk_toggle_button_get_active(b)) {
        g_signal_handlers_block_by_func(cecup.delete_after,
                                        on_delete_after_toggled, NULL);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup.delete_after),
                                     TRUE);
        g_signal_handlers_unblock_by_func(cecup.delete_after,
                                          on_delete_after_toggled, NULL);
    }
    save_config();
    on_preview_clicked(NULL, NULL);
    return;
}

static void
on_reset_clicked(GtkWidget *b, void *data) {
    (void)b;
    (void)data;

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup.filter_new), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup.filter_hard), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup.filter_update), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup.filter_equal), FALSE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup.filter_delete), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup.filter_ignore), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup.check_fs), FALSE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup.check_equal), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup.delete_excluded),
                                 FALSE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup.delete_after), FALSE);

    gtk_entry_set_text(GTK_ENTRY(cecup.diff_entry), "unidiff.bash");
    gtk_entry_set_text(GTK_ENTRY(cecup.term_entry), "xterm");
    save_config();
    return;
}

static void
on_stop_clicked(GtkWidget *b, void *data) {
    (void)b;
    (void)data;
    cecup.cancel_sync = 1;
    return;
}

static void
on_preview_clicked(GtkWidget *b, void *data) {
    char *src_path;
    char *dst_path;
    ThreadData *thread_data;

    (void)data;
    src_path = (char *)gtk_entry_get_text(GTK_ENTRY(cecup.src_entry));
    dst_path = (char *)gtk_entry_get_text(GTK_ENTRY(cecup.dst_entry));
    (void)b;

    if (strlen64(src_path) < 1 || strlen64(dst_path) < 1) {
        return;
    }

    cecup.cancel_sync = 0;
    gtk_widget_set_sensitive(cecup.preview_button, FALSE);
    gtk_widget_set_sensitive(cecup.sync_button, FALSE);
    gtk_widget_set_sensitive(cecup.stop_button, TRUE);

    g_mutex_lock(&cecup.ui_arena_mutex);
    thread_data = xarena_push(cecup.ui_arena, ALIGN16(SIZEOF(*thread_data)));
    memset64(thread_data, 0, SIZEOF(*thread_data));
    g_mutex_unlock(&cecup.ui_arena_mutex);

    thread_data->is_preview = 1;
    thread_data->scan_equal
        = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.check_equal));
    thread_data->check_different_fs
        = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.check_fs));
    thread_data->delete_excluded = gtk_toggle_button_get_active(
        GTK_TOGGLE_BUTTON(cecup.delete_excluded));
    thread_data->delete_after
        = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.delete_after));
    strncpy(thread_data->src_path, src_path, MAX_PATH_LENGTH - 1);
    strncpy(thread_data->dst_path, dst_path, MAX_PATH_LENGTH - 1);
    g_thread_new("worker", sync_worker, thread_data);
    return;
}

static void
on_filter_toggled(GtkToggleButton *b, void *data) {
    (void)data;
    (void)b;
    refresh_ui_list();
    save_config();
    return;
}

static void
on_sort_changed(GtkTreeSortable *sortable, void *data) {
    enum CecupColumn id;
    GtkSortType order;

    (void)data;
    if (gtk_tree_sortable_get_sort_column_id(sortable, &id, &order)) {
        cecup.sort_col = id;
        cecup.sort_order = order;
        refresh_ui_list();
    }
    return;
}

static void
on_cell_toggled(GtkCellRendererToggle *cell, char *path_str, void *data) {
    GtkTreeIter iter;
    CecupRow *row;

    (void)cell;
    (void)data;

    if (gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(cecup.store), &iter,
                                            path_str)) {
        gtk_tree_model_get(GTK_TREE_MODEL(cecup.store), &iter, COL_ROW_PTR,
                           &row, -1);
        if (row) {
            row->selected = !row->selected;
            gtk_widget_queue_draw(cecup.l_tree);
            gtk_widget_queue_draw(cecup.r_tree);
        }
    }
    return;
}

static void
on_ignore_clicked(GtkWidget *b, void *data) {
    GtkWidget *dialog;
    GtkWidget *scroll;
    GtkWidget *view;
    GtkTextBuffer *buffer;
    char *text;
    gsize len;

    (void)data;
    dialog = gtk_dialog_new_with_buttons(
        _("Ignore Rules"), GTK_WINDOW(cecup.gtk_window), GTK_DIALOG_MODAL,
        _("_Save"), GTK_RESPONSE_ACCEPT, _("_Close"), GTK_RESPONSE_CLOSE, NULL);
    (void)b;
    gtk_window_set_default_size(GTK_WINDOW(dialog), 600, 500);
    scroll = gtk_scrolled_window_new(NULL, NULL);
    view = gtk_text_view_new();
    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));

    if (g_file_get_contents(cecup.ignore_path, &text, &len, NULL)) {
        gtk_text_buffer_set_text(buffer, text, -1);
        g_free(text);
    }

    gtk_container_add(GTK_CONTAINER(scroll), view);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
                       scroll, TRUE, TRUE, 5);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        GtkTextIter start;
        GtkTextIter end;
        char *content;

        gtk_text_buffer_get_bounds(buffer, &start, &end);
        content = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
        g_file_set_contents(cecup.ignore_path, content, -1, NULL);
        g_free(content);
        on_preview_clicked(NULL, NULL);
    }
    gtk_widget_destroy(dialog);
    return;
}

static void
on_fix_clicked(GtkWidget *b, void *data) {
    char *src_path;
    char *dst_path;
    ThreadData *thread_data;

    (void)b;
    (void)data;
    src_path = (char *)gtk_entry_get_text(GTK_ENTRY(cecup.src_entry));
    dst_path = (char *)gtk_entry_get_text(GTK_ENTRY(cecup.dst_entry));

    if ((strlen64(src_path) <= 0) || (strlen64(dst_path) <= 0)) {
        return;
    }

    cecup.cancel_sync = 0;
    gtk_widget_set_sensitive(cecup.preview_button, FALSE);
    gtk_widget_set_sensitive(cecup.sync_button, FALSE);
    gtk_widget_set_sensitive(cecup.stop_button, TRUE);

    g_mutex_lock(&cecup.ui_arena_mutex);
    thread_data = xarena_push(cecup.ui_arena, ALIGN16(SIZEOF(*thread_data)));
    memset64(thread_data, 0, SIZEOF(*thread_data));
    g_mutex_unlock(&cecup.ui_arena_mutex);

    strncpy(thread_data->src_path, src_path, MAX_PATH_LENGTH - 1);
    strncpy(thread_data->dst_path, dst_path, MAX_PATH_LENGTH - 1);
    g_thread_new("fix_fs_worker", fix_fs_worker, thread_data);
    return;
}

static void
on_invert_clicked(GtkWidget *b, void *data) {
    char path_src[MAX_PATH_LENGTH];
    char path_dst[MAX_PATH_LENGTH];
    char *entry_text;

    (void)b;
    (void)data;

    entry_text = (char *)gtk_entry_get_text(GTK_ENTRY(cecup.src_entry));
    SNPRINTF(path_src, "%s", entry_text);

    entry_text = (char *)gtk_entry_get_text(GTK_ENTRY(cecup.dst_entry));
    SNPRINTF(path_dst, "%s", entry_text);

    gtk_entry_set_text(GTK_ENTRY(cecup.src_entry), path_dst);
    gtk_entry_set_text(GTK_ENTRY(cecup.dst_entry), path_src);
    on_preview_clicked(NULL, NULL);
    return;
}

static void
on_sync_clicked(GtkWidget *b, void *data) {
    char *path_src;
    char *path_dst;
    GtkWidget *dialog;

    (void)data;
    path_src = (char *)gtk_entry_get_text(GTK_ENTRY(cecup.src_entry));
    path_dst = (char *)gtk_entry_get_text(GTK_ENTRY(cecup.dst_entry));
    (void)b;
    dialog = gtk_message_dialog_new(
        GTK_WINDOW(cecup.gtk_window), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
        GTK_BUTTONS_YES_NO, _("Sync %s -> %s?"), path_src, path_dst);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES) {
        ThreadData *thread_data;

        cecup.cancel_sync = 0;
        gtk_widget_set_sensitive(cecup.preview_button, FALSE);
        gtk_widget_set_sensitive(cecup.sync_button, FALSE);
        gtk_widget_set_sensitive(cecup.stop_button, TRUE);

        g_mutex_lock(&cecup.ui_arena_mutex);
        thread_data
            = xarena_push(cecup.ui_arena, ALIGN16(SIZEOF(*thread_data)));
        memset64(thread_data, 0, SIZEOF(*thread_data));
        g_mutex_unlock(&cecup.ui_arena_mutex);

        thread_data->is_preview = 0;
        thread_data->scan_equal = 0;
        thread_data->check_different_fs
            = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.check_fs));
        thread_data->delete_after = gtk_toggle_button_get_active(
            GTK_TOGGLE_BUTTON(cecup.delete_after));
        strncpy(thread_data->src_path, path_src, MAX_PATH_LENGTH - 1);
        strncpy(thread_data->dst_path, path_dst, MAX_PATH_LENGTH - 1);
        g_thread_new("worker", sync_worker, thread_data);
    }
    gtk_widget_destroy(dialog);
    return;
}

static void
on_browse_src(GtkWidget *b, void *data) {
    GtkWidget *dialog;

    (void)data;
    (void)b;
    dialog = gtk_file_chooser_dialog_new(
        _("Src"), GTK_WINDOW(cecup.gtk_window),
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, _("_Cancel"),
        GTK_RESPONSE_CANCEL, _("_Select"), GTK_RESPONSE_ACCEPT, NULL);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *path;

        path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        gtk_entry_set_text(GTK_ENTRY(cecup.src_entry), path);
        g_free(path);
    }
    gtk_widget_destroy(dialog);
    return;
}

static void
on_browse_dst(GtkWidget *b, void *data) {
    GtkWidget *dialog;

    (void)data;
    (void)b;
    dialog = gtk_file_chooser_dialog_new(
        _("Dst"), GTK_WINDOW(cecup.gtk_window),
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, _("_Cancel"),
        GTK_RESPONSE_CANCEL, _("_Select"), GTK_RESPONSE_ACCEPT, NULL);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *path;

        path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        gtk_entry_set_text(GTK_ENTRY(cecup.dst_entry), path);
        g_free(path);
    }
    gtk_widget_destroy(dialog);
    return;
}

static void
on_scroll_sync(GtkAdjustment *s, void *d) {
    double v;

    v = gtk_adjustment_get_value(s);
    if (gtk_adjustment_get_value(GTK_ADJUSTMENT(d)) != v) {
        gtk_adjustment_set_value(GTK_ADJUSTMENT(d), v);
    }
    return;
}

static gboolean
on_tree_button_press(GtkWidget *widget, GdkEventButton *event, void *data) {
    int32 side;
    GtkTreePath *path;

    (void)data;
    side = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "side"));

    if (event->type != GDK_BUTTON_PRESS) {
        return FALSE;
    }

    switch (event->button) {
    case GDK_BUTTON_SECONDARY: {
        UIUpdateData *ui_update_data;
        GtkWidget *menu;
        GtkWidget *item;
        GtkWidget *sub_ext;
        GtkWidget *sub_dir;
        int32 is_disabled;
        char *file_path;
        char *other_path;
        int64 path_len;
        enum CecupAction action;
        char extension_label[32];
        char directory_label[MAX_PATH_LENGTH + 64];
        char *extension_ptr;
        char *directory_ptr;

        if (!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget),
                                           (gint)event->x, (gint)event->y,
                                           &path, NULL, NULL, NULL)) {
            break;
        }

        {
            int32 row_idx;
            CecupRow *row;

            row_idx = gtk_tree_path_get_indices(path)[0];
            row = cecup.visible_rows[row_idx];

            if (side == 0) {
                file_path = row->src_path;
                path_len = row->src_path_len;
                other_path = row->dst_path;
                action = row->src_action;
            } else {
                file_path = row->dst_path;
                path_len = row->dst_path_len;
                other_path = row->src_path;
                action = row->dst_action;
            }
        }

        g_mutex_lock(&cecup.ui_arena_mutex);
        ui_update_data
            = xarena_push(cecup.ui_arena, ALIGN16(SIZEOF(*ui_update_data)));
        memset64(ui_update_data, 0, SIZEOF(*ui_update_data));

        ui_update_data->filepath_length = path_len;
        ui_update_data->filepath
            = xarena_push(cecup.ui_arena, ALIGN16(path_len + 1));
        memcpy64(ui_update_data->filepath, file_path, path_len + 1);
        g_mutex_unlock(&cecup.ui_arena_mutex);

        ui_update_data->action = action;
        ui_update_data->side = side;

        menu = gtk_menu_new();
        item = gtk_menu_item_new_with_label(_("📄 Open File"));
        g_signal_connect(item, "activate", G_CALLBACK(on_menu_open),
                         ui_update_data);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

        item = gtk_menu_item_new_with_label(_("📂 Open Folder"));
        g_signal_connect(item, "activate", G_CALLBACK(on_menu_open_dir),
                         ui_update_data);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

        item = gtk_menu_item_new_with_label(_("📋 Copy Relative Path"));
        if (file_path == NULL) {
            gtk_widget_set_sensitive(item, FALSE);
        } else {
            g_signal_connect(item, "activate",
                             G_CALLBACK(on_menu_copy_relative), ui_update_data);
        }
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

        item = gtk_menu_item_new_with_label(_("📍 Copy Full Path"));
        if (file_path == NULL) {
            gtk_widget_set_sensitive(item, FALSE);
        } else {
            g_signal_connect(item, "activate", G_CALLBACK(on_menu_copy_full),
                             ui_update_data);
        }
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

        item = gtk_menu_item_new_with_label(_("⏯️ Apply"));
        g_signal_connect(item, "activate", G_CALLBACK(on_menu_apply),
                         ui_update_data);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

        item = gtk_menu_item_new_with_label(_("💤 Ignore..."));
        {
            GtkWidget *sub = gtk_menu_new();
            gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), sub);

            if ((extension_ptr = strrchr(ui_update_data->filepath, '.'))) {
                SNPRINTF(extension_label, _("by extension (*%s)"),
                         extension_ptr);
            } else {
                SNPRINTF(extension_label, "%s", _("by extension"));
            }
            sub_ext = gtk_menu_item_new_with_label(extension_label);
            g_signal_connect(sub_ext, "activate",
                             G_CALLBACK(on_menu_ignore_ext), ui_update_data);
            gtk_menu_shell_append(GTK_MENU_SHELL(sub), sub_ext);

            if ((directory_ptr
                 = g_path_get_dirname(ui_update_data->filepath))) {
                if (strcmp(directory_ptr, ".") != 0) {
                    SNPRINTF(directory_label, _("📁 Dir (/%s/)"),
                             directory_ptr);
                } else {
                    SNPRINTF(directory_label, "%s", _("📁 Dir"));
                }
                g_free(directory_ptr);
            } else {
                SNPRINTF(directory_label, "%s", _("📁 Dir"));
            }
            sub_dir = gtk_menu_item_new_with_label(directory_label);
            g_signal_connect(sub_dir, "activate",
                             G_CALLBACK(on_menu_ignore_dir), ui_update_data);
            gtk_menu_shell_append(GTK_MENU_SHELL(sub), sub_dir);
        }
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

        item = gtk_menu_item_new_with_label(_("🔍 Diff"));
        is_disabled
            = (file_path == NULL || other_path == NULL
               || action == UI_ACTION_HARDLINK || action == UI_ACTION_SYMLINK);
        if (is_disabled) {
            gtk_widget_set_sensitive(item, FALSE);
        } else {
            g_signal_connect(item, "activate", G_CALLBACK(on_menu_diff),
                             ui_update_data);
        }
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

        item = gtk_menu_item_new_with_label(_("🗑️ Delete"));
        if (file_path == NULL) {
            gtk_widget_set_sensitive(item, FALSE);
        } else {
            g_signal_connect(item, "activate", G_CALLBACK(on_menu_delete),
                             ui_update_data);
        }
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

        gtk_widget_show_all(menu);
        gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
        gtk_tree_path_free(path);
        return TRUE;
    }
    default:
        break;
    }
    return FALSE;
}

static gboolean
on_tree_tooltip(GtkWidget *w, gint x, gint y, gboolean k, GtkTooltip *t,
                void *d) {
    GtkTreePath *path_obj;
    GtkTreeViewColumn *col;
    gint bin_x;
    gint bin_y;

    int32 idx;
    int32 side;
    int32 view_col_idx;
    int32 number_of_columns;
    char *tip_text = NULL;
    char tip_text_buffer[8192];
    int64 tip_text_length;

    (void)k;
    (void)d;
    gtk_tree_view_convert_widget_to_bin_window_coords(GTK_TREE_VIEW(w), x, y,
                                                      &bin_x, &bin_y);

    if (!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(w), bin_x, bin_y,
                                       &path_obj, &col, NULL, NULL)) {
        return FALSE;
    }

    idx = gtk_tree_path_get_indices(path_obj)[0];
    side = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "side"));
    view_col_idx = -1;

    number_of_columns = (int32)gtk_tree_view_get_n_columns(GTK_TREE_VIEW(w));
    for (int32 i = 0; i < number_of_columns; i += 1) {
        if (col == gtk_tree_view_get_column(GTK_TREE_VIEW(w), i)) {
            view_col_idx = i;
            break;
        }
    }

    if ((idx >= 0) && (idx < cecup.visible_count)) {
        CecupRow *row = cecup.visible_rows[idx];
        char *file_path;
        enum CecupAction action;

        if (side == 0) {
            file_path = row->src_path;
            action = row->src_action;
        } else {
            file_path = row->dst_path;
            action = row->dst_action;
        }

        if (file_path == NULL) {
            if (side == SIDE_LEFT) {
                file_path = row->dst_path;
            } else {
                file_path = row->src_path;
            }
        }

        if (file_path == NULL) {
            file_path = "";
        }

        switch (view_col_idx) {
        case 1: {
            char **strings;
            char *translated_action;
            int64 string_len;

            if (side == 0) {
                strings = src_action_strings;
            } else {
                strings = dst_action_strings;
            }

            translated_action = _(strings[action]);
            string_len = strlen64(translated_action);

            g_mutex_lock(&cecup.ui_arena_mutex);
            tip_text = xarena_push(cecup.ui_arena, ALIGN16(string_len + 1));
            g_mutex_unlock(&cecup.ui_arena_mutex);
            memcpy64(tip_text, translated_action, string_len + 1);
            break;
        }
        case 2: {
            char *translated_reason;

            translated_reason = _(reason_strings[row->reason]);
            if (row->link_target) {
                tip_text_length
                    = SNPRINTF(tip_text_buffer, "%s -> %s: %s", file_path,
                               row->link_target, translated_reason);
            } else {
                tip_text_length = SNPRINTF(tip_text_buffer, "%s: %s", file_path,
                                           translated_reason);
            }
            g_mutex_lock(&cecup.ui_arena_mutex);
            tip_text
                = xarena_push(cecup.ui_arena, ALIGN16(tip_text_length + 1));
            g_mutex_unlock(&cecup.ui_arena_mutex);
            memcpy64(tip_text, tip_text_buffer, tip_text_length + 1);
            break;
        }
        case 3: {
            tip_text_length = SNPRINTF(tip_text_buffer, "%s: %lld bytes",
                                       file_path, (llong)row->size_raw);
            g_mutex_lock(&cecup.ui_arena_mutex);
            tip_text
                = xarena_push(cecup.ui_arena, ALIGN16(tip_text_length + 1));
            g_mutex_unlock(&cecup.ui_arena_mutex);
            memcpy64(tip_text, tip_text_buffer, tip_text_length + 1);
            break;
        }
        case 4: {
            tip_text_length = SNPRINTF(tip_text_buffer, "%s: %s", file_path,
                                       row->mtime_text);
            g_mutex_lock(&cecup.ui_arena_mutex);
            tip_text
                = xarena_push(cecup.ui_arena, ALIGN16(tip_text_length + 1));
            g_mutex_unlock(&cecup.ui_arena_mutex);
            memcpy64(tip_text, tip_text_buffer, tip_text_length + 1);
            break;
        }
        default: {
            break;
        }
        }
    }

    if (tip_text) {
        gtk_tooltip_set_text(t, tip_text);
        g_mutex_lock(&cecup.ui_arena_mutex);
        arena_pop(cecup.ui_arena, tip_text);
        g_mutex_unlock(&cecup.ui_arena_mutex);
        gtk_tree_path_free(path_obj);
        return TRUE;
    }
    gtk_tree_path_free(path_obj);
    return FALSE;
}

static void
add_row_logic(UIUpdateData *data) {
    CecupRow *row;
    char *bg_src = "#FFFFFF";
    char *bg_dst = "#FFFFFF";
    char *src_path_final = data->filepath;
    char *dst_path_final = data->filepath;
    enum CecupAction action_src = data->action;
    enum CecupAction action_dst = data->action;

    switch (data->action) {
    case UI_ACTION_NEW:
        bg_src = "#D4EDDA";
        dst_path_final = NULL;
        break;
    case UI_ACTION_UPDATE:
        bg_src = "#CCE5FF";
        bg_dst = "#CCE5FF";
        break;
    case UI_ACTION_HARDLINK:
        bg_src = "#E2D1F9";
        bg_dst = "#E2D1F9";
        break;
    case UI_ACTION_SYMLINK:
        bg_src = "#FFD1F9";
        bg_dst = "#FFD1F9";
        break;
    case UI_ACTION_EQUAL:
        bg_src = "#F0F0F0";
        bg_dst = "#F0F0F0";
        break;
    case UI_ACTION_DELETE:
        if (data->reason == UI_REASON_IGNORED) {
            bg_src = "#FFF3CD";
            bg_dst = "#FFF3CD";
            action_src = UI_ACTION_IGNORE;
            action_dst = UI_ACTION_DELETE;
        } else {
            bg_dst = "#F8D7DA";
            action_src = UI_ACTION_DELETED;
            action_dst = UI_ACTION_DELETE;
            src_path_final = NULL;
        }
        break;
    case UI_ACTION_NONE:
    case UI_ACTION_DELETED:
    case UI_ACTION_IGNORE:
    case NUM_UI_ACTIONS:
    default:
        error("Invalid data->action: %u\n", data->action);
        fatal(EXIT_FAILURE);
    }

    row = xarena_push(cecup.row_arena, ALIGN16(SIZEOF(*row)));
    memset64(row, 0, SIZEOF(*row));
    row->src_action = action_src;
    row->dst_action = action_dst;

    bytes_pretty(row->size_text, data->size);
    row->size_raw = data->size;

    if (data->mtime > 0) {
        time_t t = (time_t)data->mtime;
        struct tm *tm_info = localtime(&t);
        STRFTIME(row->mtime_text, "%Y-%m-%d %H:%M:%S", tm_info);
        row->mtime_raw = data->mtime;
    } else {
        strcpy(row->mtime_text, _("Unknown modification time"));
        row->mtime_raw = 0;
    }

    row->src_color = bg_src;
    row->dst_color = bg_dst;
    row->reason = data->reason;

    if (data->link_target) {
        row->link_target_len = data->link_target_len;
        row->link_target
            = xarena_push(cecup.row_arena, ALIGN16(row->link_target_len + 1));
        memcpy64(row->link_target, data->link_target, row->link_target_len + 1);
    }

    if (src_path_final) {
        row->src_path_len = data->filepath_length;
    } else {
        row->src_path_len = 0;
    }
    if (dst_path_final) {
        row->dst_path_len = data->filepath_length;
    } else {
        row->dst_path_len = 0;
    }

    if (src_path_final == NULL) {
        row->src_path = NULL;
        row->dst_path = data->filepath;
    } else if (dst_path_final == NULL) {
        row->src_path = data->filepath;
        row->dst_path = NULL;
    } else {
        row->src_path = data->filepath;
        row->dst_path = data->filepath;
    }

    if (cecup.rows_count >= cecup.rows_capacity) {
        int32 new_capacity;
        CecupRow **new_rows;
        CecupRow **new_visible;

        new_capacity = cecup.rows_capacity*2;
        new_rows = xarena_push(cecup.row_arena,
                               ALIGN16(new_capacity*SIZEOF(CecupRow *)));
        new_visible = xarena_push(cecup.row_arena,
                                  ALIGN16(new_capacity*SIZEOF(CecupRow *)));

        memcpy64(new_rows, cecup.rows, cecup.rows_count*SIZEOF(CecupRow *));
        memcpy64(new_visible, cecup.visible_rows,
                 cecup.rows_count*SIZEOF(CecupRow *));

        cecup.rows = new_rows;
        cecup.visible_rows = new_visible;
        cecup.rows_capacity = new_capacity;
    }
    cecup.rows[cecup.rows_count] = row;
    cecup.rows_count += 1;
    return;
}

static gboolean
update_ui_handler(void *user_data) {
    UIUpdateData *data = user_data;

    switch (data->type) {
    case DATA_TYPE_LOG: {
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(cecup.log_buffer, &end);
        gtk_text_buffer_insert(cecup.log_buffer, &end, data->message, -1);
        break;
    }
    case DATA_TYPE_PROGRESS_RSYNC:
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(cecup.progress_rsync),
                                      data->fraction);
        break;
    case DATA_TYPE_PROGRESS_EQUAL:
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(cecup.progress_equal),
                                      data->fraction);
        break;
    case DATA_TYPE_PROGRESS_PREVIEW:
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(cecup.progress_preview),
                                      data->fraction);
        break;
    case DATA_TYPE_TREE_ROW_BATCH: {
        for (int32 i = 0; i < data->batch_count; i += 1) {
            add_row_logic(&data->batch[i]);
        }
        if (cecup.refresh_id == 0) {
            cecup.refresh_id = g_timeout_add(UI_INTERVAL_MS,
                                             refresh_ui_timeout_callback, NULL);
        }
        break;
    }
    case DATA_TYPE_TREE_ROW: {
        add_row_logic(data);
        if (cecup.refresh_id == 0) {
            cecup.refresh_id = g_timeout_add(UI_INTERVAL_MS,
                                             refresh_ui_timeout_callback, NULL);
        }
        break;
    }
    case DATA_TYPE_REMOVE_TREE_ROW: {
        for (int32 i = 0; i < cecup.rows_count; i += 1) {
            CecupRow *row = cecup.rows[i];
            if ((row->src_path_len == data->filepath_length && row->src_path
                 && strcmp(row->src_path, data->filepath) == 0)
                || (row->dst_path_len == data->filepath_length && row->dst_path
                    && strcmp(row->dst_path, data->filepath) == 0)) {
                for (int32 j = i; j < (cecup.rows_count - 1); j += 1) {
                    cecup.rows[j] = cecup.rows[j + 1];
                }
                cecup.rows_count -= 1;
                arena_pop(cecup.row_arena, row);
                break;
            }
        }
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
        arena_reset(cecup.row_arena);
        cecup.rows = xarena_push(
            cecup.row_arena, ALIGN16(cecup.rows_capacity*SIZEOF(CecupRow *)));
        cecup.visible_rows = xarena_push(
            cecup.row_arena, ALIGN16(cecup.rows_capacity*SIZEOF(CecupRow *)));

        cecup.rows_count = 0;
        cecup.visible_count = 0;
        gtk_list_store_clear(cecup.store);
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(cecup.progress_rsync),
                                      0.0);
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(cecup.progress_equal),
                                      0.0);
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(cecup.progress_preview),
                                      0.0);
        break;
    default:
        break;
    }

    g_mutex_lock(&cecup.ui_arena_mutex);
    if (data->type == DATA_TYPE_TREE_ROW_BATCH) {
        arena_pop(cecup.ui_arena, data->batch);
    }
    if (data->message) {
        arena_pop(cecup.ui_arena, data->message);
    }
    arena_pop(cecup.ui_arena, data);
    g_mutex_unlock(&cecup.ui_arena_mutex);
    return G_SOURCE_REMOVE;
}

#if TESTING_hooks
#include <assert.h>
#include <string.h>
#include <stdio.h>

int
main(void) {
    ASSERT(true);
    return 0;
}

#endif

#endif /* HOOKS_C */
