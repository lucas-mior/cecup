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

#if !defined(ON_C)
#define ON_C

#include "cecup.h"
#include "work.c"

static void on_menu_open(GtkWidget *m, void *data);
static void on_menu_open_dir(GtkWidget *m, void *data);
static void on_menu_copy_path(GtkWidget *m, void *data);
static void on_menu_apply(GtkWidget *m, void *data);
static void on_menu_diff(GtkWidget *m, void *data);
static void on_menu_rename(GtkWidget *m, void *data);
static void on_menu_delete(GtkWidget *m, void *data);

typedef struct {
    char *label;
    uint32 keyval;
    GdkModifierType mask;
    void (*callback)(GtkWidget *, void *);
    char *path_type;
} CecupMenuItem;

// Note: NEVER delete lines with // clang-format
// clang-format off
static CecupMenuItem context_menu_items[] = {
{N_("📄 Open File"),          0,          0,                                 on_menu_open,      NULL},
{N_("📂 Open Folder"),        0,          0,                                 on_menu_open_dir,  NULL},
{N_("📋 Copy Relative Path"), GDK_KEY_c,  GDK_CONTROL_MASK,                  on_menu_copy_path, "relative"},
{N_("📍 Copy Full Path"),     GDK_KEY_c,  GDK_CONTROL_MASK | GDK_SHIFT_MASK, on_menu_copy_path, "absolute"},
{N_("⏯️ Apply"),              0,          0,                                 on_menu_apply,     NULL},
{N_("🔍 Diff"),               0,          0,                                 on_menu_diff,      NULL},
{N_("✏️ Rename"),              GDK_KEY_F2, 0,                                 on_menu_rename,    NULL},
{N_("🗑️ Delete"),             0,          0,                                 on_menu_delete,    NULL},
{N_("💤 Ignore..."),          0,          0,                                 NULL,              NULL},
};
// clang-format on

static void
on_menu_apply(GtkWidget *m, void *data) {
    Message *message = data;
    TaskList *tasks;

    (void)m;

    if ((tasks = get_target_tasks(message->side, message->filepath,
                                  message->action))) {
        cecup.cancel_sync = 0;
        gtk_widget_set_sensitive(cecup.preview_button, FALSE);
        gtk_widget_set_sensitive(cecup.sync_button, FALSE);
        gtk_widget_set_sensitive(cecup.stop_button, TRUE);
        g_thread_new("bulk_sync", work_rsync_bulk, tasks);
    }

    free_update_data(message);
    return;
}

static void
on_menu_rename(GtkWidget *m, void *data) {
    Message *message = data;
    GtkWidget *tree
        = (message->side == SIDE_LEFT) ? cecup.l_tree : cecup.r_tree;
    GtkTreeSelection *selection
        = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
    GtkTreeModel *model;
    GtkTreeIter iter;

    (void)m;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        GtkTreePath *path = gtk_tree_model_get_path(model, &iter);
        GtkTreeViewColumn *col
            = gtk_tree_view_get_column(GTK_TREE_VIEW(tree), 2);
        gtk_tree_view_set_cursor(GTK_TREE_VIEW(tree), path, col, TRUE);
        gtk_tree_path_free(path);
    }

    free_update_data(message);
    return;
}

static void
on_menu_open(GtkWidget *m, void *data) {
    Message *message = data;
    TaskList *tasks;

    (void)m;

    if ((tasks = get_target_tasks(message->side, message->filepath,
                                  message->action))) {
        for (int32 i = 0; i < tasks->count; i += 1) {
            Message *task = tasks->items[i];
            char full_path[MAX_PATH_LENGTH];
            char *base_path;
            pid_t child;

            if (message->side == SIDE_LEFT) {
                base_path = cecup.src_base;
            } else {
                base_path = cecup.dst_base;
            }

            SNPRINTF(full_path, "%s/%s", base_path, task->filepath);

            switch (child = fork()) {
            case -1:
                error("Error forking: %s.\n", strerror(errno));
                fatal(EXIT_FAILURE);
            case 0:
                execlp("xdg-open", "xdg-open", full_path, (char *)NULL);
                error("Error executing xdg-open: %s.\n", strerror(errno));
                _exit(EXIT_FAILURE);
            default:
                break;
            }
        }
        free_task_list(tasks);
    }

    free_update_data(message);
    return;
}

static void
on_menu_open_dir(GtkWidget *m, void *data) {
    Message *message = data;
    TaskList *tasks;

    (void)m;

    if ((tasks = get_target_tasks(message->side, message->filepath,
                                  message->action))) {
        for (int32 i = 0; i < tasks->count; i += 1) {
            Message *task = tasks->items[i];
            char full_path[MAX_PATH_LENGTH];
            char *dir_path;
            char *base_path;

            if (message->side == SIDE_LEFT) {
                base_path = cecup.src_base;
            } else {
                base_path = cecup.dst_base;
            }

            SNPRINTF(full_path, "%s/%s", base_path, task->filepath);
            dir_path = dirname(full_path);
            {
                char *command[] = {
                    "xdg-open",
                    dir_path,
                    NULL,
                };
                util_command_launch(LENGTH(command), command);
            }
        }
        free_task_list(tasks);
    }

    free_update_data(message);
    return;
}

static void
on_menu_copy_path(GtkWidget *m, void *data) {
    Message *message = data;
    TaskList *tasks;
    char *buffer;
    int64 buffer_size = SIZEMB(2);
    char *write_pointer;
    int64 remaining_capacity;
    char *base_path;

    buffer = xmalloc(buffer_size);
    write_pointer = buffer;
    remaining_capacity = buffer_size - 1;

    if (message->side == SIDE_LEFT) {
        base_path = cecup.src_base;
    } else {
        base_path = cecup.dst_base;
    }

    if ((tasks = get_target_tasks(message->side, message->filepath,
                                  message->action))) {
        for (int32 i = 0; i < tasks->count; i += 1) {
            Message *task = tasks->items[i];
            int64 path_len;
            char path_full[MAX_PATH_LENGTH];
            char *path;
            char *path_type = g_object_get_data(G_OBJECT(m), "path_type");

            if (!strcmp(path_type, "absolute")) {
                char path_relative[MAX_PATH_LENGTH];

                task = tasks->items[i];

                SNPRINTF(path_relative, "%s/%s", base_path, task->filepath);
                if (realpath(path_relative, path_full) == NULL) {
                    ipc_send_log_error("Error resolving full path of %s: %s.\n",
                                       path_relative, strerror(errno));
                    continue;
                }
                path = path_full;
                path_len = strlen64(path_full);
            } else {
                path = task->filepath;
                path_len = task->filepath_len;
            }

            if ((i > 0) && (remaining_capacity > 0)) {
                *write_pointer = '\n';
                write_pointer += 1;
                remaining_capacity -= 1;
            }

            if (remaining_capacity >= path_len) {
                memcpy64(write_pointer, path, path_len);
                write_pointer += path_len;
                remaining_capacity -= path_len;
            }
        }
        *write_pointer = '\0';
        gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),
                               buffer, -1);
        free_task_list(tasks);
    }

    free(buffer);
    free_update_data(message);
    return;
}

static void
on_menu_delete(GtkWidget *m, void *data) {
    Message *message = data;
    TaskList *tasks;
    GtkWidget *dialog;
    int32 count;

    (void)m;

    if ((tasks = get_target_tasks(message->side, message->filepath,
                                  UI_ACTION_DELETE))) {
        count = tasks->count;
        dialog = gtk_message_dialog_new(
            GTK_WINDOW(cecup.gtk_window), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING,
            GTK_BUTTONS_YES_NO, _("Permanently delete %d item(s)?"), count);

        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES) {
            cecup.cancel_sync = 0;
            gtk_widget_set_sensitive(cecup.preview_button, FALSE);
            gtk_widget_set_sensitive(cecup.sync_button, FALSE);
            gtk_widget_set_sensitive(cecup.stop_button, TRUE);
            g_thread_new("work_bulk_sync", work_rsync_bulk, tasks);
        } else {
            free_task_list(tasks);
        }
        gtk_widget_destroy(dialog);
    }

    free_update_data(message);
    return;
}

static void
on_menu_diff(GtkWidget *m, void *data) {
    Message *message = data;
    TaskList *tasks;
    char *diff_tool;
    char *term_cmd;

    (void)m;
    diff_tool = (char *)gtk_entry_get_text(GTK_ENTRY(cecup.diff_entry));
    term_cmd = (char *)gtk_entry_get_text(GTK_ENTRY(cecup.term_entry));

    if ((tasks = get_target_tasks(message->side, message->filepath,
                                  message->action))) {
        for (int32 i = 0; i < tasks->count; i += 1) {
            Message *task = tasks->items[i];
            char *path_src;
            char *path_dst;
            int64 size_dst;
            int64 size_src;

            size_src = strlen64(cecup.src_base) + strlen64(task->filepath) + 2;
            size_dst = strlen64(cecup.dst_base) + strlen64(task->filepath) + 2;

            switch (fork()) {
            case -1:
                ipc_send_log_error("Error forking: %s.\n", strerror(errno));
                break;
            case 0:
                path_src = xmalloc(size_src);
                path_dst = xmalloc(size_dst);

                snprintf2(path_src, size_src, "%s/%s", cecup.src_base,
                          task->filepath);
                snprintf2(path_dst, size_dst, "%s/%s", cecup.dst_base,
                          task->filepath);

                {
                    char buffer[MAX_PATH_LENGTH*2];
                    char *diff_command[] = {
                        term_cmd, "-e", diff_tool, path_dst, path_src, NULL,
                    };

                    execvp(diff_command[0], diff_command);
                    STRING_FROM_ARRAY(buffer, " ", diff_command,
                                      LENGTH(diff_command));
                    error("Error executing\n%s\n%s.\n", buffer,
                          strerror(errno));
                    _exit(1);
                }
            default:
                break;
            }
        }

        free(tasks);
    }

    free_update_data(message);
    return;
}

static void
on_menu_ignore_ext(GtkWidget *m, void *data) {
    Message *message = data;
    TaskList *tasks;
    FILE *fp;

    (void)m;

    do {
        if ((tasks = get_target_tasks(message->side, message->filepath,
                                      message->action))
            == NULL) {
            break;
        }
        if ((fp = fopen(cecup.ignore_path, "a")) == NULL) {
            ipc_send_log_error("Error opening %s: %s.\n", cecup.ignore_path,
                               strerror(errno));
            break;
        }
        for (int32 i = 0; i < tasks->count; i += 1) {
            Message *task = tasks->items[i];
            char *ext;

            if ((ext = strrchr(task->filepath, '.')) != NULL) {
                fprintf(fp, "\n*%s", ext);
            }
        }
        fclose(fp);
        on_preview_clicked(NULL, NULL);
        free_task_list(tasks);
    } while (0);

    free_update_data(message);
    return;
}

static void
on_menu_ignore_dir(GtkWidget *m, void *data) {
    Message *message = data;
    TaskList *tasks;
    FILE *fp;

    (void)m;

    do {
        if ((tasks = get_target_tasks(message->side, message->filepath,
                                      message->action))
            == NULL) {
            break;
        }
        if ((fp = fopen(cecup.ignore_path, "a")) == NULL) {
            break;
        }
        for (int32 i = 0; i < tasks->count; i += 1) {
            Message *task = tasks->items[i];
            char *dir;

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

    if (tasks) {
        free_task_list(tasks);
    }
    free_update_data(message);
    return;
}

static void
on_config_changed(GtkWidget *widget, void *data) {
    (void)widget;
    (void)data;
    cecup_get_dirs();
    save_config();
    return;
}

static void
on_preview_setting_toggled(GtkToggleButton *b, void *data) {
    (void)b;
    (void)data;
    save_config();
    on_preview_clicked(NULL, NULL);
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
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup.filter_equal), FALSE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup.filter_delete), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup.filter_ignore), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup.check_fs), FALSE);
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
    ThreadData *thread_data;

    (void)data;
    (void)b;

    cecup_get_dirs();

    cecup.cancel_sync = 0;
    gtk_widget_set_sensitive(cecup.preview_button, FALSE);
    gtk_widget_set_sensitive(cecup.sync_button, FALSE);
    gtk_widget_set_sensitive(cecup.stop_button, TRUE);

    g_mutex_lock(&cecup.ui_arena_mutex);
    thread_data = xarena_push(cecup.ui_arena, ALIGN16(SIZEOF(*thread_data)));
    memset64(thread_data, 0, SIZEOF(*thread_data));
    g_mutex_unlock(&cecup.ui_arena_mutex);

    thread_data->is_preview = 1;
    thread_data->check_different_fs
        = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.check_fs));
    thread_data->delete_excluded = gtk_toggle_button_get_active(
        GTK_TOGGLE_BUTTON(cecup.delete_excluded));
    thread_data->delete_after
        = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.delete_after));
    g_thread_new("work_rsync", work_rsync, thread_data);
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
    int32 id;
    GtkSortType order;

    (void)data;
    if (gtk_tree_sortable_get_sort_column_id(sortable, &id, &order)) {
        cecup.sort_col = (enum CecupColumn)id;
        cecup.sort_order = order;
        refresh_ui_list();
    }
    return;
}

static void
on_cell_toggled(GtkCellRendererToggle *renderer, char *path_string,
                void *user_data) {
    (void)renderer;
    (void)user_data;

    do {
        GtkTreePath *path;
        GtkTreeIter iter;
        CecupRow *parent_row;
        char *parent_path;
        int64 parent_path_len;
        bool is_root;

        if ((path = gtk_tree_path_new_from_string(path_string)) == NULL) {
            break;
        }
        if (!gtk_tree_model_get_iter(GTK_TREE_MODEL(cecup.store), &iter,
                                     path)) {
            gtk_tree_path_free(path);
            break;
        }

        gtk_tree_model_get(GTK_TREE_MODEL(cecup.store), &iter, COL_ROW_PTR,
                           &parent_row, -1);

        if (parent_row->selected) {
            parent_row->selected = false;
        } else {
            parent_row->selected = true;
        }

        if (parent_row->src_path) {
            parent_path = parent_row->src_path;
        } else {
            parent_path = parent_row->dst_path;
        }

        if (parent_path == NULL) {
            gtk_tree_path_free(path);
            break;
        }

        parent_path_len = strlen64(parent_path);
        is_root = (strcmp(parent_path, "./") == 0);

        for (int32 i = 0; i < cecup.rows_len; i += 1) {
            CecupRow *row = cecup.rows[i];
            char *row_path;
            int64 row_path_len;

            row_path = (row->src_path != NULL) ? row->src_path : row->dst_path;

            if (row_path == NULL) {
                continue;
            }

            row_path_len = strlen64(row_path);

            if (parent_row->selected) {
                if (is_root) {
                    row->selected = 1;
                } else if ((parent_path_len > 0)
                           && (parent_path[parent_path_len - 1] == '/')) {
                    if ((row_path_len >= parent_path_len)
                        && (strncmp64(row_path, parent_path, parent_path_len)
                            == 0)) {
                        row->selected = 1;
                    }
                }
            } else {
                if (is_root) {
                    row->selected = 0;
                } else {
                    if ((parent_path_len > 0)
                        && (parent_path[parent_path_len - 1] == '/')) {
                        if ((row_path_len >= parent_path_len)
                            && (strncmp64(row_path, parent_path,
                                          parent_path_len)
                                == 0)) {
                            row->selected = 0;
                        }
                    }

                    if (strcmp(row_path, "./") == 0) {
                        row->selected = 0;
                    } else if ((row_path_len < parent_path_len)
                               && (row_path[row_path_len - 1] == '/')) {
                        if (strncmp64(parent_path, row_path, row_path_len)
                            == 0) {
                            row->selected = 0;
                        }
                    }
                }
            }
        }
        gtk_tree_path_free(path);
    } while (0);

    refresh_ui_list();
    return;
}

static void
on_ignore_clicked(GtkWidget *b, void *data) {
    GtkWidget *dialog;
    GtkWidget *scroll;
    GtkWidget *view;
    GtkTextBuffer *buffer;
    char *text;

    (void)data;
    dialog = gtk_dialog_new_with_buttons(
        _("Ignore Rules"), GTK_WINDOW(cecup.gtk_window), GTK_DIALOG_MODAL,
        _("_Save"), GTK_RESPONSE_ACCEPT, _("_Close"), GTK_RESPONSE_CLOSE, NULL);
    (void)b;
    gtk_window_set_default_size(GTK_WINDOW(dialog), 600, 500);
    scroll = gtk_scrolled_window_new(NULL, NULL);
    view = gtk_text_view_new();
    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));

    if (g_file_get_contents(cecup.ignore_path, &text, NULL, NULL)) {
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

    g_thread_new("work_fix_fs_worker", work_fix_fs_worker, thread_data);
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
        thread_data->check_different_fs
            = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.check_fs));
        thread_data->delete_after = gtk_toggle_button_get_active(
            GTK_TOGGLE_BUTTON(cecup.delete_after));
        g_thread_new("work_rsync", work_rsync, thread_data);
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
on_tree_key_press(GtkWidget *widget, GdkEventKey *event, void *data) {
    GtkTreeSelection *selection;
    GtkTreeModel *model;
    GtkTreeIter iter;
    gboolean handled = FALSE;

    (void)data;
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(widget));

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        CecupRow *row;
        int32 side;
        char *filepath;
        int64 path_len;
        enum CecupAction action;
        uint32 modifiers;

        side = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "side"));
        gtk_tree_model_get(model, &iter, COL_ROW_PTR, &row, -1);

        filepath = (side == SIDE_LEFT) ? row->src_path : row->dst_path;
        path_len = (side == SIDE_LEFT) ? row->src_path_len : row->dst_path_len;
        action = (side == SIDE_LEFT) ? row->src_action : row->dst_action;
        modifiers = event->state
                    & (GDK_CONTROL_MASK | GDK_SHIFT_MASK | GDK_MOD1_MASK);

        for (int32 i = 0; i < (int32)LENGTH(context_menu_items); i += 1) {
            uint32 key = gdk_keyval_to_lower(event->keyval);
            uint32 target = gdk_keyval_to_lower(context_menu_items[i].keyval);

            if ((context_menu_items[i].keyval != 0) && (key == target)
                && (modifiers == context_menu_items[i].mask)) {
                if (filepath
                    || (context_menu_items[i].callback == on_menu_rename)) {
                    Message *message;

                    g_mutex_lock(&cecup.ui_arena_mutex);
                    message = xarena_push(cecup.ui_arena,
                                          ALIGN16(SIZEOF(*message)));
                    memset64(message, 0, SIZEOF(*message));

                    if (filepath) {
                        message->filepath_len = path_len;
                        message->filepath = xarena_push(cecup.ui_arena,
                                                        ALIGN16(path_len + 1));
                        memcpy64(message->filepath, filepath, path_len + 1);
                    }
                    g_mutex_unlock(&cecup.ui_arena_mutex);

                    message->action = action;
                    message->side = side;

                    if (context_menu_items[i].path_type) {
                        g_object_set_data(G_OBJECT(widget), "path_type",
                                          context_menu_items[i].path_type);
                    }

                    context_menu_items[i].callback(widget, message);
                    handled = TRUE;
                    break;
                }
            }
        }
    }
    return handled;
}

static gboolean
on_tree_button_press(GtkWidget *widget, GdkEventButton *event, void *data) {
    int32 side;
    GtkTreePath *path;

    (void)data;
    side = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "side"));

    if (event->type != GDK_BUTTON_PRESS && event->type != GDK_2BUTTON_PRESS) {
        return FALSE;
    }

    switch (event->button) {
    case GDK_BUTTON_PRIMARY: {
        if (event->type == GDK_2BUTTON_PRESS) {
            if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget),
                                              (gint)event->x, (gint)event->y,
                                              &path, NULL, NULL, NULL)) {
                char *filepath;
                int64 path_len;
                enum CecupAction action;
                int32 row_index = gtk_tree_path_get_indices(path)[0];
                CecupRow *row = cecup.rows_visible[row_index];

                if (side == SIDE_LEFT) {
                    filepath = row->src_path;
                    path_len = row->src_path_len;
                    action = row->src_action;
                } else {
                    filepath = row->dst_path;
                    path_len = row->dst_path_len;
                    action = row->dst_action;
                }

                if (filepath) {
                    Message *message;

                    g_mutex_lock(&cecup.ui_arena_mutex);
                    message = xarena_push(cecup.ui_arena,
                                          ALIGN16(SIZEOF(*message)));
                    memset64(message, 0, SIZEOF(*message));

                    message->filepath_len = path_len;
                    message->filepath
                        = xarena_push(cecup.ui_arena, ALIGN16(path_len + 1));
                    memcpy64(message->filepath, filepath, path_len + 1);
                    g_mutex_unlock(&cecup.ui_arena_mutex);

                    message->action = action;
                    message->side = side;

                    on_menu_open(NULL, message);
                }

                gtk_tree_path_free(path);
                return TRUE;
            }
        }
        break;
    }
    case GDK_BUTTON_SECONDARY: {
        Message *message;
        GtkWidget *menu;
        char *filepath;
        char *other_path;
        int64 path_len;
        enum CecupAction action;

        if (!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget),
                                           (gint)event->x, (gint)event->y,
                                           &path, NULL, NULL, NULL)) {
            break;
        }

        {
            int32 row_index = gtk_tree_path_get_indices(path)[0];
            CecupRow *row = cecup.rows_visible[row_index];

            if (side == SIDE_LEFT) {
                filepath = row->src_path;
                path_len = row->src_path_len;
                other_path = row->dst_path;
                action = row->src_action;
            } else {
                filepath = row->dst_path;
                path_len = row->dst_path_len;
                other_path = row->src_path;
                action = row->dst_action;
            }
        }

        g_mutex_lock(&cecup.ui_arena_mutex);
        message = xarena_push(cecup.ui_arena, ALIGN16(SIZEOF(*message)));
        memset64(message, 0, SIZEOF(*message));

        if (filepath) {
            message->filepath_len = path_len;
            message->filepath
                = xarena_push(cecup.ui_arena, ALIGN16(path_len + 1));
            memcpy64(message->filepath, filepath, path_len + 1);
        }
        g_mutex_unlock(&cecup.ui_arena_mutex);

        message->action = action;
        message->side = side;

        menu = gtk_menu_new();

        for (int32 i = 0; i < (int32)LENGTH(context_menu_items); i += 1) {
            GtkWidget *item;
            char *accel;
            char label[256];

            if (context_menu_items[i].keyval != 0) {
                accel = gtk_accelerator_get_label(context_menu_items[i].keyval,
                                                  context_menu_items[i].mask);
                snprintf2(label, SIZEOF(label), "%s (%s)",
                          _(context_menu_items[i].label), accel);
                g_free(accel);
            } else {
                snprintf2(label, SIZEOF(label), "%s",
                          _(context_menu_items[i].label));
            }

            item = gtk_menu_item_new_with_label(label);

            if (context_menu_items[i].callback == on_menu_apply) {
                g_signal_connect(item, "activate", G_CALLBACK(on_menu_apply),
                                 message);
            } else if (context_menu_items[i].callback == on_menu_diff) {
                if ((filepath == NULL) || (other_path == NULL)) {
                    gtk_widget_set_sensitive(item, FALSE);
                } else {
                    g_signal_connect(item, "activate", G_CALLBACK(on_menu_diff),
                                     message);
                }
            } else if (context_menu_items[i].callback == NULL) {
                GtkWidget *sub = gtk_menu_new();
                GtkWidget *sub_ext;
                GtkWidget *sub_dir;
                char extension_label[32];
                char directory_label[MAX_PATH_LENGTH + 64];
                char *extension_ptr;
                char *directory_ptr;
                char *name = (filepath) ? basename(filepath) : "";
                int64 length = strlen64(name);

                gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), sub);

                if (filepath == NULL) {
                    gtk_widget_set_sensitive(item, FALSE);
                } else {
                    if ((extension_ptr = memchr(name, '.', length))) {
                        extension_ptr = strrchr(extension_ptr, '.');
                        SNPRINTF(extension_label, _("by extension (*%s)"),
                                 extension_ptr);
                    } else {
                        SNPRINTF(extension_label, "%s", _("by extension"));
                    }
                    sub_ext = gtk_menu_item_new_with_label(extension_label);
                    g_signal_connect(sub_ext, "activate",
                                     G_CALLBACK(on_menu_ignore_ext), message);
                    gtk_menu_shell_append(GTK_MENU_SHELL(sub), sub_ext);

                    if ((directory_ptr = g_path_get_dirname(filepath))) {
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
                                     G_CALLBACK(on_menu_ignore_dir), message);
                    gtk_menu_shell_append(GTK_MENU_SHELL(sub), sub_dir);
                }
            } else {
                if ((filepath == NULL)
                    && (context_menu_items[i].callback != on_menu_rename)) {
                    gtk_widget_set_sensitive(item, FALSE);
                } else {
                    if (context_menu_items[i].path_type) {
                        g_object_set_data(G_OBJECT(item), "path_type",
                                          context_menu_items[i].path_type);
                    }
                    g_signal_connect(item, "activate",
                                     G_CALLBACK(context_menu_items[i].callback),
                                     message);
                }
            }
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        }

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

    int32 index;
    int32 side;
    int32 view_column_index;
    int32 number_of_columns;
    char *tip_text = NULL;
    char tip_text_buffer[MAX_PATH_LENGTH*2];
    int64 tip_text_length;

    (void)k;
    (void)d;
    gtk_tree_view_convert_widget_to_bin_window_coords(GTK_TREE_VIEW(w), x, y,
                                                      &bin_x, &bin_y);

    if (!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(w), bin_x, bin_y,
                                       &path_obj, &col, NULL, NULL)) {
        return FALSE;
    }

    index = gtk_tree_path_get_indices(path_obj)[0];
    side = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "side"));
    view_column_index = -1;

    number_of_columns = (int32)gtk_tree_view_get_n_columns(GTK_TREE_VIEW(w));
    for (int32 i = 0; i < number_of_columns; i += 1) {
        if (col == gtk_tree_view_get_column(GTK_TREE_VIEW(w), i)) {
            view_column_index = i;
            break;
        }
    }

    if ((index >= 0) && (index < cecup.rows_visible_len)) {
        CecupRow *row = cecup.rows_visible[index];
        char *filepath;
        enum CecupAction action;

        if (side == SIDE_LEFT) {
            filepath = row->src_path;
            action = row->src_action;
        } else {
            filepath = row->dst_path;
            action = row->dst_action;
        }

        if (filepath == NULL) {
            if (side == SIDE_LEFT) {
                filepath = row->dst_path;
            } else {
                filepath = row->src_path;
            }
        }

        if (filepath == NULL) {
            filepath = "";
        }

        switch (view_column_index) {
        case 1:
            if (side == SIDE_LEFT) {
                tip_text = _(src_action_strings[action]);
            } else {
                tip_text = _(dst_action_strings[action]);
            }
            break;
        case 2: {
            char *translated_reason;

            translated_reason = _(reason_strings[row->reason]);
            if (row->link_target) {
                tip_text_length
                    = SNPRINTF(tip_text_buffer, "%s -> %s: %s", filepath,
                               row->link_target, translated_reason);
            } else {
                tip_text_length = SNPRINTF(tip_text_buffer, "%s: %s", filepath,
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
                                       filepath, (llong)row->size_raw);
            g_mutex_lock(&cecup.ui_arena_mutex);
            tip_text
                = xarena_push(cecup.ui_arena, ALIGN16(tip_text_length + 1));
            g_mutex_unlock(&cecup.ui_arena_mutex);
            memcpy64(tip_text, tip_text_buffer, tip_text_length + 1);
            break;
        }
        case 4: {
            tip_text_length = SNPRINTF(tip_text_buffer, "%s: %s", filepath,
                                       row->mtime_text);
            g_mutex_lock(&cecup.ui_arena_mutex);
            tip_text
                = xarena_push(cecup.ui_arena, ALIGN16(tip_text_length + 1));
            g_mutex_unlock(&cecup.ui_arena_mutex);
            memcpy64(tip_text, tip_text_buffer, tip_text_length + 1);
            break;
        }
        default:
            break;
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
on_path_edited(GtkCellRendererText *renderer, char *path_str, char *new_text,
               void *data) {
    GtkWidget *tree = data;
    GtkTreePath *path;
    GtkTreeIter iter;
    CecupRow *row;
    int32 side;

    (void)renderer;
    side = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(tree), "side"));
    path = gtk_tree_path_new_from_string(path_str);

    if (gtk_tree_model_get_iter(GTK_TREE_MODEL(cecup.store), &iter, path)) {
        char *base_path;
        char *current_rel_path;

        gtk_tree_model_get(GTK_TREE_MODEL(cecup.store), &iter, COL_ROW_PTR,
                           &row, -1);

        if (side == SIDE_LEFT) {
            base_path = cecup.src_base;
            current_rel_path = row->src_path;
        } else {
            base_path = cecup.dst_base;
            current_rel_path = row->dst_path;
        }

        if (current_rel_path && strlen64(new_text) > 0) {
            char old_full[MAX_PATH_LENGTH];
            char new_full[MAX_PATH_LENGTH];
            char *dir_name;

            SNPRINTF(old_full, "%s/%s", base_path, current_rel_path);

            if ((dir_name = g_path_get_dirname(old_full))) {
                SNPRINTF(new_full, "%s/%s", dir_name, new_text);

                if (rename(old_full, new_full) == 0) {
                    ipc_send_log(_("Renamed: %s -> %s\n"), current_rel_path,
                                 new_text);
                    on_preview_clicked(NULL, NULL);
                } else {
                    ipc_send_log_error(_("Error renaming %s: %s\n"),
                                       current_rel_path, strerror(errno));
                }
                g_free(dir_name);
            }
        }
    }

    gtk_tree_path_free(path);
    return;
}

#endif
