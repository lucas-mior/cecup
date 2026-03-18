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
#include "aux.c"

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_on 1
#elif !defined(TESTING_on)
#define TESTING_on 0
#endif

static void on_menu_open_item(GtkWidget *m, void *data);
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

static CecupMenuItem context_menu_items[] = {
{N_("📄 Open File"),          0,          0,                                 on_menu_open_item, "file"},
{N_("📂 Open Folder"),        0,          0,                                 on_menu_open_item, "folder"},
{N_("📋 Copy Relative Path"), GDK_KEY_c,  GDK_CONTROL_MASK,                  on_menu_copy_path, "relative"},
{N_("📍 Copy Full Path"),     GDK_KEY_c,  GDK_CONTROL_MASK | GDK_SHIFT_MASK, on_menu_copy_path, "absolute"},
{N_("⏯️ Apply"),              0,          0,                                 on_menu_apply,     NULL},
{N_("🔍 Diff"),               0,          0,                                 on_menu_diff,      NULL},
{N_("✏️ Rename"),              GDK_KEY_F2, 0,                                 on_menu_rename,    NULL},
{N_("🗑️ Delete"),             0,          0,                                 on_menu_delete,    NULL},
{N_("💤 Ignore..."),          0,          0,                                 NULL,              NULL},
};

static void
on_log_copy(GtkWidget *m, void *data) {
    char *which = data;
    GtkTextIter start;
    GtkTextIter end;
    char *text;
    int line_num;

    if (!strcmp(which, "all")) {
        gtk_text_buffer_get_bounds(cecup.log_buffer, &start, &end);
    } else if (!strcmp(which, "line")) {
        line_num = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(m), "line_num"));
        gtk_text_buffer_get_iter_at_line(cecup.log_buffer, &start, line_num);
        end = start;

        if (!gtk_text_iter_ends_line(&end)) {
            gtk_text_iter_forward_to_line_end(&end);
        }
    } else {
        return;
    }

    if ((text = gtk_text_buffer_get_text(cecup.log_buffer, &start, &end, FALSE))) {
        gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), text, -1);
        g_free(text);
    }

    return;
}

static gboolean
on_log_button_press(GtkWidget *widget, GdkEventButton *event, void *data) {
    (void)data;
    if (event->type == GDK_BUTTON_PRESS && event->button == GDK_BUTTON_SECONDARY) {
        GtkWidget *menu;
        GtkWidget *item;
        GtkTextIter iter;
        int32 x;
        int32 y;

        menu = gtk_menu_new();

        item = gtk_menu_item_new_with_label(_("📋 Copy Whole Log"));
        g_signal_connect(item, "activate", G_CALLBACK(on_log_copy), "all");
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

        gtk_text_view_window_to_buffer_coords(GTK_TEXT_VIEW(widget),
                                              GTK_TEXT_WINDOW_WIDGET,
                                              (int32)event->x, (int32)event->y,
                                              &x, &y);
        gtk_text_view_get_iter_at_location(GTK_TEXT_VIEW(widget), &iter, x, y);

        item = gtk_menu_item_new_with_label(_("📍 Copy This Line"));
        g_object_set_data(G_OBJECT(item), "line_num", GINT_TO_POINTER(gtk_text_iter_get_line(&iter)));
        g_signal_connect(item, "activate", G_CALLBACK(on_log_copy), "line");
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

        gtk_widget_show_all(menu);
        gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
        return TRUE;
    }
    return FALSE;
}

static void
on_menu_apply(GtkWidget *m, void *data) {
    Message *message = data;
    TaskList *tasks;

    (void)m;

    if ((tasks = get_target_tasks(message->side, message->src_path,
                                  message->action))) {
        protect_interface_from_user(true);
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
        GtkTreePath *tree_path = gtk_tree_model_get_path(model, &iter);
        GtkTreeViewColumn *col
            = gtk_tree_view_get_column(GTK_TREE_VIEW(tree), 2);
        gtk_tree_view_set_cursor(GTK_TREE_VIEW(tree), tree_path, col, TRUE);
        gtk_tree_path_free(tree_path);
    }

    free_update_data(message);
    return;
}

static void
on_menu_open_item(GtkWidget *m, void *data) {
    Message *message = data;
    TaskList *tasks;
    char *path_type;

    if (m) {
        path_type = g_object_get_data(G_OBJECT(m), "path_type");
    } else {
        path_type = NULL;
    }

    if ((tasks = get_target_tasks(message->side, message->src_path,
                                  message->action))) {
        for (int32 i = 0; i < tasks->count; i += 1) {
            Task *task = tasks->items[i];
            char full_path[MAX_PATH_LENGTH];
            char *base_path;
            int32 n;

            if (message->side == SIDE_LEFT) {
                base_path = cecup.src_base;
            } else {
                base_path = cecup.dst_base;
            }

            n = SNPRINTF(full_path, "%s/%s", base_path, task->path);

            if (path_type && (strcmp(path_type, "folder") == 0)) {
                int32 path_len = n;
                dirname2(full_path, full_path, &path_len);
            }

            {
                char cmd[MAX_PATH_LENGTH];
                char *command[] = {
                    "xdg-open",
                    full_path,
                    NULL,
                };
                STRING_FROM_ARRAY(cmd, " ", command, LENGTH(command));
                IPC_SEND_LOG("Launching %s...\n", cmd);
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

    if ((tasks = get_target_tasks(message->side, message->src_path,
                                  message->action))) {
        for (int32 i = 0; i < tasks->count; i += 1) {
            Task *task = tasks->items[i];
            int32 path_len;
            char path_full[MAX_PATH_LENGTH];
            char *path;
            char *path_type = g_object_get_data(G_OBJECT(m), "path_type");

            if (!strcmp(path_type, "absolute")) {
                char path_relative[MAX_PATH_LENGTH];

                task = tasks->items[i];

                SNPRINTF(path_relative, "%s/%s", base_path, task->path);
                if (realpath(path_relative, path_full) == NULL) {
                    IPC_SEND_LOG_ERROR("Error resolving full path of %s: %s.\n",
                                       path_relative, strerror(errno));
                    continue;
                }
                path = path_full;
                path_len = strlen32(path_full);
            } else {
                path = task->path;
                path_len = task->path_len;
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

    if ((tasks
         = get_target_tasks(message->side, message->src_path, ACTION_DELETE))) {
        for (int32 i = 0; i < tasks->count; i += 1) {
            tasks->items[i]->action = ACTION_DELETE;
        }

        count = tasks->count;
        dialog = gtk_message_dialog_new(
            GTK_WINDOW(cecup.gtk_window), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING,
            GTK_BUTTONS_YES_NO, _("Permanently delete %d item(s)?"), count);

        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES) {
            protect_interface_from_user(true);
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

    if ((tasks = get_target_tasks(message->side, message->src_path,
                                  message->action))) {
        for (int32 i = 0; i < tasks->count; i += 1) {
            Task *task = tasks->items[i];
            char *path_src;
            char *path_dst;
            int64 size_dst;
            int64 size_src;

            size_src = strlen32(cecup.src_base) + strlen32(task->path) + 2;
            size_dst = strlen32(cecup.dst_base) + strlen32(task->path) + 2;

            switch (fork()) {
            case -1:
                IPC_SEND_LOG_ERROR("Error forking: %s.\n", strerror(errno));
                break;
            case 0:
                path_src = xmalloc(size_src);
                path_dst = xmalloc(size_dst);

                snprintf2(path_src, size_src,
                          "%s/%s", cecup.src_base, task->path);
                snprintf2(path_dst, size_dst,
                          "%s/%s", cecup.dst_base, task->path);

                {
                    char cmd[MAX_PATH_LENGTH*2];
                    char *diff_command[] = {
                        term_cmd, "-e", diff_tool, path_dst, path_src, NULL,
                    };

                    execvp(diff_command[0], diff_command);
                    STRING_FROM_ARRAY(cmd, " ", diff_command,
                                      LENGTH(diff_command));
                    error("Error executing\n%s\n%s.\n", cmd, strerror(errno));
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
on_menu_ignore(GtkWidget *m, void *data) {
    Message *message = data;
    char *pattern;
    FILE *fp;

    pattern = (char *)g_object_get_data(G_OBJECT(m), "ignore_pattern");

    if (pattern && (fp = fopen(cecup.ignore_path, "a"))) {
        fprintf(fp, "\n%s", pattern);
        fclose(fp);
    } else if (pattern == NULL) {
        IPC_SEND_LOG_ERROR("Internal error: Ignore pattern not found in widget data.\n");
    } else {
        IPC_SEND_LOG_ERROR("Error opening %s: %s.\n",
                           cecup.ignore_path, strerror(errno));
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

static gboolean
on_search_timeout(void *data) {
    (void)data;
    refresh_ui_list(REFRESH_FILTER_CHANGED, NULL);
    gtk_entry_set_icon_from_icon_name(GTK_ENTRY(cecup.search_entry),
                                      GTK_ENTRY_ICON_SECONDARY, NULL);
    cecup.search_timeout_id = 0;
    return G_SOURCE_REMOVE;
}

static void
on_search_changed(GtkEditable *editable, void *data) {
    char *text;
    (void)data;

    text = (char *)gtk_entry_get_text(GTK_ENTRY(editable));

    if (cecup.search_query) {
        free(cecup.search_query);
    }

    cecup.search_query = xstrdup(text);

    if (cecup.search_timeout_id != 0) {
        g_source_remove(cecup.search_timeout_id);
    }

    gtk_entry_set_icon_from_icon_name(GTK_ENTRY(cecup.search_entry),
                                      GTK_ENTRY_ICON_SECONDARY,
                                      "view-refresh-symbolic");

    cecup.search_timeout_id = g_timeout_add(250, on_search_timeout, NULL);
    return;
}

static void
on_preview_setting_toggled(GtkToggleButton *b, void *data) {
    (void)b;
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
on_preview_clicked(GtkWidget *b, void *data) {
    ThreadData *thread_data;

    (void)data;
    (void)b;

    cecup_get_dirs();

    protect_interface_from_user(true);

    thread_data = xmalloc(SIZEOF(*thread_data));
    memset64(thread_data, 0, SIZEOF(*thread_data));

    thread_data->is_preview = true;
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

        protect_interface_from_user(true);

        thread_data = xmalloc(SIZEOF(*thread_data));
        memset64(thread_data, 0, SIZEOF(*thread_data));

        thread_data->is_preview = false;
        thread_data->check_different_fs
            = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.check_fs));
        thread_data->delete_after = gtk_toggle_button_get_active(
            GTK_TOGGLE_BUTTON(cecup.delete_after));
        thread_data->delete_excluded = gtk_toggle_button_get_active(
            GTK_TOGGLE_BUTTON(cecup.delete_excluded));
        g_thread_new("work_rsync", work_rsync, thread_data);
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

    if ((strlen32(src_path) <= 0) || (strlen32(dst_path) <= 0)) {
        return;
    }

    protect_interface_from_user(true);

    thread_data = xmalloc(SIZEOF(*thread_data));
    memset64(thread_data, 0, SIZEOF(*thread_data));

    g_thread_new("work_fix_fs_worker", work_fix_fs_worker, thread_data);
    return;
}

static void
on_stop_clicked(GtkWidget *b, void *data) {
    (void)b;
    (void)data;
    if (cecup.child_pid > 0) {
        kill(-cecup.child_pid, SIGTERM);
    }
    return;
}

static void
on_filter_toggled(GtkToggleButton *b, void *data) {
    (void)data;
    (void)b;
    refresh_ui_list(REFRESH_FILTER_CHANGED, NULL);
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
        refresh_ui_list(REFRESH_FILTER_CHANGED, NULL);
    }
    return;
}

static void
on_cell_toggled(GtkCellRendererToggle *renderer, char *path_string,
                void *user_data) {
    (void)renderer;
    (void)user_data;

    do {
        GtkTreePath *tree_path;
        GtkTreeIter iter;
        CecupRow *parent_row;
        char *parent_path;
        int64 parent_path_len;
        bool is_root;

        if ((tree_path = gtk_tree_path_new_from_string(path_string)) == NULL) {
            break;
        }
        if (!gtk_tree_model_get_iter(GTK_TREE_MODEL(cecup.store), &iter,
                                     tree_path)) {
            gtk_tree_path_free(tree_path);
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
            gtk_tree_path_free(tree_path);
            break;
        }

        parent_path_len = strlen32(parent_path);
        is_root = !strcmp(parent_path, "./");

        for (int32 i = 0; i < cecup.rows_len; i += 1) {
            CecupRow *row = cecup.rows[i];
            char *path;
            int32 path_len;

            if (row->src_path) {
                path = row->src_path;
            } else {
                path = row->dst_path;
            }
            path_len = row->path_len;

            if (parent_row->selected) {
                if (is_root) {
                    row->selected = true;
                } else if ((parent_path_len > 0)
                           && (parent_path[parent_path_len - 1] == '/')) {
                    if ((path_len >= parent_path_len)
                        && (strncmp32(path, parent_path, parent_path_len)
                            == 0)) {
                        row->selected = true;
                    }
                }
            } else {
                if (is_root) {
                    row->selected = false;
                } else {
                    if ((parent_path_len > 0)
                        && (parent_path[parent_path_len - 1] == '/')) {
                        if ((path_len >= parent_path_len)
                            && (strncmp32(path, parent_path, parent_path_len)
                                == 0)) {
                            row->selected = false;
                        }
                    }

                    if (strcmp(path, "./") == 0) {
                        row->selected = false;
                    } else if ((path_len < parent_path_len)
                               && (path[path_len - 1] == '/')) {
                        if (strncmp32(parent_path, path, path_len) == 0) {
                            row->selected = false;
                        }
                    }
                }
            }
        }
        gtk_tree_path_free(tree_path);
    } while (0);

    refresh_ui_list(REFRESH_FINAL, NULL);
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
        "_Save", GTK_RESPONSE_ACCEPT, "_Close", GTK_RESPONSE_CLOSE, NULL);
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
    }
    gtk_widget_destroy(dialog);
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
    return;
}

static void
on_browse_src(GtkWidget *b, void *data) {
    GtkWidget *dialog;

    (void)data;
    (void)b;
    dialog = gtk_file_chooser_dialog_new(
        _("Src"), GTK_WINDOW(cecup.gtk_window),
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "_Cancel",
        GTK_RESPONSE_CANCEL, "_Select", GTK_RESPONSE_ACCEPT, NULL);
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
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "_Cancel",
        GTK_RESPONSE_CANCEL, "_Select", GTK_RESPONSE_ACCEPT, NULL);
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
    bool is_busy;

    (void)data;
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(widget));
    is_busy = gtk_widget_get_sensitive(cecup.stop_button);

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        CecupRow *row;
        int32 side;
        char *filepath;
        int32 path_len;
        enum CecupAction action;
        uint32 modifiers;

        side = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "side"));
        gtk_tree_model_get(model, &iter, COL_ROW_PTR, &row, -1);

        filepath = (side == SIDE_LEFT) ? row->src_path : row->dst_path;
        path_len = row->path_len;
        action = (side == SIDE_LEFT) ? row->src_action : row->dst_action;
        modifiers = event->state
                    & (GDK_CONTROL_MASK | GDK_SHIFT_MASK | GDK_MOD1_MASK);

        for (int32 i = 0; i < (int32)LENGTH(context_menu_items); i += 1) {
            uint32 key = gdk_keyval_to_lower(event->keyval);
            uint32 target = gdk_keyval_to_lower(context_menu_items[i].keyval);

            if ((context_menu_items[i].keyval != 0) && (key == target)
                && (modifiers == context_menu_items[i].mask)) {

                if (is_busy && (context_menu_items[i].callback == on_menu_rename ||
                                context_menu_items[i].callback == on_menu_delete ||
                                context_menu_items[i].callback == on_menu_apply)) {
                    IPC_SEND_LOG_ERROR(_("Action blocked: Background task is running.\n"));
                    return TRUE;
                }

                if (filepath
                    || (context_menu_items[i].callback == on_menu_rename)) {
                    Message *message;

                    message = xmalloc(SIZEOF(*message));
                    memset64(message, 0, SIZEOF(*message));

                    if (filepath) {
                        message->path_len = path_len;
                        message->src_path
                            = xmalloc(path_len + 1);
                        memcpy64(message->src_path, filepath, path_len + 1);
                    }

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
    GtkTreePath *tree_path;
    bool is_busy;

    (void)data;
    side = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "side"));
    is_busy = gtk_widget_get_sensitive(cecup.stop_button);

    if (event->type != GDK_BUTTON_PRESS && event->type != GDK_2BUTTON_PRESS) {
        return FALSE;
    }

    switch (event->button) {
    case GDK_BUTTON_PRIMARY: {
        if (event->type == GDK_2BUTTON_PRESS) {
            if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget),
                                              (gint)event->x, (gint)event->y,
                                              &tree_path, NULL, NULL, NULL)) {
                char *filepath;
                enum CecupAction action;
                int32 row_index = gtk_tree_path_get_indices(tree_path)[0];
                CecupRow *row = cecup.rows_visible[row_index];

                if (side == SIDE_LEFT) {
                    filepath = row->src_path;
                    action = row->src_action;
                } else {
                    filepath = row->dst_path;
                    action = row->dst_action;
                }

                if (filepath) {
                    Message *message;

                    message = xmalloc(SIZEOF(*message));
                    memset64(message, 0, SIZEOF(*message));

                    message->path_len = row->path_len;
                    message->src_path
                        = xmalloc(row->path_len + 1);
                    memcpy64(message->src_path, filepath, row->path_len + 1);

                    message->action = action;
                    message->side = side;

                    on_menu_open_item(NULL, message);
                }

                gtk_tree_path_free(tree_path);
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
        int32 path_len;
        enum CecupAction action;

        if (!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget),
                                           (gint)event->x, (gint)event->y,
                                           &tree_path, NULL, NULL, NULL)) {
            break;
        }

        {
            int32 row_index = gtk_tree_path_get_indices(tree_path)[0];
            CecupRow *row = cecup.rows_visible[row_index];

            if (side == SIDE_LEFT) {
                filepath = row->src_path;
                other_path = row->dst_path;
                action = row->src_action;
            } else {
                filepath = row->dst_path;
                other_path = row->src_path;
                action = row->dst_action;
            }
            path_len = row->path_len;
        }

        message = xmalloc(SIZEOF(*message));
        memset64(message, 0, SIZEOF(*message));

        if (filepath) {
            message->path_len = path_len;
            message->src_path = xmalloc(path_len + 1);
            memcpy64(message->src_path, filepath, path_len + 1);
        }

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

            if (is_busy && (context_menu_items[i].callback == on_menu_apply ||
                            context_menu_items[i].callback == on_menu_rename ||
                            context_menu_items[i].callback == on_menu_delete)) {
                gtk_widget_set_sensitive(item, FALSE);
            }

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
                GtkWidget *sub_relative;
                GtkWidget *sub_name;
                char *name;
                int32 length;

                if (filepath) {
                    name = basename2(filepath, &path_len, &length);
                } else {
                    name = "";
                    length = 0;
                }

                gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), sub);
                gtk_widget_set_sensitive(item, FALSE);

                if (filepath) {
                    char *extension_ptr = NULL;
                    char extension_label[32];
                    char directory_label[MAX_PATH_LENGTH + 64];
                    char directory_buffer[MAX_PATH_LENGTH];
                    char relative_label[MAX_NAME_LENGTH];
                    char name_label[MAX_NAME_LENGTH + 2];
                    char pattern_buffer[MAX_PATH_LENGTH];

                    sub_ext = gtk_menu_item_new();
                    sub_dir = gtk_menu_item_new();
                    sub_relative = gtk_menu_item_new();
                    sub_name = gtk_menu_item_new();

                    gtk_widget_set_sensitive(item, TRUE);
                    gtk_widget_set_sensitive(sub_ext, FALSE);
                    gtk_widget_set_sensitive(sub_dir, FALSE);
                    gtk_widget_set_sensitive(sub_relative, TRUE);
                    gtk_widget_set_sensitive(sub_name, TRUE);

                    if ((extension_ptr = memchr64(name, '.', length))) {
                        extension_ptr = strrchr(extension_ptr, '.');
                        SNPRINTF(extension_label,
                                 _("by extension (*%s)"), extension_ptr);

                        SNPRINTF(pattern_buffer, "*%s", extension_ptr);

                        gtk_widget_set_sensitive(sub_ext, TRUE);
                        g_object_set_data_full(G_OBJECT(sub_ext),
                                               "ignore_pattern",
                                               g_strdup(pattern_buffer),
                                               g_free);
                    } else {
                        SNPRINTF(extension_label, "%s", _("by extension"));
                    }

                    dirname2(directory_buffer, filepath, &path_len);
                    if (strcmp(directory_buffer, ".")) {
                        SNPRINTF(directory_label, _("📁 Dir (/%s/)"),
                                 directory_buffer);

                        SNPRINTF(pattern_buffer, "/%s/", directory_buffer);

                        gtk_widget_set_sensitive(sub_dir, TRUE);
                        g_object_set_data_full(G_OBJECT(sub_dir),
                                               "ignore_pattern",
                                               g_strdup(pattern_buffer),
                                               g_free);
                    } else {
                        SNPRINTF(directory_label, "%s", _("📁 Dir"));
                    }

                    SNPRINTF(relative_label,
                             _("This file only (/%s)"), filepath);
                    SNPRINTF(name_label,
                             _("This filename on any folder (*/%s)"), name);

                    SNPRINTF(pattern_buffer, "/%s", filepath);
                    g_object_set_data_full(G_OBJECT(sub_relative),
                                           "ignore_pattern",
                                           g_strdup(pattern_buffer),
                                           g_free);

                    SNPRINTF(pattern_buffer, "*/%s", name);
                    g_object_set_data_full(G_OBJECT(sub_name),
                                           "ignore_pattern",
                                           g_strdup(pattern_buffer),
                                           g_free);

                    g_signal_connect(sub_dir, "activate",
                                     G_CALLBACK(on_menu_ignore), message);
                    g_signal_connect(sub_ext, "activate",
                                     G_CALLBACK(on_menu_ignore), message);
                    g_signal_connect(sub_relative, "activate",
                                     G_CALLBACK(on_menu_ignore), message);
                    g_signal_connect(sub_name, "activate",
                                     G_CALLBACK(on_menu_ignore), message);


                    gtk_menu_item_set_label(GTK_MENU_ITEM(sub_ext),
                                            extension_label);
                    gtk_menu_item_set_label(GTK_MENU_ITEM(sub_dir),
                                            directory_label);
                    gtk_menu_item_set_label(GTK_MENU_ITEM(sub_relative),
                                            relative_label);
                    gtk_menu_item_set_label(GTK_MENU_ITEM(sub_name),
                                            name_label);

                    gtk_menu_shell_append(GTK_MENU_SHELL(sub), sub_ext);
                    gtk_menu_shell_append(GTK_MENU_SHELL(sub), sub_dir);
                    gtk_menu_shell_append(GTK_MENU_SHELL(sub), sub_relative);
                    gtk_menu_shell_append(GTK_MENU_SHELL(sub), sub_name);

                    if (is_busy) {
                        gtk_widget_set_sensitive(item, FALSE);
                    }
                }
            } else {
                if (filepath == NULL) {
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
        gtk_tree_path_free(tree_path);
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
    GtkTreePath *tree_path;
    GtkTreeViewColumn *col;
    gint bin_x;
    gint bin_y;

    int32 index;
    int32 side;
    int32 view_column_index;
    int32 number_of_columns;
    char *tip_text = NULL;
    char tip_buffer[MAX_PATH_LENGTH*2];

    (void)k;
    (void)d;
    gtk_tree_view_convert_widget_to_bin_window_coords(GTK_TREE_VIEW(w), x, y,
                                                      &bin_x, &bin_y);

    if (!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(w), bin_x, bin_y,
                                       &tree_path, &col, NULL, NULL)) {
        return FALSE;
    }

    index = gtk_tree_path_get_indices(tree_path)[0];
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
            char *reason;

            reason = _(reason_strings[row->reason]);
            if (row->link_target) {
                SNPRINTF(tip_buffer, 
                         "%s -> %s: %s", filepath, row->link_target, reason);
            } else if (row->ignore_pattern) {
                SNPRINTF(tip_buffer,
                         "%s: %s (" N_("pattern") ": %s)",
                         filepath, reason, row->ignore_pattern);
            } else {
                SNPRINTF(tip_buffer, "%s: %s", filepath, reason);
            }
            tip_text = tip_buffer;
            break;
        }
        case 3: {
            int64 size_raw;
            if (side == SIDE_LEFT) {
                size_raw = row->src_size_raw;
            } else {
                size_raw = row->dst_size_raw;
            }
            SNPRINTF(tip_buffer, "%s: %lld bytes", filepath, (llong)size_raw);
            tip_text = tip_buffer;
            break;
        }
        case 4: {
            char *mtime_text;
            if (side == SIDE_LEFT) {
                mtime_text = row->src_mtime_text;
            } else {
                mtime_text = row->dst_mtime_text;
            }
            SNPRINTF(tip_buffer, "%s: %s", filepath, mtime_text);
            tip_text = tip_buffer;
            break;
        }
        default:
            break;
        }
    }

    if (tip_text) {
        gtk_tooltip_set_text(t, tip_text);
        gtk_tree_path_free(tree_path);
        return TRUE;
    }
    gtk_tree_path_free(tree_path);
    return FALSE;
}

static void
regenerate_preview_filtered(char *relative_old, char *relative_new,
                            int32 len_old, int32 len_new) {
    ThreadData *thread_data;

    thread_data = xmalloc(SIZEOF(*thread_data));
    memset64(thread_data, 0, SIZEOF(*thread_data));

    thread_data->relative_old = xmalloc(len_old + 1);
    thread_data->relative_new = xmalloc(len_new + 1);

    memcpy64(thread_data->relative_old, relative_old, len_old + 1);
    memcpy64(thread_data->relative_new, relative_new, len_new + 1);

    thread_data->is_preview = true;
    thread_data->check_different_fs
        = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.check_fs));
    thread_data->delete_excluded = gtk_toggle_button_get_active(
        GTK_TOGGLE_BUTTON(cecup.delete_excluded));
    thread_data->delete_after
        = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.delete_after));

    thread_data->filtered = true;
    thread_data->len_old = len_old;
    thread_data->len_new = len_new;

    {
        Message *message;

        message = xmalloc(SIZEOF(*message));
        memset64(message, 0, SIZEOF(*message));

        message->path_len = len_old;
        message->src_path = xmalloc(len_old + 1);
        memcpy64(message->src_path, relative_old, len_old + 1);

        message->type = DATA_TYPE_REMOVE_ROW;
        g_idle_add_full(G_PRIORITY_HIGH_IDLE, update_ui_handler, message, NULL);
        while (g_main_context_pending(NULL)) {
            g_main_context_iteration(NULL, false);
        }
        g_thread_new("work_rsync", work_rsync, thread_data);
    }
    return;
}

typedef struct SelectionData {
    GtkEditable *editable;
    int32 start_pos;
    int32 end_pos;
} SelectionData;

static gboolean
on_path_selection_idle(void *data) {
    SelectionData *selection_data = data;

    gtk_editable_select_region(selection_data->editable,
                               selection_data->start_pos,
                               selection_data->end_pos);
    free(selection_data);
    return G_SOURCE_REMOVE;
}

static void
on_path_editing_started(GtkCellRenderer *renderer, GtkCellEditable *editable,
                        char *path_str, void *data) {
    GtkWidget *tree = data;
    GtkTreePath *tree_path;
    int32 row_index;
    int32 side;

    (void)renderer;

    if (!GTK_IS_ENTRY(editable) || (tree == NULL)) {
        return;
    }

    side = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(tree), "side"));

    if ((tree_path = gtk_tree_path_new_from_string(path_str))) {
        row_index = gtk_tree_path_get_indices(tree_path)[0];
        gtk_tree_path_free(tree_path);
    } else {
        return;
    }

    if ((row_index >= 0) && (row_index < cecup.rows_visible_len)) {
        CecupRow *row = cecup.rows_visible[row_index];
        char *relative;
        if (side == SIDE_LEFT) {
            relative = row->src_path;
        } else {
            relative = row->dst_path;
        }

        if (relative) {
            GtkEntry *entry = GTK_ENTRY(editable);
            char *name;
            char *last_dot;
            int32 name_len;
            int32 start_pos;
            int32 end_pos = row->path_len;

            gtk_entry_set_text(entry, relative);

            name = basename2(relative, &row->path_len, &name_len);
            last_dot = strrchr(name, '.');

            start_pos = row->path_len - name_len;

            if (last_dot && (last_dot != name)) {
                end_pos = (int32)(last_dot - relative);
            }

            if (end_pos > start_pos) {
                SelectionData *selection_data;

                selection_data = xmalloc(SIZEOF(*selection_data));
                memset64(selection_data, 0, SIZEOF(*selection_data));

                selection_data->editable = GTK_EDITABLE(entry);
                selection_data->start_pos = start_pos;
                selection_data->end_pos = end_pos;

                g_idle_add(on_path_selection_idle, selection_data);
            }
        }
    }
    return;
}

static void
on_path_edited(GtkCellRendererText *renderer,
               char *path_str, char *new_text, void *data) {
    GtkWidget *tree = data;
    GtkTreePath *tree_path;
    GtkTreeIter iter;
    CecupRow *row;
    int32 side;
    char *base_path;
    char old_full[MAX_PATH_LENGTH];
    char *relative_old;
    char relative_new[MAX_PATH_LENGTH];
    int32 new_length;

    (void)renderer;
    side = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(tree), "side"));
    tree_path = gtk_tree_path_new_from_string(path_str);

    if (!gtk_tree_model_get_iter(GTK_TREE_MODEL(cecup.store), &iter,
                                  tree_path)) {
        return;
    }

    gtk_tree_model_get(GTK_TREE_MODEL(cecup.store), &iter, COL_ROW_PTR, &row,
                       -1);

    if (side == SIDE_LEFT) {
        base_path = cecup.src_base;
        relative_old = row->src_path;
    } else {
        base_path = cecup.dst_base;
        relative_old = row->dst_path;
    }
    if (relative_old == NULL) {
        goto out;
    }

    SNPRINTF(old_full, "%s/%s", base_path, relative_old);

    if ((new_length = strlen32(new_text)) > 0) {
        char new_full[MAX_PATH_LENGTH];
        int32 old_length = strlen32(relative_old);
        int32 new_full_length;

        memcpy64(relative_new, new_text, new_length + 1);
        normalize(relative_new, &new_length);
        new_full_length = SNPRINTF(new_full, "%s/%s", base_path, relative_new);
        normalize(new_full, &new_full_length);

        if (rename(old_full, new_full) == 0) {
            IPC_SEND_LOG(_("Renamed: %s -> %s\n"), relative_old, relative_new);

            if ((relative_old[old_length - 1] == '/')
                && (relative_new[new_length - 1] != '/')) {
                relative_new[new_length] = '/';
                relative_new[new_length+1] = '\0';
                new_length += 1;
            }
            regenerate_preview_filtered(relative_old, relative_new,
                                        old_length, new_length);
        } else {
            IPC_SEND_LOG_ERROR(_("Error renaming %s to %s: %s\n"), old_full,
                               new_full, strerror(errno));
        }
    }

out:
    gtk_tree_path_free(tree_path);
    return;
}

#if TESTING_on
int
main(void) {
    ASSERT(true);
    exit(EXIT_SUCCESS);
}
#endif

#endif
