// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(ON_MENU_C)
#define ON_MENU_C

#include "gtk_include.h"

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
        error("Error: Can't get \"active_tree\" from the application widget.\n");
    }
    if ((message = g_object_steal_data(G_OBJECT(cecup.application), "active_message")) == NULL) {
        error("Error: Can't get \"active_message\" from the application widget.\n");
    }
    if (parameter == NULL) {
        error("Error: GVariant *parameter is NULL.\n");
    }
    if (parameter) {
        index = g_variant_get_int32(parameter);
        if ((index < 0) || (index >= LENGTH(tree_menu_items))) {
            error("Error: index out of range for the menu items array.\n");
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

static bool
on_menu_append_ignore_pattern(char *pattern, int32 pattern_len) {
    FILE *ignore_file;
    int64 ignore_file_len;
    int32 last_byte = '\n';
    bool appended = false;

    if (pattern_len >= MAX_PATH_LENGTH) {
        LOG_ERROR(_("Error appending pattern %.*s ... Pattern is too long.\n"),
                  50, pattern);
        return false;
    }

    if ((ignore_file = fopen(cecup.ignore_path, "a+")) == NULL) {
        LOG_ERROR(_("Error opening %s: %s.\n"),
                  cecup.ignore_path, strerror(errno));
        return false;
    }

    if (fseek(ignore_file, 0, SEEK_END) != 0) {
        LOG_ERROR(_("Error seeking %s: %s.\n"),
                  cecup.ignore_path, strerror(errno));
        goto close_file;
    }
    if ((ignore_file_len = ftell(ignore_file)) < 0) {
        LOG_ERROR(_("Error getting the size of %s: %s.\n"),
                  cecup.ignore_path, strerror(errno));
        goto close_file;
    }

    if (ignore_file_len > 0) {
        if (fseek(ignore_file, -1, SEEK_END) != 0) {
            LOG_ERROR(_("Error seeking %s: %s.\n"),
                      cecup.ignore_path, strerror(errno));
            goto close_file;
        }
        if ((last_byte = fgetc(ignore_file)) == EOF) {
            LOG_ERROR(_("Error reading %s: %s.\n"),
                      cecup.ignore_path, strerror(errno));
            goto close_file;
        }
        if (fseek(ignore_file, 0, SEEK_END) != 0) {
            LOG_ERROR(_("Error seeking %s: %s.\n"),
                      cecup.ignore_path, strerror(errno));
            goto close_file;
        }
    }

    if ((last_byte != '\n') && (fputc('\n', ignore_file) == EOF)) {
        LOG_ERROR(_("Error appending a newline to %s: %s.\n"),
                  cecup.ignore_path, strerror(errno));
        goto close_file;
    }
    if (fprintf(ignore_file, "%s\n", pattern) != (pattern_len + 1)) {
        LOG_ERROR(_("Error appending ignore pattern \"%s\" to %s: %s.\n"),
                  pattern, cecup.ignore_path, strerror(errno));
        goto close_file;
    }

    appended = true;

close_file:
    if (fclose(ignore_file)) {
        LOG_ERROR(_("Error closing %s: %s.\n"),
                  cecup.ignore_path, strerror(errno));
        appended = false;
    }
    return appended;
}

static void
on_menu_ignore_action(GSimpleAction *action, GVariant *parameter, void *data) {
    char *pattern;
    int32 pattern_len;

    (void)action;
    (void)data;

    if (parameter == NULL) {
        error("Error: GVariant *parameter is NULL.\n");
        fatal(EXIT_FAILURE);
    }

    if ((pattern = (char *)g_variant_get_string(parameter, NULL)) == NULL) {
        error("Ignore pattern is NULL.\n");
        fatal(EXIT_FAILURE);
    }
    pattern_len = strlen32(pattern);
    if (!on_menu_append_ignore_pattern(pattern, pattern_len)) {
        return;
    }

    {
        Message *message = malloc2(SIZEOF(*message));
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
        ThreadData *thread_data = malloc2(SIZEOF(*thread_data));
        memset64(thread_data, 0, SIZEOF(*thread_data));

        aux_protect_interface_from_user(true);
        thread_data->tasks = tasks;
        thread_data->check_different_fs
            = gtk_check_button_get_active(GTK_CHECK_BUTTON(cecup.check_fs_button));

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

    if (strequal(variant, "folder")) {
        folder = true;
    } else if (strequal(variant, "file")) {
        folder = false;
    } else {
        error("Error: "
              "\"variant\" must be \"folder\" or \"file\", but \"%s\" was passed.\n",
              variant);
        fatal(EXIT_FAILURE);
    }

    for (int32 i = 0; i < tasks->count; i += 1) {
        Task *task = tasks->items[i];
        char full_path[MAX_PATH_LENGTH];
        char *base_path;
        int32 n;
        int fd;

        if (message->side == L) {
            base_path = cecup.base[L];
        } else {
            base_path = cecup.base[R];
        }
        n = SNPRINTF(full_path, "%s/%s", base_path, task->path);

        if (folder) {
            int32 path_len = n;
            dirname2(full_path, full_path, &path_len);
        }

        if ((fd = open(full_path, O_RDONLY)) < 0) {
            LOG_ERROR(_("Error opening %s: %s.\n"), full_path, strerror(errno));
            continue;
        }
        XCLOSE(&fd);

        {
            Command command = {0};

            command_push(&command, "xdg-open");
            command_push(&command, full_path);
            LOG(_("Launching...\n"));
            command_print(&command);
            (void)command_run(&command,
                              COMMAND_DETACHED
                              |COMMAND_NEW_SESSION);
            command_free(&command);
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
    buffer = malloc2(buffer_size);
    buf_pointer = buffer;

    clipboard = gdk_display_get_clipboard(gdk_display_get_default());
    space = buffer_size - 1;
    variant = get_variant(widget, __func__);

    if (strequal(variant, "absolute")) {
        absolute = true;
    } else if (strequal(variant, "relative")) {
        absolute = false;
    } else {
        error("Error: "
              "\"variant\" must be \"absolute\" or \"relative\", but \"%s\" was passed.\n",
              variant);
        fatal(EXIT_FAILURE);
    }

    if (message->side == L) {
        base_path = cecup.base[L];
    } else {
        base_path = cecup.base[R];
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
                LOG_ERROR(_("Error resolving full path of %s:%s. Copying relative path instead.\n"),           path_relative, strerror(errno));
                SNPRINTF(path_full, "%s", path_relative);
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

    free2(buffer, buffer_size);
    task_list_free(tasks);
    free_message(message);
    return;
}

static void
on_delete_response(GtkDialog *dialog, int32 response_id, void *data) {
    TaskList *tasks = data;

    if (response_id == GTK_RESPONSE_YES) {
        ThreadData *thread_data = malloc2(SIZEOF(*thread_data));
        memset64(thread_data, 0, SIZEOF(*thread_data));

        aux_protect_interface_from_user(true);
        thread_data->tasks = tasks;
        thread_data->check_different_fs
            = gtk_check_button_get_active(GTK_CHECK_BUTTON(cecup.check_fs_button));

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

static Command
on_menu_diff_command(char *term_command, char *diff_tool) {
    Command command = {0};

    command_push_split(&command, term_command, " ");
    COMMAND_PUSH(&command, "-e");
    command_push_split(&command, diff_tool, " ");

    return command;
}

static void
on_menu_diff(GtkWidget *widget, void *data) {
    Message *message = data;
    TaskList *tasks;
    char *term_command;
    char *diff_tool;

    (void)widget;

    term_command = (char *)gtk_editable_get_text(
        GTK_EDITABLE(cecup.term_entry));
    diff_tool = (char *)gtk_editable_get_text(GTK_EDITABLE(cecup.diff_entry));
    tasks = get_target_tasks(message->side, message->src_path, message->action);

    for (int32 i = 0; i < tasks->count; i += 1) {
        Task *task = tasks->items[i];
        int32 size_src = strlen32(cecup.base[L]) + strlen32(task->path) + 2;
        int32 size_dst = strlen32(cecup.base[R]) + strlen32(task->path) + 2;
        char *path_src = malloc2(size_src);
        char *path_dst = malloc2(size_dst);
        Command command;
        int32 path_src_len = snprintf2(path_src, size_src,
                                       "%s/%s", cecup.base[L], task->path);
        int32 path_dst_len = snprintf2(path_dst, size_dst,
                                       "%s/%s", cecup.base[R], task->path);

        command = on_menu_diff_command(term_command, diff_tool);
        COMMAND_PUSH(&command, path_dst, path_dst_len);
        COMMAND_PUSH(&command, path_src, path_src_len);

        (void)command_run_async(&command, COMMAND_NEW_SESSION);
        command_free(&command);
        free2(path_src, size_src);
        free2(path_dst, size_dst);
    }

    task_list_free(tasks);
    free_message(message);
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
#define CBASE_IMPLEMENT
#include "cbase.h"

#include "work.c"
#include "on.c"
#include "list_model.c"

typedef struct ClipboardResult {
    bool done;
    char *text;
} ClipboardResult;

static void
clipboard_read_callback(GObject *source, GAsyncResult *result, void *data) {
    ClipboardResult *clipboard_result;

    clipboard_result = data;
    clipboard_result->text = gdk_clipboard_read_text_finish(
        GDK_CLIPBOARD(source), result, NULL);
    clipboard_result->done = true;
    return;
}

int
main(void) {
    FILE *file;
    char buffer[256];
    int64 read_bytes;
    Message *msg;
    GtkWidget *tree;
    GtkSelectionModel *sel;
    CecupListModel *store;
    ClipboardResult clipboard_result;

    if (!gtk_init_check()) {
        exit(EXIT_SUCCESS);
    }

    cecup.application = gtk_application_new("com.cecup.test.on_menu", G_APPLICATION_NON_UNIQUE);
    cecup.gtk_window = gtk_window_new();

    cecup.stop_button = gtk_button_new();
    cecup.diff_entry = gtk_entry_new();
    cecup.term_entry = gtk_entry_new();

    gtk_editable_set_text(GTK_EDITABLE(cecup.diff_entry), "diff");
    gtk_editable_set_text(GTK_EDITABLE(cecup.term_entry), "true");

    {
        Command command = on_menu_diff_command("xterm --hold", "diff --color=always");

        COMMAND_PUSH(&command, "/destination");
        COMMAND_PUSH(&command, "/source");

        ASSERT_EQUAL(command.argc, 7);
        ASSERT_EQUAL(command.argv[0], "xterm");
        ASSERT_EQUAL(command.argv[1], "--hold");
        ASSERT_EQUAL(command.argv[2], "-e");
        ASSERT_EQUAL(command.argv[3], "diff");
        ASSERT_EQUAL(command.argv[4], "--color=always");
        ASSERT_EQUAL(command.argv[5], "/destination");
        ASSERT_EQUAL(command.argv[6], "/source");
        ASSERT_EQUAL(command.argv[command.argc], NULL);

        command_free(&command);
    }

    cecup.base[L] = xstrdup("/tmp");
    cecup.base_len[L] = strlen32(cecup.base[L]);
    cecup.base[R] = xstrdup("/tmp");
    cecup.base_len[R] = strlen32(cecup.base[R]);

    store = cecup_list_model_new();
    sel = GTK_SELECTION_MODEL(gtk_single_selection_new(G_LIST_MODEL(store)));
    tree = gtk_column_view_new(sel);
    g_object_ref_sink(tree);

    g_object_set_data(G_OBJECT(cecup.application), "active_tree", tree);

    SNPRINTF(cecup.ignore_path, "%s", "test_ignore_temp.txt");
    file = fopen(cecup.ignore_path, "w");
    ASSERT(file != NULL);
    fclose(file);

    ASSERT(on_menu_append_ignore_pattern(STRLIT("*.test_ext")));

    file = fopen(cecup.ignore_path, "r");
    ASSERT(file != NULL);

    memset64(buffer, 0, SIZEOF(buffer));
    read_bytes = fread64(buffer, 1, SIZEOF(buffer) - 1, file);
    fclose(file);

    ASSERT_EQUAL(read_bytes, STRLIT_LEN("*.test_ext\n"));
    ASSERT_EQUAL(buffer, "*.test_ext\n");

    remove(cecup.ignore_path);

    cecup.rows_visible_len = 0;
    cecup.rows_selected = malloc2(10 * SIZEOF(uint8));
    cecup.rows_selected[0] = false;

    msg = malloc2(SIZEOF(*msg));
    memset64(msg, 0, SIZEOF(*msg));
    msg->side = L;
    msg->action = ACTION_NEW;
    msg->src_path = xstrdup("test.txt");
    msg->src_path_len = 8;
    on_menu_apply(tree, msg);

    msg = malloc2(SIZEOF(*msg));
    memset64(msg, 0, SIZEOF(*msg));
    msg->src_path = xstrdup("test.txt");
    msg->src_path_len = 8;
    on_menu_rename(tree, msg);

    msg = malloc2(SIZEOF(*msg));
    memset64(msg, 0, SIZEOF(*msg));
    msg->side = L;
    msg->src_path = xstrdup("test.txt");
    msg->src_path_len = 8;
    g_object_set_data(G_OBJECT(tree), "variant", "file");
    on_menu_open_item(tree, msg);

    {
        char expected[MAX_PATH_LENGTH];
        char missing_full[MAX_PATH_LENGTH];
        char missing_path[MAX_PATH_LENGTH];

        SNPRINTF(missing_path, "cecup-on-menu-missing-%d", getpid());
        SNPRINTF(missing_full, "/tmp/%s", missing_path);
        SNPRINTF(expected, "%s\n", missing_full);
        remove(missing_full);

        msg = malloc2(SIZEOF(*msg));
        memset64(msg, 0, SIZEOF(*msg));
        msg->side = L;
        msg->src_path = xstrdup(missing_path);
        msg->src_path_len = strlen32(missing_path);
        g_object_set_data(G_OBJECT(tree), "variant", "absolute");
        on_menu_copy_path(tree, msg);

        clipboard_result.done = false;
        clipboard_result.text = NULL;
        gdk_clipboard_read_text_async(
            gdk_display_get_clipboard(gdk_display_get_default()),
            NULL, clipboard_read_callback, &clipboard_result);
        while (!clipboard_result.done) {
            g_main_context_iteration(NULL, true);
        }

        ASSERT(clipboard_result.text != NULL);
        ASSERT_EQUAL(clipboard_result.text, expected);
        g_free(clipboard_result.text);
    }

    {
        TaskList *tasks;
        GtkWidget *dialog;

        tasks = malloc2(SIZEOF(TaskList));
        tasks->count = 0;
        dialog = gtk_dialog_new();
        on_delete_response(GTK_DIALOG(dialog), GTK_RESPONSE_NO, tasks);
    }

    msg = malloc2(SIZEOF(*msg));
    memset64(msg, 0, SIZEOF(*msg));
    msg->side = L;
    msg->src_path = xstrdup("test.txt");
    msg->src_path_len = 8;
    on_menu_delete(tree, msg);

    msg = malloc2(SIZEOF(*msg));
    memset64(msg, 0, SIZEOF(*msg));
    msg->side = L;
    msg->src_path = xstrdup("test.txt");
    msg->src_path_len = 8;
    on_menu_diff(tree, msg);

    {
        GVariant *idx_param;

        msg = malloc2(SIZEOF(*msg));
        memset64(msg, 0, SIZEOF(*msg));
        msg->side = L;
        msg->action = ACTION_NEW;
        msg->src_path = xstrdup("test.txt");
        msg->src_path_len = 8;
        g_object_set_data_full(G_OBJECT(cecup.application), "active_message", msg, free_message);

        idx_param = g_variant_new_int32(0);
        g_variant_ref_sink(idx_param);
        on_menu_dispatch(NULL, idx_param, NULL);
        g_variant_unref(idx_param);
    }

    free2(cecup.base[L], cecup.base_len[L] + 1);
    free2(cecup.base[R], cecup.base_len[R] + 1);
    free2(cecup.rows_selected, 10 * SIZEOF(uint8));

    g_object_unref(tree);
    g_object_unref(cecup.application);
    gtk_window_destroy(GTK_WINDOW(cecup.gtk_window));

    ASSERT(true);
    exit(EXIT_SUCCESS);
}

#endif

#endif /* ON_MENU_C */
