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

#if !defined(ON_MENU_C)
#define ON_MENU_C

#include <gtk/gtk.h>

#include "cecup.h"
#include "util.c"
#include "aux.c"
#include "on.h"

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_on_menu 1
#elif !defined(TESTING_on_menu)
#define TESTING_on_menu 0
#endif

static void
on_menu_dispatch(GSimpleAction *action, GVariant *parameter, void *data) {
    int32 index = g_variant_get_int32(parameter);
    CecupMenuItem *menu_item = &tree_menu_items[index];
    GtkWidget *tree;
    Message *message;

    (void)action;
    (void)data;

    tree = g_object_get_data(G_OBJECT(cecup.application), "active_tree");
    message = g_object_steal_data(G_OBJECT(cecup.application), "active_message");

    if (tree && message) {
        if (menu_item->callback) {
            if (menu_item->variant) {
                g_object_set_data_full(G_OBJECT(tree), "variant", menu_item->variant, NULL);
            }
            menu_item->callback(tree, message);
        } else {
            free_message(message);
        }
    } else if (message) {
        free_message(message);
    }

    return;
}

static void
on_menu_ignore_action(GSimpleAction *action, GVariant *parameter, void *data) {
    char *pattern = (char *)g_variant_get_string(parameter, NULL);
    FILE *fp;
    Message *message;

    (void)action;
    (void)data;

    if (pattern == NULL) {
        error("Ignore pattern is NULL.\n");
        fatal(EXIT_FAILURE);
    }

    if ((fp = fopen(cecup.ignore_path, "a")) == NULL) {
        LOG_ERROR(_("Error opening %s: %s.\n"), cecup.ignore_path, strerror(errno));
        return;
    }

    fprintf(fp, "\n%s", pattern);
    if (fclose(fp)) {
        LOG_ERROR(_("Error closing %s: %s.\n"), cecup.ignore_path, strerror(errno));
    }

    message = xmalloc(SIZEOF(*message));
    memset64(message, 0, SIZEOF(*message));

    message->type = MSG_IGNORE_PATTERN;
    message->ignore_pattern_len = strlen32(pattern);
    message->ignore_pattern = xmalloc(message->ignore_pattern_len + 1);
    memcpy64(message->ignore_pattern, pattern, message->ignore_pattern_len + 1);

    g_idle_add(update_ui_handler, message);
    return;
}

static void
on_menu_apply(GtkWidget *widget, void *data) {
    Message *message = data;
    TaskList *tasks;

    (void)widget;

    tasks = get_target_tasks(message->side, message->src_path, message->action);

    if (tasks->count > 0) {
        ThreadData *thread_data;

        protect_interface_from_user(true);
        thread_data = xmalloc(SIZEOF(*thread_data));
        memset64(thread_data, 0, SIZEOF(*thread_data));
        thread_data->tasks = tasks;

        g_thread_new("bulk_sync", work_rsync, thread_data);
    } else {
        free_task_list(tasks);
    }

    free_message(message);
    return;
}

static void
on_menu_rename(GtkWidget *tree, void *data) {
    Message *message = data;
    GtkSelectionModel *selection;
    uint32 pos;
    GtkWidget *current;

    selection = gtk_column_view_get_model(GTK_COLUMN_VIEW(tree));
    pos = gtk_single_selection_get_selected(GTK_SINGLE_SELECTION(selection));

    if (pos == GTK_INVALID_LIST_POSITION) {
        free_message(message);
        return;
    }

    current = tree;

    while (current != NULL) {
        void *col_ptr;
        void *pos_ptr;
        GtkWidget *next_child;

        col_ptr = g_object_get_data(G_OBJECT(current), "cecup-col");
        pos_ptr = g_object_get_data(G_OBJECT(current), "cecup-pos");

        if (col_ptr && (GPOINTER_TO_INT(col_ptr) == COLUMN_PATH)) {
            if (pos_ptr && ((GPOINTER_TO_UINT(pos_ptr) - 1) == pos)) {
                gtk_editable_label_start_editing(GTK_EDITABLE_LABEL(current));
                break;
            }
        }

        if ((next_child = gtk_widget_get_first_child(current))) {
            current = next_child;
            continue;
        }

        while (current
               && (current != tree)
               && (gtk_widget_get_next_sibling(current) == NULL)) {
            current = gtk_widget_get_parent(current);
        }

        if (current == tree) {
            break;
        }

        if (current != NULL) {
            current = gtk_widget_get_next_sibling(current);
        }
    }

    free_message(message);
    return;
}

static void
on_menu_open_item(GtkWidget *widget, void *data) {
    Message *message = data;
    TaskList *tasks;
    char *variant;

    if (widget) {
        variant = g_object_get_data(G_OBJECT(widget), "variant");
    } else {
        variant = NULL;
    }
    tasks = get_target_tasks(message->side, message->src_path, message->action);

    for (int32 i = 0; i < tasks->count; i += 1) {
        Task *task = tasks->items[i];
        char full_path[MAX_PATH_LENGTH];
        char *base_path;
        int32 n;

        if (message->side == L) {
            base_path = cecup.src_base;
        } else {
            base_path = cecup.dst_base;
        }
        n = SNPRINTF(full_path, "%s/%s", base_path, task->path);

        if (variant) {
            if (strcmp(variant, "folder") == 0) {
                int32 path_len = n;
                dirname2(full_path, full_path, &path_len);
            }
        }

        {
            char cmd[MAX_PATH_LENGTH];
            char *command[] = {
                "xdg-open",
                full_path,
                NULL,
            };
            STRING_FROM_ARRAY(cmd, " ", command, LENGTH(command));
            LOG(_("Launching %s...\n"), cmd);
            util_command_launch(LENGTH(command), command);
        }
    }

    free_task_list(tasks);
    free_message(message);
    return;
}

static void
on_menu_copy_path(GtkWidget *widget, void *data) {
    Message *message = data;
    TaskList *tasks;
    char *variant;
    char *buffer;
    int32 buffer_size;
    char *write_pointer;
    int32 remaining_capacity;
    char *base_path;
    GdkClipboard *clipboard;

    buffer_size = SIZEMB(2);
    buffer = xmalloc(buffer_size);
    write_pointer = buffer;

    clipboard = gdk_display_get_clipboard(gdk_display_get_default());
    remaining_capacity = buffer_size - 1;
    variant = g_object_get_data(G_OBJECT(widget), "variant");

    if (message->side == L) {
        base_path = cecup.src_base;
    } else {
        base_path = cecup.dst_base;
    }
    tasks = get_target_tasks(message->side, message->src_path, message->action);

    for (int32 i = 0; i < tasks->count; i += 1) {
        Task *task = tasks->items[i];
        int32 path_len;
        char path_full[PATH_MAX];
        char *path;

        if (variant && !strcmp(variant, "absolute")) {
            char path_relative[MAX_PATH_LENGTH];

            SNPRINTF(path_relative, "%s/%s", base_path, task->path);
            if (realpath(path_relative, path_full) == NULL) {
                LOG_ERROR(_("Error resolving full path of %s: %s.\n"),
                          path_relative, strerror(errno));
                continue;
            }
            path = path_full;
            path_len = strlen32(path_full);
        } else {
            path = task->path;
            path_len = task->path_len;
        }

        // TODO: If `remaining_capacity` is less than `path_len`, this item is silently dropped.
        // Consider dynamically reallocating the buffer to fit all selected paths to prevent data
        // loss.
        if (i > 0) {
            if (remaining_capacity > 0) {
                *write_pointer = '\n';
                write_pointer += 1;
                remaining_capacity -= 1;
            }
        }

        if (remaining_capacity >= path_len) {
            memcpy64(write_pointer, path, path_len);
            write_pointer += path_len;
            remaining_capacity -= path_len;
        }
    }
    *write_pointer = '\0';
    gdk_clipboard_set_text(clipboard, buffer);

    free(buffer, buffer_size);
    free_task_list(tasks);
    free_message(message);
    return;
}

static void
on_delete_response(GtkDialog *dialog, int32 response_id, void *data) {
    TaskList *tasks = data;

    if (response_id == GTK_RESPONSE_YES) {
        ThreadData *thread_data;

        protect_interface_from_user(true);
        thread_data = xmalloc(SIZEOF(*thread_data));
        memset64(thread_data, 0, SIZEOF(*thread_data));
        thread_data->tasks = tasks;

        g_thread_new("work_bulk_sync", work_rsync, thread_data);
    } else {
        free_task_list(tasks);
    }
    gtk_window_destroy(GTK_WINDOW(dialog));

    return;
}

static void
on_menu_delete(GtkWidget *widget, void *data) {
    Message *message = data;
    TaskList *tasks;
    GtkWidget *dialog;
    int32 count;

    (void)widget;

    tasks = get_target_tasks(message->side, message->src_path, ACTION_DELETE);

    if (tasks->count > 0) {
        for (int32 i = 0; i < tasks->count; i += 1) {
            tasks->items[i]->action = ACTION_DELETE;
        }

        count = tasks->count;
        dialog = gtk_message_dialog_new(
            GTK_WINDOW(cecup.gtk_window), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING,
            GTK_BUTTONS_YES_NO, _("Permanently delete %d item(s)?"), count);

        g_signal_connect(dialog, "response", G_CALLBACK(on_delete_response), tasks);
        gtk_widget_show(dialog);
    }

    free_message(message);
    return;
}

static void
on_menu_diff(GtkWidget *widget, void *data) {
    Message *message = data;
    TaskList *tasks;
    char *diff_tool;
    char *term_cmd;

    (void)widget;
    diff_tool = (char *)gtk_editable_get_text(GTK_EDITABLE(cecup.diff_entry));
    term_cmd = (char *)gtk_editable_get_text(GTK_EDITABLE(cecup.term_entry));

    tasks = get_target_tasks(message->side, message->src_path, message->action);

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
            error("Error forking: %s.\n", strerror(errno));
            fatal(EXIT_FAILURE);
        case 0:
            path_src = xmalloc(size_src);
            path_dst = xmalloc(size_dst);

            snprintf2(path_src, size_src, "%s/%s", cecup.src_base, task->path);
            snprintf2(path_dst, size_dst, "%s/%s", cecup.dst_base, task->path);

            {
                char cmd[MAX_PATH_LENGTH*2];
                char *diff_command[] = {
                    term_cmd, "-e", diff_tool, path_dst, path_src, NULL,
                };

                execvp(diff_command[0], diff_command);
                STRING_FROM_ARRAY(cmd, " ", diff_command, LENGTH(diff_command));
                error("Error executing\n%s\n%s.\n", cmd, strerror(errno));
                _exit(EXIT_FAILURE);
            }
        default:
            // TODO: Zombie Process Creation. The parent iterates through the loop and `fork()`s
            // children but never calls `wait()` or `waitpid()`. Because this loops `tasks->count`
            // times, it will leave a trail of zombie processes until the application exits unless
            // you have a global SIGCHLD handler managing reaps elsewhere.
            break;
        }
    }

    free_task_list(tasks);
    free_message(message);
    return;
}

/* #if 0 == TESTING_on_menu */
static inline void
on_menu_functions_sink(void) {
    (void)on_menu_functions_sink;
    (void)on_menu_ignore_action;
    (void)on_menu_dispatch;
}
/* #endif */

#if TESTING_on_menu
#include "work.c"
#include "on.c"

int
main(void) {
    exit(EXIT_SUCCESS);
}

#endif

#endif /* ON_MENU_C */
