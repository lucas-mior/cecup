#include <gtk/gtk.h>
#include <stdlib.h>
#include "cecup.h"
#include "rsync.c"

static void
free_cecup_row(gpointer data) {
    CecupRow *row;

    row = (CecupRow *)data;
    g_free(row->src_path);
    g_free(row->dst_path);
    free(row->size_text);
    g_free(row);
    return;
}

static void
free_task_list(GPtrArray *tasks) {
    char *shared_src;
    char *shared_dst;

    shared_src = NULL;
    shared_dst = NULL;

    for (int32 i = 0; i < (int32)tasks->len; i += 1) {
        UIUpdateData *t;

        t = (UIUpdateData *)g_ptr_array_index(tasks, i);
        if (shared_src == NULL) {
            shared_src = t->src_base;
        }
        if (shared_dst == NULL) {
            shared_dst = t->dst_base;
        }

        g_free(t->filepath);
        g_free(t->term_cmd);
        g_free(t->diff_tool);
        g_free(t);
    }

    g_free(shared_src);
    g_free(shared_dst);
    g_ptr_array_unref(tasks);
    return;
}

static GPtrArray *
get_target_tasks(int32 side, char *clicked_path,
                 enum CecupAction clicked_action) {
    GPtrArray *tasks;
    char *shared_src;
    char *shared_dst;

    tasks = g_ptr_array_new();
    shared_src = g_strdup(gtk_entry_get_text(GTK_ENTRY(cecup_state.src_entry)));
    shared_dst = g_strdup(gtk_entry_get_text(GTK_ENTRY(cecup_state.dst_entry)));

    for (int32 i = 0; i < (int32)cecup_state.rows->len; i += 1) {
        CecupRow *row;

        row = (CecupRow *)g_ptr_array_index(cecup_state.rows, i);
        if (row->selected) {
            char *f_path;
            enum CecupAction action;
            UIUpdateData *task;

            f_path = (side == 0) ? row->src_path : row->dst_path;
            action = (side == 0) ? row->src_action : row->dst_action;

            if (g_strcmp0(f_path, "-") != 0) {
                task = g_new0(UIUpdateData, 1);
                task->filepath = g_strdup(f_path);
                task->action = action;
                task->side = side;
                task->src_base = shared_src;
                task->dst_base = shared_dst;
                g_ptr_array_add(tasks, task);
            }
        }
    }

    if (tasks->len == 0 && g_strcmp0(clicked_path, "-") != 0) {
        UIUpdateData *task;

        task = g_new0(UIUpdateData, 1);
        task->filepath = g_strdup(clicked_path);
        task->action = clicked_action;
        task->side = side;
        task->src_base = shared_src;
        task->dst_base = shared_dst;
        g_ptr_array_add(tasks, task);
    }

    if (tasks->len == 0) {
        g_free(shared_src);
        g_free(shared_dst);
        g_ptr_array_unref(tasks);
        return NULL;
    }

    return tasks;
}

static gint
cecup_row_compare(gconstpointer a, gconstpointer b, gpointer user_data) {
    CecupRow *ra;
    CecupRow *rb;
    int32 result;

    (void)user_data;

    ra = *(CecupRow **)a;
    rb = *(CecupRow **)b;
    result = 0;

    switch (cecup_state.sort_col) {
    case COL_SRC_PATH:
    case COL_DST_PATH:
        result = g_strcmp0(ra->src_path, rb->src_path);
        break;
    case COL_SIZE_RAW:
        result = (ra->size_raw > rb->size_raw)
                     ? 1
                     : (ra->size_raw < rb->size_raw ? -1 : 0);
        break;
    default:
        result = (ra->src_action > rb->src_action)
                     ? 1
                     : (ra->src_action < rb->src_action ? -1 : 0);
        break;
    }

    if (cecup_state.sort_order == GTK_SORT_DESCENDING) {
        result *= -1;
    }

    return result;
}

static void
refresh_ui_list(void) {
    gboolean show_new;
    gboolean show_hard;
    gboolean show_update;
    gboolean show_equal;
    gboolean show_delete;
    gboolean show_ignore;

    show_new = gtk_toggle_button_get_active(
        GTK_TOGGLE_BUTTON(cecup_state.filter_new));
    show_hard = gtk_toggle_button_get_active(
        GTK_TOGGLE_BUTTON(cecup_state.filter_hard));
    show_update = gtk_toggle_button_get_active(
        GTK_TOGGLE_BUTTON(cecup_state.filter_update));
    show_equal = gtk_toggle_button_get_active(
        GTK_TOGGLE_BUTTON(cecup_state.filter_equal));
    show_delete = gtk_toggle_button_get_active(
        GTK_TOGGLE_BUTTON(cecup_state.filter_delete));
    show_ignore = gtk_toggle_button_get_active(
        GTK_TOGGLE_BUTTON(cecup_state.filter_ignore));

    gtk_list_store_clear(cecup_state.store);
    g_ptr_array_sort_with_data(cecup_state.rows, cecup_row_compare, NULL);

    for (int32 i = 0; i < (int32)cecup_state.rows->len; i += 1) {
        CecupRow *row;
        gboolean visible;

        row = (CecupRow *)g_ptr_array_index(cecup_state.rows, i);
        visible = FALSE;

        if (row->src_action == UI_ACTION_NEW) {
            visible = show_new;
        } else if (row->src_action == UI_ACTION_HARDLINK) {
            visible = show_hard;
        } else if (row->src_action == UI_ACTION_UPDATE) {
            visible = show_update;
        } else if (row->src_action == UI_ACTION_EQUAL) {
            visible = show_equal;
        } else if (row->src_action == UI_ACTION_DELETED) {
            visible = show_delete;
        } else if (row->src_action == UI_ACTION_IGNORE) {
            visible = show_ignore;
        }

        if (visible) {
            GtkTreeIter iter;
            gtk_list_store_append(cecup_state.store, &iter);
            gtk_list_store_set(
                cecup_state.store, &iter, COL_SELECTED, row->selected,
                COL_SRC_ACTION, action_strings[row->src_action], COL_DST_ACTION,
                action_strings[row->dst_action], COL_SRC_PATH, row->src_path,
                COL_DST_PATH, row->dst_path, COL_SIZE_TEXT, row->size_text,
                COL_SIZE_RAW, row->size_raw, COL_SRC_COLOR, row->src_color,
                COL_DST_COLOR, row->dst_color, COL_REASON,
                reason_strings[row->reason], -1);
        }
    }
    return;
}

static gboolean
refresh_ui_timeout_callback(gpointer data) {
    (void)data;
    refresh_ui_list();
    cecup_state.refresh_id = 0;
    return G_SOURCE_REMOVE;
}

static void
on_menu_apply(GtkWidget *m, gpointer data) {
    UIUpdateData *ud;
    GPtrArray *tasks;

    (void)m;
    ud = (UIUpdateData *)data;
    tasks = get_target_tasks(ud->side, ud->filepath, ud->action);

    if (tasks != NULL) {
        cecup_state.cancel_sync = 0;
        gtk_widget_set_sensitive(cecup_state.preview_button, FALSE);
        gtk_widget_set_sensitive(cecup_state.sync_button, FALSE);
        gtk_widget_set_sensitive(cecup_state.stop_button, TRUE);
        g_thread_new("bulk_sync", bulk_sync_worker, tasks);
    }

    g_free(ud->filepath);
    g_free(ud);
    return;
}

static void
on_menu_open(GtkWidget *m, gpointer data) {
    UIUpdateData *ud;
    GPtrArray *tasks;
    char cmd[4096];

    (void)m;
    ud = (UIUpdateData *)data;
    tasks = get_target_tasks(ud->side, ud->filepath, ud->action);

    if (tasks != NULL) {
        for (int32 i = 0; i < (int32)tasks->len; i += 1) {
            UIUpdateData *t;
            char *full;

            t = (UIUpdateData *)g_ptr_array_index(tasks, i);
            full = g_build_filename(ud->side == 0 ? t->src_base : t->dst_base,
                                    t->filepath, NULL);
            SNPRINTF(cmd, "xdg-open '%s' &", full);
            system(cmd);
            g_free(full);
        }
        free_task_list(tasks);
    }

    g_free(ud->filepath);
    g_free(ud);
    return;
}

static void
on_menu_open_dir(GtkWidget *m, gpointer data) {
    UIUpdateData *ud;
    GPtrArray *tasks;
    char cmd[4096];

    (void)m;
    ud = (UIUpdateData *)data;
    tasks = get_target_tasks(ud->side, ud->filepath, ud->action);

    if (tasks != NULL) {
        for (int32 i = 0; i < (int32)tasks->len; i += 1) {
            UIUpdateData *t;
            char *full;
            char *dir;

            t = (UIUpdateData *)g_ptr_array_index(tasks, i);
            full = g_build_filename(ud->side == 0 ? t->src_base : t->dst_base,
                                    t->filepath, NULL);
            if ((dir = g_path_get_dirname(full)) != NULL) {
                SNPRINTF(cmd, "xdg-open '%s' &", dir);
                system(cmd);
                g_free(dir);
            }
            g_free(full);
        }
        free_task_list(tasks);
    }

    g_free(ud->filepath);
    g_free(ud);
    return;
}

static void
on_menu_delete(GtkWidget *m, gpointer data) {
    UIUpdateData *ud;
    GPtrArray *tasks;
    GtkWidget *dialog;
    int32 count;

    (void)m;
    ud = (UIUpdateData *)data;
    tasks = get_target_tasks(ud->side, ud->filepath, UI_ACTION_DELETE);

    if (tasks == NULL) {
        g_free(ud->filepath);
        g_free(ud);
        return;
    }

    count = (int32)tasks->len;
    dialog = gtk_message_dialog_new(GTK_WINDOW(cecup_state.gtk_window),
                                    GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING,
                                    GTK_BUTTONS_YES_NO,
                                    "Permanently delete %d item(s)?", count);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES) {
        cecup_state.cancel_sync = 0;
        gtk_widget_set_sensitive(cecup_state.preview_button, FALSE);
        gtk_widget_set_sensitive(cecup_state.sync_button, FALSE);
        gtk_widget_set_sensitive(cecup_state.stop_button, TRUE);
        g_thread_new("bulk_delete", bulk_sync_worker, tasks);
    } else {
        free_task_list(tasks);
    }

    gtk_widget_destroy(dialog);
    g_free(ud->filepath);
    g_free(ud);
    return;
}

static void
on_menu_diff(GtkWidget *m, gpointer data) {
    UIUpdateData *ud;
    GPtrArray *tasks;
    char *diff_tool;
    char *term_cmd;

    (void)m;
    ud = (UIUpdateData *)data;
    diff_tool = (char *)gtk_entry_get_text(GTK_ENTRY(cecup_state.diff_entry));
    term_cmd = (char *)gtk_entry_get_text(GTK_ENTRY(cecup_state.term_entry));
    tasks = get_target_tasks(ud->side, ud->filepath, ud->action);

    if (tasks != NULL) {
        for (int32 i = 0; i < (int32)tasks->len; i += 1) {
            UIUpdateData *t;

            t = (UIUpdateData *)g_ptr_array_index(tasks, i);
            t->diff_tool = g_strdup(diff_tool);
            t->term_cmd = g_strdup(term_cmd);
            g_thread_new("diff_worker", diff_worker, t);
        }
        g_ptr_array_unref(tasks);
    }

    g_free(ud->filepath);
    g_free(ud);
    return;
}

static void
on_menu_exclude_ext(GtkWidget *m, gpointer data) {
    UIUpdateData *ud;
    GPtrArray *tasks;
    FILE *fp;

    (void)m;
    ud = (UIUpdateData *)data;
    tasks = get_target_tasks(ud->side, ud->filepath, ud->action);

    if (tasks != NULL) {
        if ((fp = fopen(cecup_state.exclude_path, "a")) != NULL) {
            for (uint32 i = 0; i < tasks->len; i += 1) {
                UIUpdateData *t;
                char *ext;

                t = (UIUpdateData *)g_ptr_array_index(tasks, i);
                if ((ext = strrchr(t->filepath, '.')) != NULL) {
                    fprintf(fp, "\n*%s", ext);
                }
            }
            fclose(fp);
            on_preview_clicked(NULL, NULL);
        }
        free_task_list(tasks);
    }

    g_free(ud->filepath);
    g_free(ud);
    return;
}

static void
on_menu_exclude_dir(GtkWidget *m, gpointer data) {
    UIUpdateData *ud;
    GPtrArray *tasks;
    FILE *fp;

    (void)m;
    ud = (UIUpdateData *)data;
    tasks = get_target_tasks(ud->side, ud->filepath, ud->action);

    if (tasks != NULL) {
        if ((fp = fopen(cecup_state.exclude_path, "a")) != NULL) {
            for (int32 i = 0; i < (int32)tasks->len; i += 1) {
                UIUpdateData *t;
                char *dir;

                t = (UIUpdateData *)g_ptr_array_index(tasks, i);
                if ((dir = g_path_get_dirname(t->filepath)) != NULL) {
                    if (g_strcmp0(dir, ".") != 0) {
                        fprintf(fp, "\n/%s/", dir);
                    }
                    g_free(dir);
                }
            }
            fclose(fp);
            on_preview_clicked(NULL, NULL);
        }
        free_task_list(tasks);
    }

    g_free(ud->filepath);
    g_free(ud);
    return;
}

static void
save_config(void) {
    GKeyFile *key;
    char *out;
    gsize len;

    key = g_key_file_new();
    g_key_file_set_string(key, "Paths", "src",
                          gtk_entry_get_text(GTK_ENTRY(cecup_state.src_entry)));
    g_key_file_set_string(key, "Paths", "dst",
                          gtk_entry_get_text(GTK_ENTRY(cecup_state.dst_entry)));
    g_key_file_set_string(
        key, "Tools", "diff",
        gtk_entry_get_text(GTK_ENTRY(cecup_state.diff_entry)));
    g_key_file_set_string(
        key, "Tools", "term",
        gtk_entry_get_text(GTK_ENTRY(cecup_state.term_entry)));
    g_key_file_set_boolean(key, "Filters", "new",
                           gtk_toggle_button_get_active(
                               GTK_TOGGLE_BUTTON(cecup_state.filter_new)));
    g_key_file_set_boolean(key, "Filters", "hard",
                           gtk_toggle_button_get_active(
                               GTK_TOGGLE_BUTTON(cecup_state.filter_hard)));
    g_key_file_set_boolean(key, "Filters", "update",
                           gtk_toggle_button_get_active(
                               GTK_TOGGLE_BUTTON(cecup_state.filter_update)));
    g_key_file_set_boolean(key, "Filters", "equal",
                           gtk_toggle_button_get_active(
                               GTK_TOGGLE_BUTTON(cecup_state.filter_equal)));
    g_key_file_set_boolean(key, "Filters", "delete",
                           gtk_toggle_button_get_active(
                               GTK_TOGGLE_BUTTON(cecup_state.filter_delete)));
    g_key_file_set_boolean(key, "Filters", "ignore",
                           gtk_toggle_button_get_active(
                               GTK_TOGGLE_BUTTON(cecup_state.filter_ignore)));
    g_key_file_set_boolean(key, "Options", "check_fs",
                           gtk_toggle_button_get_active(
                               GTK_TOGGLE_BUTTON(cecup_state.check_fs_toggle)));

    out = g_key_file_to_data(key, &len, NULL);
    g_file_set_contents(cecup_state.config_path, out, (gssize)len, NULL);

    g_free(out);
    g_key_file_free(key);
    return;
}

static void
on_config_changed(GtkWidget *widget, gpointer data) {
    (void)widget;
    (void)data;
    save_config();
    return;
}

static void
on_reset_clicked(GtkWidget *b, gpointer data) {
    (void)b;
    (void)data;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup_state.filter_new),
                                 TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup_state.filter_hard),
                                 TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup_state.filter_update),
                                 TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup_state.filter_equal),
                                 FALSE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup_state.filter_delete),
                                 TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup_state.filter_ignore),
                                 TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup_state.check_fs_toggle),
                                 FALSE);
    gtk_entry_set_text(GTK_ENTRY(cecup_state.diff_entry), "unidiff.bash");
    gtk_entry_set_text(GTK_ENTRY(cecup_state.term_entry), "xterm");
    save_config();
    return;
}

static void
on_stop_clicked(GtkWidget *b, gpointer data) {
    (void)b;
    (void)data;
    cecup_state.cancel_sync = 1;
    return;
}

static void
on_preview_clicked(GtkWidget *b, gpointer data) {
    char *s;
    char *d;
    ThreadData *td;

    (void)data;
    s = (char *)gtk_entry_get_text(GTK_ENTRY(cecup_state.src_entry));
    d = (char *)gtk_entry_get_text(GTK_ENTRY(cecup_state.dst_entry));
    (void)b;

    if (strlen(s) < 1 || strlen(d) < 1) {
        return;
    }

    cecup_state.cancel_sync = 0;
    gtk_widget_set_sensitive(cecup_state.preview_button, FALSE);
    gtk_widget_set_sensitive(cecup_state.sync_button, FALSE);
    gtk_widget_set_sensitive(cecup_state.stop_button, TRUE);

    td = g_new0(ThreadData, 1);
    td->is_preview = 1;
    td->show_equal = gtk_toggle_button_get_active(
        GTK_TOGGLE_BUTTON(cecup_state.filter_equal));
    strncpy(td->src_path, s, 1023);
    strncpy(td->dst_path, d, 1023);
    g_thread_new("worker", sync_worker, td);
    return;
}

static void
on_filter_toggled(GtkToggleButton *b, gpointer data) {
    (void)data;
    (void)b;
    refresh_ui_list();
    save_config();
    return;
}

static void
on_sort_changed(GtkTreeSortable *s, gpointer d) {
    int32 id;
    GtkSortType o;

    (void)d;
    if (gtk_tree_sortable_get_sort_column_id(s, &id, &o)) {
        cecup_state.sort_col = id;
        cecup_state.sort_order = o;
        refresh_ui_list();
    }
    return;
}

static void
on_cell_toggled(GtkCellRendererToggle *cell, char *path_str, gpointer data) {
    GtkTreePath *p;
    GtkTreeIter i;
    char *f_path;

    (void)cell;
    (void)data;
    p = gtk_tree_path_new_from_string(path_str);

    if (gtk_tree_model_get_iter(GTK_TREE_MODEL(cecup_state.store), &i, p)) {
        gtk_tree_model_get(GTK_TREE_MODEL(cecup_state.store), &i, COL_SRC_PATH,
                           &f_path, -1);

        for (int32 k = 0; k < (int32)cecup_state.rows->len; k += 1) {
            CecupRow *row;
            row = (CecupRow *)g_ptr_array_index(cecup_state.rows, k);
            if (g_strcmp0(row->src_path, f_path) == 0
                || g_strcmp0(row->dst_path, f_path) == 0) {
                row->selected = !row->selected;
                break;
            }
        }
        g_free(f_path);
        refresh_ui_list();
    }

    gtk_tree_path_free(p);
    return;
}

static void
on_exclude_clicked(GtkWidget *b, gpointer data) {
    GtkWidget *dialog;
    GtkWidget *scroll;
    GtkWidget *view;
    GtkTextBuffer *buffer;
    char *text;
    gsize len;

    (void)data;
    dialog = gtk_dialog_new_with_buttons(
        "Exclusions", GTK_WINDOW(cecup_state.gtk_window), GTK_DIALOG_MODAL,
        "_Save", GTK_RESPONSE_ACCEPT, "_Close", GTK_RESPONSE_CLOSE, NULL);
    (void)b;
    gtk_window_set_default_size(GTK_WINDOW(dialog), 600, 500);
    scroll = gtk_scrolled_window_new(NULL, NULL);
    view = gtk_text_view_new();
    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));

    if (g_file_get_contents(cecup_state.exclude_path, &text, &len, NULL)) {
        gtk_text_buffer_set_text(buffer, text, -1);
        g_free(text);
    }

    gtk_container_add(GTK_CONTAINER(scroll), view);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
                       scroll, TRUE, TRUE, 5);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        GtkTextIter s;
        GtkTextIter e;
        char *content;

        gtk_text_buffer_get_bounds(buffer, &s, &e);
        content = gtk_text_buffer_get_text(buffer, &s, &e, FALSE);
        g_file_set_contents(cecup_state.exclude_path, content, -1, NULL);
        g_free(content);
        on_preview_clicked(NULL, NULL);
    }
    gtk_widget_destroy(dialog);
    return;
}

static void
on_invert_clicked(GtkWidget *b, gpointer data) {
    char *path_src;
    char *path_dst;

    (void)b;
    (void)data;
    path_src = g_strdup(gtk_entry_get_text(GTK_ENTRY(cecup_state.src_entry)));
    path_dst = g_strdup(gtk_entry_get_text(GTK_ENTRY(cecup_state.dst_entry)));
    gtk_entry_set_text(GTK_ENTRY(cecup_state.src_entry), path_dst);
    gtk_entry_set_text(GTK_ENTRY(cecup_state.dst_entry), path_src);
    g_free(path_src);
    g_free(path_dst);
    on_preview_clicked(NULL, NULL);
    return;
}

static void
on_sync_clicked(GtkWidget *b, gpointer data) {
    char *path_src;
    char *path_dst;
    GtkWidget *dialog;

    (void)data;
    path_src = (char *)gtk_entry_get_text(GTK_ENTRY(cecup_state.src_entry));
    path_dst = (char *)gtk_entry_get_text(GTK_ENTRY(cecup_state.dst_entry));
    (void)b;
    dialog = gtk_message_dialog_new(GTK_WINDOW(cecup_state.gtk_window),
                                    GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
                                    GTK_BUTTONS_YES_NO, "Sync %s -> %s?",
                                    path_src, path_dst);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES) {
        ThreadData *thread_data;

        cecup_state.cancel_sync = 0;
        gtk_widget_set_sensitive(cecup_state.preview_button, FALSE);
        gtk_widget_set_sensitive(cecup_state.sync_button, FALSE);
        gtk_widget_set_sensitive(cecup_state.stop_button, TRUE);
        thread_data = g_new0(ThreadData, 1);
        thread_data->is_preview = 0;
        thread_data->show_equal = 0;
        thread_data->check_different_fs = gtk_toggle_button_get_active(
            GTK_TOGGLE_BUTTON(cecup_state.check_fs_toggle));
        strncpy(thread_data->src_path, path_src, 1023);
        strncpy(thread_data->dst_path, path_dst, 1023);
        g_thread_new("worker", sync_worker, thread_data);
    }
    gtk_widget_destroy(dialog);
    return;
}

static void
on_browse_src(GtkWidget *b, gpointer data) {
    GtkWidget *dialog;

    (void)data;
    (void)b;
    dialog = gtk_file_chooser_dialog_new(
        "Src", GTK_WINDOW(cecup_state.gtk_window),
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "_Cancel", GTK_RESPONSE_CANCEL,
        "_Select", GTK_RESPONSE_ACCEPT, NULL);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *path;

        path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        gtk_entry_set_text(GTK_ENTRY(cecup_state.src_entry), path);
        g_free(path);
    }
    gtk_widget_destroy(dialog);
    return;
}

static void
on_browse_dst(GtkWidget *b, gpointer data) {
    GtkWidget *dialog;

    (void)data;
    (void)b;
    dialog = gtk_file_chooser_dialog_new(
        "Dst", GTK_WINDOW(cecup_state.gtk_window),
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "_Cancel", GTK_RESPONSE_CANCEL,
        "_Select", GTK_RESPONSE_ACCEPT, NULL);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *path;

        path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        gtk_entry_set_text(GTK_ENTRY(cecup_state.dst_entry), path);
        g_free(path);
    }
    gtk_widget_destroy(dialog);
    return;
}

static void
on_scroll_sync(GtkAdjustment *s, gpointer d) {
    double v;

    v = gtk_adjustment_get_value(s);
    if (gtk_adjustment_get_value(GTK_ADJUSTMENT(d)) != v) {
        gtk_adjustment_set_value(GTK_ADJUSTMENT(d), v);
    }
    return;
}

static gboolean
on_tree_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    int32 side;

    (void)data;
    side = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "side"));

    if (event->type == GDK_BUTTON_PRESS && event->button == 3) {
        GtkTreePath *path;

        if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), (gint)event->x,
                                          (gint)event->y, &path, NULL, NULL,
                                          NULL)) {
            GtkTreeModel *model;
            GtkTreeIter iter;

            model = gtk_tree_view_get_model(GTK_TREE_VIEW(widget));
            if (gtk_tree_model_get_iter(model, &iter, path)) {
                int32 path_col;
                int32 act_col;
                int32 other_col;
                char *f_path;
                char *action_str;
                char *other_path;
                UIUpdateData *ud;
                GtkWidget *menu;
                GtkWidget *item;
                GtkWidget *sub;
                GtkWidget *sub_ext;
                GtkWidget *sub_dir;
                int32 is_disabled;
                enum CecupAction action;

                path_col = (side == 0) ? COL_SRC_PATH : COL_DST_PATH;
                act_col = (side == 0) ? COL_SRC_ACTION : COL_DST_ACTION;
                other_col = (side == 0) ? COL_DST_PATH : COL_SRC_PATH;
                gtk_tree_model_get(model, &iter, path_col, &f_path, act_col,
                                   &action_str, other_col, &other_path, -1);

                action = UI_ACTION_NONE;
                for (int32 k = 0; k < NUM_UI_ACTIONS; k += 1) {
                    if (g_strcmp0(action_str, action_strings[k]) == 0) {
                        action = k;
                        break;
                    }
                }

                ud = g_new0(UIUpdateData, 1);
                ud->filepath = g_strdup(f_path);
                ud->action = action;
                ud->side = side;

                menu = gtk_menu_new();
                item = gtk_menu_item_new_with_label("Open File");
                g_signal_connect(item, "activate", G_CALLBACK(on_menu_open),
                                 ud);
                gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

                item = gtk_menu_item_new_with_label("Open Folder");
                g_signal_connect(item, "activate", G_CALLBACK(on_menu_open_dir),
                                 ud);
                gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

                item = gtk_menu_item_new_with_label("Apply");
                g_object_set_data(G_OBJECT(item), "target_tree", widget);
                g_signal_connect(item, "activate", G_CALLBACK(on_menu_apply),
                                 ud);
                gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

                item = gtk_menu_item_new_with_label("Exclude...");
                sub = gtk_menu_new();
                gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), sub);

                sub_ext = gtk_menu_item_new_with_label("Ext");
                g_signal_connect(sub_ext, "activate",
                                 G_CALLBACK(on_menu_exclude_ext), ud);
                gtk_menu_shell_append(GTK_MENU_SHELL(sub), sub_ext);

                sub_dir = gtk_menu_item_new_with_label("Dir");
                g_signal_connect(sub_dir, "activate",
                                 G_CALLBACK(on_menu_exclude_dir), ud);
                gtk_menu_shell_append(GTK_MENU_SHELL(sub), sub_dir);

                gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

                item = gtk_menu_item_new_with_label("Diff");
                is_disabled = (g_strcmp0(f_path, "-") == 0
                               || g_strcmp0(other_path, "-") == 0
                               || action == UI_ACTION_HARDLINK);
                if (is_disabled) {
                    gtk_widget_set_sensitive(item, FALSE);
                } else {
                    g_signal_connect(item, "activate", G_CALLBACK(on_menu_diff),
                                     ud);
                }
                gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

                item = gtk_menu_item_new_with_label("Delete");
                if (g_strcmp0(f_path, "-") == 0) {
                    gtk_widget_set_sensitive(item, FALSE);
                } else {
                    g_signal_connect(item, "activate",
                                     G_CALLBACK(on_menu_delete), ud);
                }
                gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

                gtk_widget_show_all(menu);
                gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
                g_free(f_path);
                g_free(other_path);
                g_free(action_str);
            }
            gtk_tree_path_free(path);
            return TRUE;
        }
    } else if (event->type == GDK_BUTTON_PRESS && event->button == 1) {
        GtkTreePath *path;

        if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), (gint)event->x,
                                          (gint)event->y, &path, NULL, NULL,
                                          NULL)) {
            GtkTreeSelection *sel;

            sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(widget));
            if (gtk_tree_selection_path_is_selected(sel, path)) {
                gtk_tree_selection_unselect_path(sel, path);
                gtk_tree_path_free(path);
                return TRUE;
            }
            gtk_tree_path_free(path);
        }
    }
    return FALSE;
}

static gboolean
on_tree_tooltip(GtkWidget *w, gint x, gint y, gboolean k, GtkTooltip *t,
                gpointer d) {
    GtkTreePath *p;
    gint bx;
    gint by;

    (void)k;
    (void)d;
    gtk_tree_view_convert_widget_to_bin_window_coords(GTK_TREE_VIEW(w), x, y,
                                                      &bx, &by);
    if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(w), bx, by, &p, NULL, NULL,
                                      NULL)) {
        GtkTreeModel *m;
        GtkTreeIter i;

        m = gtk_tree_view_get_model(GTK_TREE_VIEW(w));
        if (gtk_tree_model_get_iter(m, &i, p)) {
            char *r;

            gtk_tree_model_get(m, &i, COL_REASON, &r, -1);
            if (r && strlen(r) > 0) {
                gtk_tooltip_set_text(t, r);
                g_free(r);
                gtk_tree_path_free(p);
                return TRUE;
            }
            g_free(r);
        }
        gtk_tree_path_free(p);
    }
    return FALSE;
}
