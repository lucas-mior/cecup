#if !defined(ON_C)
#define ON_C

#include "cecup.h"
#include "work.c"

static void
on_menu_apply(GtkWidget *m, void *data) {
    Message *message = data;
    GPtrArray *tasks;

    (void)m;

    if ((tasks = get_target_tasks(message->side, message->filepath,
                                  message->action))) {
        cecup.cancel_sync = 0;
        gtk_widget_set_sensitive(cecup.preview_button, FALSE);
        gtk_widget_set_sensitive(cecup.sync_button, FALSE);
        gtk_widget_set_sensitive(cecup.stop_button, TRUE);
        g_thread_new("bulk_sync", work_bulk_sync_worker, tasks);
    }

    free_update_data(message);
    return;
}

static void
on_menu_open(GtkWidget *m, void *data) {
    Message *message = data;
    GPtrArray *tasks;

    (void)m;

    if ((tasks = get_target_tasks(message->side, message->filepath,
                                  message->action))) {
        for (int32 i = 0; i < (int32)tasks->len; i += 1) {
            Message *task;
            char full_path[MAX_PATH_LENGTH];
            char *base_path;
            pid_t child;

            task = (Message *)g_ptr_array_index(tasks, i);
            if (message->side == 0) {
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
    GPtrArray *tasks;

    (void)m;

    if ((tasks = get_target_tasks(message->side, message->filepath,
                                  message->action))) {
        for (int32 i = 0; i < (int32)tasks->len; i += 1) {
            Message *task;
            char full_path[MAX_PATH_LENGTH];
            char *dir_path;
            char *base_path;

            task = (Message *)g_ptr_array_index(tasks, i);
            if (message->side == 0) {
                base_path = cecup.src_base;
            } else {
                base_path = cecup.dst_base;
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

    free_update_data(message);
    return;
}

static void
on_menu_copy_path(GtkWidget *m, void *data) {
    Message *message = data;
    GPtrArray *tasks;
    char *buffer;
    int64 buffer_size = SIZEMB(2);
    char *write_pointer;
    int64 remaining_capacity;
    char *base_path;

    buffer = xmalloc(buffer_size);
    write_pointer = buffer;
    remaining_capacity = buffer_size - 1;

    if (message->side == 0) {
        base_path = cecup.src_base;
    } else {
        base_path = cecup.dst_base;
    }

    if ((tasks = get_target_tasks(message->side, message->filepath,
                                  message->action))) {
        for (int32 i = 0; i < (int32)tasks->len; i += 1) {
            Message *task;
            int64 path_length;
            char path_full[MAX_PATH_LENGTH];
            char *path;
            char *path_type = g_object_get_data(G_OBJECT(m), "path_type");

            task = (Message *)g_ptr_array_index(tasks, i);

            if (!strcmp(path_type, "absolute")) {
                char path_relative[MAX_PATH_LENGTH];

                task = (Message *)g_ptr_array_index(tasks, i);

                SNPRINTF(path_relative, "%s/%s", base_path, task->filepath);
                if (realpath(path_relative, path_full) == NULL) {
                    dispatch_log_error("Error resolving full path of %s: %s.\n",
                                       path_relative, strerror(errno));
                    continue;
                }
                path = path_full;
                path_length = strlen64(path_full);
            } else {
                path = task->filepath;
                path_length = task->filepath_length;
            }

            if ((i > 0) && (remaining_capacity > 0)) {
                *write_pointer = '\n';
                write_pointer += 1;
                remaining_capacity -= 1;
            }

            if (remaining_capacity >= path_length) {
                memcpy64(write_pointer, path, path_length);
                write_pointer += path_length;
                remaining_capacity -= path_length;
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
    GPtrArray *tasks;
    GtkWidget *dialog;
    int32 count;

    (void)m;

    if ((tasks = get_target_tasks(message->side, message->filepath,
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
            g_thread_new("bulk_sync", work_bulk_sync_worker, tasks);
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

    if ((tasks = get_target_tasks(message->side, message->filepath,
                                  message->action))) {
        for (int32 i = 0; i < (int32)tasks->len; i += 1) {
            Message *task;

            task = (Message *)g_ptr_array_index(tasks, i);
            g_mutex_lock(&cecup.ui_arena_mutex);
            task->diff_tool_len = diff_len;
            task->diff_tool
                = xarena_push(cecup.ui_arena, ALIGN16(diff_len + 1));
            memcpy64(task->diff_tool, diff_tool, diff_len + 1);

            task->term_cmd_len = term_len;
            task->term_cmd = xarena_push(cecup.ui_arena, ALIGN16(term_len + 1));
            memcpy64(task->term_cmd, term_cmd, term_len + 1);
            g_mutex_unlock(&cecup.ui_arena_mutex);

            g_thread_new("diff_worker", work_diff_worker, task);
        }
        g_ptr_array_unref(tasks);
    }

    free_update_data(message);
    return;
}

static void
on_menu_ignore_ext(GtkWidget *m, void *data) {
    Message *message = data;
    GPtrArray *tasks;
    FILE *fp;

    (void)m;

    do {
        if ((tasks = get_target_tasks(message->side, message->filepath,
                                      message->action))
            == NULL) {
            break;
        }
        if ((fp = fopen(cecup.ignore_path, "a")) == NULL) {
            dispatch_log_error("Error opening %s: %s.\n", cecup.ignore_path,
                               strerror(errno));
            break;
        }
        for (int32 i = 0; i < (int32)tasks->len; i += 1) {
            Message *task;
            char *ext;

            task = (Message *)g_ptr_array_index(tasks, i);
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
    GPtrArray *tasks;
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
        for (int32 i = 0; i < (int32)tasks->len; i += 1) {
            Message *task;
            char *dir;

            task = (Message *)g_ptr_array_index(tasks, i);
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
    free_update_data(message);
    return;
}

static void
on_config_changed(GtkWidget *widget, void *data) {
    (void)widget;
    (void)data;
    cecup.src_base = (char *)gtk_entry_get_text(GTK_ENTRY(cecup.src_entry));
    cecup.dst_base = (char *)gtk_entry_get_text(GTK_ENTRY(cecup.dst_entry));
    cecup.src_base_len = strlen64(cecup.src_base);
    cecup.dst_base_len = strlen64(cecup.dst_base);
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

    strncpy(thread_data->src_path, src_path, MAX_PATH_LENGTH - 1);
    strncpy(thread_data->dst_path, dst_path, MAX_PATH_LENGTH - 1);
    g_thread_new("fix_fs_worker", work_fix_fs_worker, thread_data);
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

    if (event->type != GDK_BUTTON_PRESS && event->type != GDK_2BUTTON_PRESS) {
        return FALSE;
    }

    switch (event->button) {
    case GDK_BUTTON_PRIMARY: {
        if (event->type == GDK_2BUTTON_PRESS) {
            if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget),
                                              (gint)event->x, (gint)event->y,
                                              &path, NULL, NULL, NULL)) {
                Message *message;
                CecupRow *row;
                int32 row_index;
                char *file_path;
                int64 path_length;
                enum CecupAction action;

                row_index = gtk_tree_path_get_indices(path)[0];
                row = cecup.visible_rows[row_index];

                if (side == 0) {
                    file_path = row->src_path;
                    path_length = row->src_path_len;
                    action = row->src_action;
                } else {
                    file_path = row->dst_path;
                    path_length = row->dst_path_len;
                    action = row->dst_action;
                }

                if (file_path) {
                    g_mutex_lock(&cecup.ui_arena_mutex);
                    message = xarena_push(cecup.ui_arena,
                                          ALIGN16(SIZEOF(*message)));
                    memset64(message, 0, SIZEOF(*message));

                    message->filepath_length = path_length;
                    message->filepath
                        = xarena_push(cecup.ui_arena, ALIGN16(path_length + 1));
                    memcpy64(message->filepath, file_path, path_length + 1);
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
        GtkWidget *item;
        char *file_path;
        char *other_path;
        int64 path_length;
        enum CecupAction action;

        if (!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget),
                                           (gint)event->x, (gint)event->y,
                                           &path, NULL, NULL, NULL)) {
            break;
        }

        {
            int32 row_index;
            CecupRow *row;

            row_index = gtk_tree_path_get_indices(path)[0];
            row = cecup.visible_rows[row_index];

            if (side == 0) {
                file_path = row->src_path;
                path_length = row->src_path_len;
                other_path = row->dst_path;
                action = row->src_action;
            } else {
                file_path = row->dst_path;
                path_length = row->dst_path_len;
                other_path = row->src_path;
                action = row->dst_action;
            }
        }

        g_mutex_lock(&cecup.ui_arena_mutex);
        message = xarena_push(cecup.ui_arena, ALIGN16(SIZEOF(*message)));
        memset64(message, 0, SIZEOF(*message));

        message->filepath_length = path_length;
        message->filepath
            = xarena_push(cecup.ui_arena, ALIGN16(path_length + 1));
        memcpy64(message->filepath, file_path, path_length + 1);
        g_mutex_unlock(&cecup.ui_arena_mutex);

        message->action = action;
        message->side = side;

        menu = gtk_menu_new();
        item = gtk_menu_item_new_with_label(_("📄 Open File"));
        g_signal_connect(item, "activate", G_CALLBACK(on_menu_open), message);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

        item = gtk_menu_item_new_with_label(_("📂 Open Folder"));
        g_signal_connect(item, "activate", G_CALLBACK(on_menu_open_dir),
                         message);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

        item = gtk_menu_item_new_with_label(_("📋 Copy Relative Path"));
        if (file_path == NULL) {
            gtk_widget_set_sensitive(item, FALSE);
        } else {
            g_object_set_data(G_OBJECT(item), "path_type", "relative");
            g_signal_connect(item, "activate", G_CALLBACK(on_menu_copy_path),
                             message);
        }
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

        item = gtk_menu_item_new_with_label(_("📍 Copy Full Path"));
        if (file_path == NULL) {
            gtk_widget_set_sensitive(item, FALSE);
        } else {
            g_object_set_data(G_OBJECT(item), "path_type", "absolute");
            g_signal_connect(item, "activate", G_CALLBACK(on_menu_copy_path),
                             message);
        }
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

        item = gtk_menu_item_new_with_label(_("⏯️ Apply"));
        g_signal_connect(item, "activate", G_CALLBACK(on_menu_apply), message);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

        item = gtk_menu_item_new_with_label(_("💤 Ignore..."));
        {
            GtkWidget *sub = gtk_menu_new();
            GtkWidget *sub_ext;
            GtkWidget *sub_dir;
            char extension_label[32];
            char directory_label[MAX_PATH_LENGTH + 64];
            char *extension_ptr;
            char *directory_ptr;
            char *name = basename(message->filepath);
            int64 length = strlen64(name);

            gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), sub);

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

            if ((directory_ptr = g_path_get_dirname(message->filepath))) {
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
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

        item = gtk_menu_item_new_with_label(_("🔍 Diff"));
        if ((file_path == NULL) || (other_path == NULL)
            || (action == UI_ACTION_HARDLINK)
            || (action == UI_ACTION_SYMLINK)) {
            gtk_widget_set_sensitive(item, FALSE);
        } else {
            g_signal_connect(item, "activate", G_CALLBACK(on_menu_diff),
                             message);
        }
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

        item = gtk_menu_item_new_with_label(_("🗑️ Delete"));
        if (file_path == NULL) {
            gtk_widget_set_sensitive(item, FALSE);
        } else {
            g_signal_connect(item, "activate", G_CALLBACK(on_menu_delete),
                             message);
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

    int32 index;
    int32 side;
    int32 view_column_index;
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

    if ((index >= 0) && (index < cecup.visible_count)) {
        CecupRow *row = cecup.visible_rows[index];
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

        switch (view_column_index) {
        case 1: {
            char **strings;
            char *translated_action;
            int64 string_length;

            if (side == 0) {
                strings = src_action_strings;
            } else {
                strings = dst_action_strings;
            }

            translated_action = _(strings[action]);
            string_length = strlen64(translated_action);

            g_mutex_lock(&cecup.ui_arena_mutex);
            tip_text = xarena_push(cecup.ui_arena, ALIGN16(string_length + 1));
            g_mutex_unlock(&cecup.ui_arena_mutex);
            memcpy64(tip_text, translated_action, string_length + 1);
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

#endif /* ON_C */
