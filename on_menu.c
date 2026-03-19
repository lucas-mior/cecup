#if !defined(ON_MENU_C)
#define ON_MENU_C

#include <gtk/gtk.h>
#include "cecup.h"
#include "util.c"

static void free_message(void *data);

static void
on_menu_dispatch(GSimpleAction *action, GVariant *parameter, void *data) {
    int32 index;
    CecupMenuItem *menu_item;
    GtkWidget *tree;
    Message *message;

    (void)action;
    (void)data;

    index = g_variant_get_int32(parameter);
    menu_item = &tree_menu_items[index];

    tree = g_object_get_data(G_OBJECT(cecup.application), "active_tree");
    message = g_object_steal_data(G_OBJECT(cecup.application), "active_message");

    if (tree && message && menu_item->callback) {
        if (menu_item->variant) {
            g_object_set_data(G_OBJECT(tree), "variant", menu_item->variant);
        }
        menu_item->callback(tree, message);
    } else if (message) {
        free_message(message);
    }

    return;
}

static void
on_menu_ignore_action(GSimpleAction *action, GVariant *parameter, void *data) {
    char *pattern;
    FILE *fp;

    (void)action;
    (void)data;

    pattern = (char *)g_variant_get_string(parameter, NULL);

    if (pattern && (fp = fopen(cecup.ignore_path, "a"))) {
        fprintf(fp, "\n%s", pattern);
        fclose(fp);
    } else if (pattern == NULL) {
        error("Ignore pattern is NULL.\n");
    } else {
        IPC_SEND_LOG_ERROR("Error opening %s: %s.\n",
                           cecup.ignore_path, strerror(errno));
    }

    return;
}

static void
on_menu_apply(GtkWidget *m, void *data) {
    Message *message;
    TaskList *tasks;

    (void)m;
    message = data;

    if ((tasks = get_target_tasks(message->side, message->src_path,
                                  message->action))) {
        protect_interface_from_user(true);
        g_thread_new("bulk_sync", work_rsync_bulk, tasks);
    }

    XFREE(message->src_path);
    XFREE(message);

    return;
}

static void
on_menu_rename(GtkWidget *m, void *data) {
    Message *message;
    GtkWidget *tree;
    GtkTreeSelection *selection;
    GtkTreeModel *model;
    GtkTreeIter iter;

    (void)m;
    message = data;

    if (message->side == LEFT) {
        tree = cecup.tree[LEFT];
    } else {
        tree = cecup.tree[RIGHT];
    }

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        GtkTreePath *tree_path;
        GtkTreeViewColumn *col;

        tree_path = gtk_tree_model_get_path(model, &iter);
        col = gtk_tree_view_get_column(GTK_TREE_VIEW(tree), 2);
        gtk_tree_view_set_cursor(GTK_TREE_VIEW(tree), tree_path, col, TRUE);
        gtk_tree_path_free(tree_path);
    }

    XFREE(message->src_path);
    XFREE(message);

    return;
}

static void
on_menu_open_item(GtkWidget *m, void *data) {
    Message *message;
    TaskList *tasks;
    char *variant;

    message = data;

    if (m) {
        variant = g_object_get_data(G_OBJECT(m), "variant");
    } else {
        variant = NULL;
    }

    if ((tasks = get_target_tasks(message->side, message->src_path,
                                  message->action))) {
        for (int32 i = 0; i < tasks->count; i += 1) {
            Task *task;
            char full_path[MAX_PATH_LENGTH];
            char *base_path;
            int32 n;

            task = tasks->items[i];

            if (message->side == LEFT) {
                base_path = cecup.src_base;
            } else {
                base_path = cecup.dst_base;
            }

            n = SNPRINTF(full_path, "%s/%s", base_path, task->path);

            if (variant && (strcmp(variant, "folder") == 0)) {
                int32 path_len;
                path_len = n;
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

    XFREE(message->src_path);
    XFREE(message);

    return;
}

static void
on_menu_copy_path(GtkWidget *m, void *data) {
    Message *message;
    TaskList *tasks;
    char *buffer;
    int64 buffer_size;
    char *write_pointer;
    int64 remaining_capacity;
    char *base_path;
    GdkClipboard *clipboard;

    message = data;
    buffer_size = SIZEMB(2);
    clipboard = gdk_display_get_clipboard(gdk_display_get_default());
    buffer = xmalloc(buffer_size);
    write_pointer = buffer;
    remaining_capacity = buffer_size - 1;

    if (message->side == LEFT) {
        base_path = cecup.src_base;
    } else {
        base_path = cecup.dst_base;
    }

    if ((tasks = get_target_tasks(message->side, message->src_path,
                                  message->action))) {
        for (int32 i = 0; i < tasks->count; i += 1) {
            Task *task;
            int32 path_len;
            char path_full[MAX_PATH_LENGTH];
            char *path;
            char *variant;

            task = tasks->items[i];
            variant = g_object_get_data(G_OBJECT(m), "variant");

            if (variant && (strcmp(variant, "absolute") == 0)) {
                char path_relative[MAX_PATH_LENGTH];

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
        gdk_clipboard_set_text(clipboard, buffer);
        free_task_list(tasks);
    }

    XFREE(buffer);
    XFREE(message->src_path);
    XFREE(message);

    return;
}

static void
on_delete_response(GtkDialog *dialog, int32 response_id, void *data) {
    TaskList *tasks;
    tasks = data;

    if (response_id == GTK_RESPONSE_YES) {
        protect_interface_from_user(true);
        g_thread_new("work_bulk_sync", work_rsync_bulk, tasks);
    } else {
        free_task_list(tasks);
    }
    gtk_window_destroy(GTK_WINDOW(dialog));

    return;
}

static void
on_menu_delete(GtkWidget *m, void *data) {
    Message *message;
    TaskList *tasks;
    GtkWidget *dialog;
    int32 count;

    (void)m;
    message = data;

    if ((tasks = get_target_tasks(message->side,
                                  message->src_path, ACTION_DELETE))) {
        for (int32 i = 0; i < tasks->count; i += 1) {
            tasks->items[i]->action = ACTION_DELETE;
        }

        count = tasks->count;
        dialog = gtk_message_dialog_new(
            GTK_WINDOW(cecup.gtk_window), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING,
            GTK_BUTTONS_YES_NO, _("Permanently delete %d item(s)?"), count);

        g_signal_connect(dialog, "response",
                         G_CALLBACK(on_delete_response), tasks);
        gtk_widget_show(dialog);
    }

    XFREE(message->src_path);
    XFREE(message);

    return;
}

static void
on_menu_diff(GtkWidget *m, void *data) {
    Message *message;
    TaskList *tasks;
    char *diff_tool;
    char *term_cmd;

    (void)m;
    message = data;
    diff_tool = (char *)gtk_editable_get_text(GTK_EDITABLE(cecup.diff_entry));
    term_cmd = (char *)gtk_editable_get_text(GTK_EDITABLE(cecup.term_entry));

    if ((tasks = get_target_tasks(message->side, message->src_path,
                                  message->action))) {
        for (int32 i = 0; i < tasks->count; i += 1) {
            Task *task;
            char *path_src;
            char *path_dst;
            int64 size_dst;
            int64 size_src;
            int32 pid;

            task = tasks->items[i];
            size_src = strlen32(cecup.src_base) + strlen32(task->path) + 2;
            size_dst = strlen32(cecup.dst_base) + strlen32(task->path) + 2;

            pid = fork();
            if (pid == -1) {
                IPC_SEND_LOG_ERROR("Error forking: %s.\n", strerror(errno));
            } else if (pid == 0) {
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
            }
        }

        free_task_list(tasks);
    }

    XFREE(message->src_path);
    XFREE(message);

    return;
}


static void
on_menu_ignore(GtkWidget *m, void *data) {
    Message *message;
    char *pattern;
    FILE *fp;

    message = data;
    pattern = (char *)g_object_get_data(G_OBJECT(m), "ignore_pattern");

    if (pattern && (fp = fopen(cecup.ignore_path, "a"))) {
        fprintf(fp, "\n%s", pattern);
        fclose(fp);
    } else if (pattern == NULL) {
        error("Ignore pattern not found in widget data.\n");
        fatal(EXIT_FAILURE);
    } else {
        IPC_SEND_LOG_ERROR("Error opening %s: %s.\n",
                           cecup.ignore_path, strerror(errno));
    }

    XFREE(message->src_path);
    XFREE(message);

    return;
}

#endif /* ON_MENU_C */
