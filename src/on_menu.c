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
#include "on.h"
#include "aux.c"

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_on_menu 1
#elif !defined(TESTING_on_menu)
#define TESTING_on_menu 0
#endif
#if !defined(TESTING)
#define TESTING 0
#endif

static void
on_menu_dispatch(GSimpleAction *action, GVariant *parameter, void *data) {
    GtkWidget *tree;
    Message *message;
    int32 index = -1;
    CecupMenuItem *menu_item;

    (void)action;
    (void)data;

    if ((tree = g_object_get_data(G_OBJECT(cecup.application), "active_tree")) == NULL) {
        error("Error in %s: Can't get \"active_tree\" from the application widget.\n", __func__);
    }
    if ((message = g_object_steal_data(G_OBJECT(cecup.application), "active_message")) == NULL) {
        error("Error in %s: Can't get \"active_message\" from the application widget.\n", __func__);
    }
    if (parameter == NULL) {
        error("Error in %s: GVariant *parameter is NULL.\n", __func__);
    }
    if (parameter) {
        index = g_variant_get_int32(parameter);
        if ((index < 0) || (index >= LENGTH(tree_menu_items))) {
            error("Error in %s: index out of range for the menu items array.\n", __func__);
            index = -1;
        }
    }

    if ((tree == NULL) || (message == NULL) || (parameter == NULL) || (index < 0)) {
        free_message(message);
        return;
    }

    menu_item = &tree_menu_items[index];
    if (menu_item->callback) {
        if (menu_item->variant) {
            g_object_set_data_full(G_OBJECT(tree), "variant", menu_item->variant, NULL);
        }
        menu_item->callback(tree, message);
    } else {
        free_message(message);
    }

    return;
}

static void
on_menu_ignore_action(GSimpleAction *action, GVariant *parameter, void *data) {
    char *pattern;
    FILE *ignore_file;

    (void)action;
    (void)data;

    if (parameter == NULL) {
        error("Error in %s: GVariant *parameter is NULL.\n", __func__);
        fatal(EXIT_FAILURE);
    }
    if ((pattern = (char *)g_variant_get_string(parameter, NULL)) == NULL) {
        error("Ignore pattern is NULL.\n");
        fatal(EXIT_FAILURE);
    }

    if ((ignore_file = fopen(cecup.ignore_path, "a")) == NULL) {
        LOG_ERROR(_("Error opening %s: %s.\n"), cecup.ignore_path, strerror(errno));
        return;
    }

    fprintf(ignore_file, "\n%s", pattern);
    if (fclose(ignore_file)) {
        LOG_ERROR(_("Error closing %s: %s.\n"), cecup.ignore_path, strerror(errno));
    }

    {
        Message *message = xmalloc(SIZEOF(*message));
        memset64(message, 0, SIZEOF(*message));

        message->type = MSG_IGNORE_PATTERN;
        g_idle_add(update_ui_handler, message);
    }
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

        aux_protect_interface_from_user(true);
        thread_data = xmalloc(SIZEOF(*thread_data));
        memset64(thread_data, 0, SIZEOF(*thread_data));
        thread_data->tasks = tasks;

        xpthread_create(&cecup.work_thread, NULL, work_rsync, thread_data);
    } else {
        task_list_free(tasks);
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
        void *col_ptr = g_object_get_data(G_OBJECT(current), "cecup-col");
        void *pos_ptr = g_object_get_data(G_OBJECT(current), "cecup-pos");
        GtkWidget *next_child;

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
    bool folder;

    variant = get_variant(widget, "variant");
    tasks = get_target_tasks(message->side, message->src_path, message->action);

    if (!strcmp(variant, "folder")) {
        folder = true;
    } else if (!strcmp(variant, "file")) {
        folder = false;
    } else {
        error("Error in %s:"
              "\"variant\" must be \"folder\" or \"file\", but \"%s\" was passed.\n",
              __func__, variant);
        fatal(EXIT_FAILURE);
    }

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

        if (folder) {
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
            STRING_FROM_ARRAY(cmd, " ", command, LENGTH(command) - 1);
            LOG(_("Launching %s...\n"), cmd);
            util_command_launch(LENGTH(command), command);
        }
    }

    task_list_free(tasks);
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
    char *buf_pointer;
    int32 space;
    char *base_path;
    GdkClipboard *clipboard;
    bool absolute;

    buffer_size = SIZEMB(8);
    buffer = xmalloc(buffer_size);
    buf_pointer = buffer;

    clipboard = gdk_display_get_clipboard(gdk_display_get_default());
    space = buffer_size - 1;
    variant = get_variant(widget, __func__);

    if (!strcmp(variant, "absolute")) {
        absolute = true;
    } else if (!strcmp(variant, "relative")) {
        absolute = false;
    } else {
        error("Error in %s:"
              "\"variant\" must be \"absolute\" or \"relative\", but \"%s\" was passed.\n",
              __func__, variant);
        fatal(EXIT_FAILURE);
    }

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

        if (absolute) {
            char path_relative[MAX_PATH_LENGTH];

            SNPRINTF(path_relative, "%s/%s", base_path, task->path);

            if (realpath(path_relative, path_full) == NULL) {
                LOG_ERROR(_("Error resolving full path of %s:"
                            "%s. Copying relative path instead.\n"),
                          path_relative, strerror(errno));
                SNPRINTF(path_full, "%s", path_relative);
                continue;
            }
            path = path_full;
            path_len = strlen32(path_full);
        } else {
            path = task->path;
            path_len = task->path_len;
        }

        if (space > (path_len + 1)) {
            memcpy64(buf_pointer, path, path_len);
            buf_pointer += path_len;
            space -= path_len;

            *buf_pointer = '\n';
            buf_pointer += 1;
            space -= 1;
        } else {
            LOG_ERROR("Error adding paths to clipboard. Too many paths.\n");
            break;
        }
    }
    *buf_pointer = '\0';
    gdk_clipboard_set_text(clipboard, buffer);

    free(buffer, buffer_size);
    task_list_free(tasks);
    free_message(message);
    return;
}

static void
on_delete_response(GtkDialog *dialog, int32 response_id, void *data) {
    TaskList *tasks = data;

    if (response_id == GTK_RESPONSE_YES) {
        ThreadData *thread_data;

        aux_protect_interface_from_user(true);
        thread_data = xmalloc(SIZEOF(*thread_data));
        memset64(thread_data, 0, SIZEOF(*thread_data));
        thread_data->tasks = tasks;

        xpthread_create(&cecup.work_thread, NULL, work_rsync, thread_data);
    } else {
        task_list_free(tasks);
    }
    gtk_window_destroy(GTK_WINDOW(dialog));

    return;
}

static void
on_menu_delete(GtkWidget *widget, void *data) {
    Message *message = data;
    TaskList *tasks;

    (void)widget;

    tasks = get_target_tasks(message->side, message->src_path, ACTION_DELETE);

    if (tasks->count > 0) {
        GtkWidget *dialog;

        for (int32 i = 0; i < tasks->count; i += 1) {
            tasks->items[i]->action = ACTION_DELETE;
        }

        dialog = gtk_message_dialog_new(GTK_WINDOW(cecup.gtk_window),
                                        GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_YES_NO,
                                        _("Permanently delete %d item(s)?"), tasks->count);

        g_signal_connect(dialog, "response", G_CALLBACK(on_delete_response), tasks);
        gtk_widget_show(dialog);
    } else {
        task_list_free(tasks);
    }

    free_message(message);
    return;
}

static void
on_menu_diff(GtkWidget *widget, void *data) {
    Message *message = data;
    TaskList *tasks;
    char *term_cmd_raw;
    char *diff_tool_raw;
    char *diff_tool;
    char *term_cmd;
    int32 term_cmd_len;
    int32 diff_tool_len;
    char *term_arguments[64];
    char *diff_arguments[64];
    char *token;
    int32 term_argument_count = 0;
    int32 diff_argument_count = 0;

    (void)widget;

    term_cmd_raw = (char *)gtk_editable_get_text(GTK_EDITABLE(cecup.term_entry));
    diff_tool_raw = (char *)gtk_editable_get_text(GTK_EDITABLE(cecup.diff_entry));

    term_cmd_len = strlen32(term_cmd_raw);
    diff_tool_len = strlen32(diff_tool_raw);

    term_cmd = xmemdup(term_cmd_raw, term_cmd_len + 1);
    diff_tool = xmemdup(diff_tool_raw, diff_tool_len + 1);

    token = strtok(term_cmd, " ");
    while (token != NULL && term_argument_count < LENGTH(term_arguments)) {
        term_arguments[term_argument_count++] = token;
        token = strtok(NULL, " ");
    }
    term_arguments[term_argument_count] = NULL;

    token = strtok(diff_tool, " ");
    while (token != NULL && diff_argument_count < LENGTH(diff_arguments)) {
        diff_arguments[diff_argument_count++] = token;
        token = strtok(NULL, " ");
    }
    diff_arguments[diff_argument_count] = NULL;

    tasks = get_target_tasks(message->side, message->src_path, message->action);

    for (int32 i = 0; i < tasks->count; i += 1) {
        Task *task = tasks->items[i];
        int32 size_src = strlen32(cecup.src_base) + strlen32(task->path) + 2;
        int32 size_dst = strlen32(cecup.dst_base) + strlen32(task->path) + 2;
        char *path_src = xmalloc(size_src);
        char *path_dst = xmalloc(size_dst);

        switch (fork()) {
        case -1:
            error("Error forking: %s.\n", strerror(errno));
            fatal(EXIT_FAILURE);
        case 0:
            if (setsid() < 0) {
                error("Error in setsid: %s.\n", strerror(errno));
            }

            snprintf2(path_src, size_src, "%s/%s", cecup.src_base, task->path);
            snprintf2(path_dst, size_dst, "%s/%s", cecup.dst_base, task->path);

            {
                char cmd[MAX_PATH_LENGTH*2];
                char *combined_arguments[128];
                int32 k = 0;

                for (int32 j = 0; j < term_argument_count; j += 1) {
                    combined_arguments[k++] = term_arguments[j];
                }

                combined_arguments[k++] = "-e";

                for (int32 j = 0; j < diff_argument_count; j += 1) {
                    combined_arguments[k++] = diff_arguments[j];
                }

                combined_arguments[k++] = path_dst;
                combined_arguments[k++] = path_src;
                combined_arguments[k++] = NULL;

                execvp(combined_arguments[0], combined_arguments);
                STRING_FROM_ARRAY(cmd, " ", combined_arguments, k);
                error("Error executing\n%s\n%s.\n", cmd, strerror(errno));
                _exit(EXIT_FAILURE);
            }
        default:
            free(path_src, size_src);
            free(path_dst, size_dst);
            break;
        }
    }

    task_list_free(tasks);
    free_message(message);
    free(term_cmd, term_cmd_len);
    free(diff_tool, diff_tool_len);
    return;
}

#if (0 == TESTING_on_menu) && TESTING
static inline void
on_menu_functions_sink(void) {
    (void)on_menu_functions_sink;
    (void)on_menu_ignore_action;
    (void)on_menu_dispatch;
    return;
}
#endif

#if TESTING_on_menu
#include "work.c"
#include "on.c"
#include "assert.c"
#include "list_model.c"

int
main(void) {
    GVariant *param;
    FILE *file;
    char buffer[256];
    int64 read_bytes;
    Message *msg;
    GtkWidget *tree;
    GtkSelectionModel *sel;
    CecupListModel *store;

    if (!gtk_init_check()) {
        exit(EXIT_SUCCESS);
    }

    cecup.application = gtk_application_new("com.cecup.test.on_menu", G_APPLICATION_NON_UNIQUE);
    cecup.gtk_window = gtk_window_new();

    cecup.stop_button = gtk_button_new();
    cecup.diff_entry = gtk_entry_new();
    cecup.term_entry = gtk_entry_new();

    gtk_editable_set_text(GTK_EDITABLE(cecup.diff_entry), "diff");
    gtk_editable_set_text(GTK_EDITABLE(cecup.term_entry), "xterm");

    store = cecup_list_model_new();
    sel = GTK_SELECTION_MODEL(gtk_single_selection_new(G_LIST_MODEL(store)));
    tree = gtk_column_view_new(sel);
    g_object_ref_sink(tree);

    g_object_set_data(G_OBJECT(cecup.application), "active_tree", tree);

    SNPRINTF(cecup.ignore_path, "%s", "test_ignore_temp.txt");
    file = fopen(cecup.ignore_path, "w");
    ASSERT(file != NULL);
    fclose(file);

    param = g_variant_new_string("*.test_ext");
    g_variant_ref_sink(param);

    on_menu_ignore_action(NULL, param, NULL);

    file = fopen(cecup.ignore_path, "r");
    ASSERT(file != NULL);

    memset64(buffer, 0, SIZEOF(buffer));
    read_bytes = fread(buffer, 1, SIZEOF(buffer) - 1, file);
    fclose(file);

    ASSERT_MORE(read_bytes, 0);
    ASSERT_EQUAL(buffer, "\n*.test_ext");

    g_variant_unref(param);
    remove(cecup.ignore_path);

    cecup.rows_visible_len = 0;
    cecup.rows_selected = xmalloc(10 * SIZEOF(uint8));
    cecup.rows_selected[0] = false;

    msg = xmalloc(SIZEOF(*msg));
    memset64(msg, 0, SIZEOF(*msg));
    msg->side = L;
    msg->action = ACTION_NEW;
    on_menu_apply(tree, msg);

    msg = xmalloc(SIZEOF(*msg));
    memset64(msg, 0, SIZEOF(*msg));
    on_menu_rename(tree, msg);

    msg = xmalloc(SIZEOF(*msg));
    memset64(msg, 0, SIZEOF(*msg));
    g_object_set_data(G_OBJECT(tree), "variant", "file");
    on_menu_open_item(tree, msg);

    msg = xmalloc(SIZEOF(*msg));
    memset64(msg, 0, SIZEOF(*msg));
    g_object_set_data(G_OBJECT(tree), "variant", "absolute");
    cecup.src_base = xmalloc(10);
    memcpy64(cecup.src_base, "/tmp", 5);
    on_menu_copy_path(tree, msg);

    {
        TaskList *tasks;

        tasks = xmalloc(SIZEOF(TaskList));
        tasks->count = 0;
        on_delete_response(NULL, GTK_RESPONSE_NO, tasks);
    }

    msg = xmalloc(SIZEOF(*msg));
    memset64(msg, 0, SIZEOF(*msg));
    on_menu_delete(tree, msg);

    msg = xmalloc(SIZEOF(*msg));
    memset64(msg, 0, SIZEOF(*msg));
    on_menu_diff(tree, msg);

    {
        GVariant *idx_param;

        msg = xmalloc(SIZEOF(*msg));
        memset64(msg, 0, SIZEOF(*msg));
        g_object_set_data_full(G_OBJECT(cecup.application), "active_message", msg, free_message);

        idx_param = g_variant_new_int32(0);
        g_variant_ref_sink(idx_param);
        on_menu_dispatch(NULL, idx_param, NULL);
        g_variant_unref(idx_param);
    }

    free(cecup.src_base, 10);
    free(cecup.rows_selected, 10 * SIZEOF(uint8));

    g_object_unref(tree);
    g_object_unref(sel);
    g_object_unref(store);
    g_object_unref(cecup.application);
    gtk_window_destroy(GTK_WINDOW(cecup.gtk_window));

    ASSERT(true);
    exit(EXIT_SUCCESS);
}

#endif

#endif /* ON_MENU_C */
