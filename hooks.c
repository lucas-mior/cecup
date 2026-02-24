#include <gtk/gtk.h>
#include <stdlib.h>
#include "cecup.h"
#include "rsync.c"

static void
free_task_list(GList *tasks) {
    for (GList *l = tasks; l != NULL; l = l->next) {
        UIUpdateData *t;

        t = (UIUpdateData *)l->data;
        g_free(t->filepath);
        g_free(t->action);
        g_free(t->src_base);
        g_free(t->dst_base);
        g_free(t->term_cmd);
        g_free(t->diff_tool);
        g_free(t);
    }
    g_list_free(tasks);
    return;
}

static GList *
get_target_tasks(AppWidgets *w, int32 side, char *clicked_path,
                 char *clicked_action) {
    GList *tasks;
    GtkTreeModel *model;
    GtkTreeIter iter;
    gboolean valid;
    char *src_base;
    char *dst_base;

    tasks = NULL;
    model = GTK_TREE_MODEL(w->store);
    valid = gtk_tree_model_get_iter_first(model, &iter);
    src_base = (char *)gtk_entry_get_text(GTK_ENTRY(w->src_entry));
    dst_base = (char *)gtk_entry_get_text(GTK_ENTRY(w->dst_entry));

    while (valid) {
        gboolean selected;

        gtk_tree_model_get(model, &iter, COL_SELECTED, &selected, -1);
        if (selected) {
            char *f_path;
            char *action;
            UIUpdateData *task;

            gtk_tree_model_get(
                model, &iter, side == 0 ? COL_SRC_PATH : COL_DST_PATH, &f_path,
                side == 0 ? COL_SRC_ACTION : COL_DST_ACTION, &action, -1);

            if (g_strcmp0(f_path, "-") != 0) {
                task = g_new0(UIUpdateData, 1);
                task->widgets = w;
                task->filepath = g_strdup(f_path);
                task->action = g_strdup(action);
                task->side = side;
                task->src_base = g_strdup(src_base);
                task->dst_base = g_strdup(dst_base);
                tasks = g_list_append(tasks, task);
            }
            g_free(f_path);
            g_free(action);
        }
        valid = gtk_tree_model_iter_next(model, &iter);
    }

    if (tasks == NULL && g_strcmp0(clicked_path, "-") != 0) {
        UIUpdateData *task;

        task = g_new0(UIUpdateData, 1);
        task->widgets = w;
        task->filepath = g_strdup(clicked_path);
        task->action = g_strdup(clicked_action);
        task->side = side;
        task->src_base = g_strdup(src_base);
        task->dst_base = g_strdup(dst_base);
        tasks = g_list_append(tasks, task);
    }

    return tasks;
}

static void
on_menu_apply(GtkWidget *m, gpointer data) {
    UIUpdateData *ud;
    GList *tasks;

    (void)m;
    ud = (UIUpdateData *)data;
    tasks = get_target_tasks(ud->widgets, ud->side, ud->filepath, ud->action);

    if (tasks != NULL) {
        ud->widgets->cancel_sync = 0;
        gtk_widget_set_sensitive(ud->widgets->preview_button, FALSE);
        gtk_widget_set_sensitive(ud->widgets->sync_button, FALSE);
        gtk_widget_set_sensitive(ud->widgets->stop_button, TRUE);
        g_thread_new("bulk_sync", bulk_sync_worker, tasks);
    }

    g_free(ud->filepath);
    g_free(ud->action);
    g_free(ud);
    return;
}

static void
on_menu_open(GtkWidget *m, gpointer data) {
    UIUpdateData *ud;
    GList *tasks;

    (void)m;
    ud = (UIUpdateData *)data;
    tasks = get_target_tasks(ud->widgets, ud->side, ud->filepath, ud->action);

    for (GList *l = tasks; l != NULL; l = l->next) {
        UIUpdateData *t;
        char *full;
        char *cmd;

        t = (UIUpdateData *)l->data;
        full = g_build_filename(ud->side == 0 ? t->src_base : t->dst_base,
                                t->filepath, NULL);
        cmd = g_strdup_printf("xdg-open '%s' &", full);
        system(cmd);
        g_free(cmd);
        g_free(full);
    }

    free_task_list(tasks);
    g_free(ud->filepath);
    g_free(ud->action);
    g_free(ud);
    return;
}

static void
on_menu_open_dir(GtkWidget *m, gpointer data) {
    UIUpdateData *ud;
    GList *tasks;

    (void)m;
    ud = (UIUpdateData *)data;
    tasks = get_target_tasks(ud->widgets, ud->side, ud->filepath, ud->action);

    for (GList *l = tasks; l != NULL; l = l->next) {
        UIUpdateData *t;
        char *full;
        char *dir;

        t = (UIUpdateData *)l->data;
        full = g_build_filename(ud->side == 0 ? t->src_base : t->dst_base,
                                t->filepath, NULL);
        if ((dir = g_path_get_dirname(full)) != NULL) {
            char *cmd;

            cmd = g_strdup_printf("xdg-open '%s' &", dir);
            system(cmd);
            g_free(cmd);
            g_free(dir);
        }
        g_free(full);
    }

    free_task_list(tasks);
    g_free(ud->filepath);
    g_free(ud->action);
    g_free(ud);
    return;
}

static void
on_menu_delete(GtkWidget *m, gpointer data) {
    UIUpdateData *ud;
    GList *tasks;
    GtkWidget *dialog;
    int32 count;

    (void)m;
    ud = (UIUpdateData *)data;
    tasks = get_target_tasks(ud->widgets, ud->side, ud->filepath, "Delete");
    count = (int32)g_list_length(tasks);

    if (count == 0) {
        g_free(ud->filepath);
        g_free(ud->action);
        g_free(ud);
        return;
    }

    dialog = gtk_message_dialog_new(GTK_WINDOW(ud->widgets->gtk_window),
                                    GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING,
                                    GTK_BUTTONS_YES_NO,
                                    "Permanently delete %d item(s)?", count);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES) {
        ud->widgets->cancel_sync = 0;
        gtk_widget_set_sensitive(ud->widgets->preview_button, FALSE);
        gtk_widget_set_sensitive(ud->widgets->sync_button, FALSE);
        gtk_widget_set_sensitive(ud->widgets->stop_button, TRUE);
        g_thread_new("bulk_delete", bulk_sync_worker, tasks);
    } else {
        free_task_list(tasks);
    }

    gtk_widget_destroy(dialog);
    g_free(ud->filepath);
    g_free(ud->action);
    g_free(ud);
    return;
}

static void
on_menu_diff(GtkWidget *m, gpointer data) {
    UIUpdateData *ud;
    GList *tasks;
    char *diff_tool;
    char *term_cmd;

    (void)m;
    ud = (UIUpdateData *)data;
    diff_tool = (char *)gtk_entry_get_text(GTK_ENTRY(ud->widgets->diff_entry));
    term_cmd = (char *)gtk_entry_get_text(GTK_ENTRY(ud->widgets->term_entry));
    tasks = get_target_tasks(ud->widgets, ud->side, ud->filepath, ud->action);

    for (GList *l = tasks; l != NULL; l = l->next) {
        UIUpdateData *t;

        t = (UIUpdateData *)l->data;
        t->diff_tool = g_strdup(diff_tool);
        t->term_cmd = g_strdup(term_cmd);
        g_thread_new("diff_worker", diff_worker, t);
    }

    g_list_free(tasks);
    g_free(ud->filepath);
    g_free(ud->action);
    g_free(ud);
    return;
}

static void
on_menu_exclude_ext(GtkWidget *m, gpointer data) {
    UIUpdateData *ud;
    GList *tasks;
    FILE *fp;

    (void)m;
    ud = (UIUpdateData *)data;
    tasks = get_target_tasks(ud->widgets, ud->side, ud->filepath, ud->action);

    if ((fp = fopen(ud->widgets->exclude_path, "a")) != NULL) {
        for (GList *l = tasks; l != NULL; l = l->next) {
            UIUpdateData *t;
            char *ext;

            t = (UIUpdateData *)l->data;
            if ((ext = strrchr(t->filepath, '.')) != NULL) {
                fprintf(fp, "\n*%s", ext);
            }
        }
        fclose(fp);
        on_preview_clicked(NULL, ud->widgets);
    }

    free_task_list(tasks);
    g_free(ud->filepath);
    g_free(ud->action);
    g_free(ud);
    return;
}

static void
on_menu_exclude_dir(GtkWidget *m, gpointer data) {
    UIUpdateData *ud;
    GList *tasks;
    FILE *fp;

    (void)m;
    ud = (UIUpdateData *)data;
    tasks = get_target_tasks(ud->widgets, ud->side, ud->filepath, ud->action);

    if ((fp = fopen(ud->widgets->exclude_path, "a")) != NULL) {
        for (GList *l = tasks; l != NULL; l = l->next) {
            UIUpdateData *t;
            char *dir;

            t = (UIUpdateData *)l->data;
            if ((dir = g_path_get_dirname(t->filepath)) != NULL) {
                if (g_strcmp0(dir, ".") != 0) {
                    fprintf(fp, "\n/%s/", dir);
                }
                g_free(dir);
            }
        }
        fclose(fp);
        on_preview_clicked(NULL, ud->widgets);
    }

    free_task_list(tasks);
    g_free(ud->filepath);
    g_free(ud->action);
    g_free(ud);
    return;
}

static void
save_config(AppWidgets *w) {
    GKeyFile *key;
    char *out;
    gsize len;

    key = g_key_file_new();
    g_key_file_set_string(key, "Paths", "src",
                          gtk_entry_get_text(GTK_ENTRY(w->src_entry)));
    g_key_file_set_string(key, "Paths", "dst",
                          gtk_entry_get_text(GTK_ENTRY(w->dst_entry)));
    g_key_file_set_string(key, "Tools", "diff",
                          gtk_entry_get_text(GTK_ENTRY(w->diff_entry)));
    g_key_file_set_string(key, "Tools", "term",
                          gtk_entry_get_text(GTK_ENTRY(w->term_entry)));
    g_key_file_set_boolean(
        key, "Filters", "new",
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->filter_new)));
    g_key_file_set_boolean(
        key, "Filters", "hard",
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->filter_hard)));
    g_key_file_set_boolean(
        key, "Filters", "update",
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->filter_update)));
    g_key_file_set_boolean(
        key, "Filters", "equal",
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->filter_equal)));
    g_key_file_set_boolean(
        key, "Filters", "delete",
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->filter_delete)));
    g_key_file_set_boolean(
        key, "Filters", "ignore",
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->filter_ignore)));
    g_key_file_set_boolean(
        key, "Options", "check_fs",
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->check_fs_toggle)));

    out = g_key_file_to_data(key, &len, NULL);
    g_file_set_contents(w->config_path, out, (gssize)len, NULL);

    g_free(out);
    g_key_file_free(key);
    return;
}

static void
on_config_changed(GtkWidget *widget, gpointer data) {
    AppWidgets *w;

    (void)widget;
    w = (AppWidgets *)data;
    save_config(w);
    return;
}

static void
on_reset_clicked(GtkWidget *b, gpointer data) {
    AppWidgets *w;

    (void)b;
    w = (AppWidgets *)data;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->filter_new), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->filter_hard), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->filter_update), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->filter_equal), FALSE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->filter_delete), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->filter_ignore), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->check_fs_toggle), FALSE);
    gtk_entry_set_text(GTK_ENTRY(w->diff_entry), "unidiff.bash");
    gtk_entry_set_text(GTK_ENTRY(w->term_entry), "xterm");
    save_config(w);
    return;
}

static void
on_stop_clicked(GtkWidget *b, gpointer data) {
    AppWidgets *w;

    (void)b;
    w = (AppWidgets *)data;
    w->cancel_sync = 1;
    return;
}

static void
on_preview_clicked(GtkWidget *b, gpointer data) {
    AppWidgets *w;
    char *s;
    char *d;
    ThreadData *td;

    w = (AppWidgets *)data;
    s = (char *)gtk_entry_get_text(GTK_ENTRY(w->src_entry));
    d = (char *)gtk_entry_get_text(GTK_ENTRY(w->dst_entry));
    (void)b;

    if (strlen(s) < 1 || strlen(d) < 1) {
        return;
    }

    w->cancel_sync = 0;
    gtk_widget_set_sensitive(w->preview_button, FALSE);
    gtk_widget_set_sensitive(w->sync_button, FALSE);
    gtk_widget_set_sensitive(w->stop_button, TRUE);

    td = g_new0(ThreadData, 1);
    td->widgets = w;
    td->is_preview = 1;
    td->show_equal
        = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->filter_equal));
    strncpy(td->src_path, s, 1023);
    strncpy(td->dst_path, d, 1023);
    g_thread_new("worker", sync_worker, td);
    return;
}

static void
on_filter_toggled(GtkToggleButton *b, gpointer data) {
    AppWidgets *w;

    w = (AppWidgets *)data;
    (void)b;
    gtk_tree_model_filter_refilter(w->filter_model);
    save_config(w);
    return;
}

static void
on_sort_changed(GtkTreeSortable *s, gpointer d) {
    int32 id;
    GtkSortType o;

    if (gtk_tree_sortable_get_sort_column_id(s, &id, &o)) {
        GtkTreeViewColumn *c;

        if ((c = gtk_tree_view_get_column(GTK_TREE_VIEW(d), 1)) != NULL) {
            gtk_tree_view_column_set_sort_indicator(c, TRUE);
            gtk_tree_view_column_set_sort_order(c, o);
        }
    }
    return;
}

static void
on_cell_toggled(GtkCellRendererToggle *cell, char *path_str, gpointer data) {
    AppWidgets *w;
    GtkTreePath *filter_path;
    GtkTreePath *sort_path;
    GtkTreePath *child_path;
    GtkTreeIter iter;
    gboolean val;

    (void)cell;
    w = (AppWidgets *)data;
    sort_path = gtk_tree_path_new_from_string(path_str);

    filter_path = gtk_tree_model_sort_convert_path_to_child_path(w->sort_model,
                                                                 sort_path);

    child_path = gtk_tree_model_filter_convert_path_to_child_path(
        w->filter_model, filter_path);

    if (gtk_tree_model_get_iter(GTK_TREE_MODEL(w->store), &iter, child_path)) {
        gtk_tree_model_get(GTK_TREE_MODEL(w->store), &iter, COL_SELECTED, &val,
                           -1);
        gtk_list_store_set(w->store, &iter, COL_SELECTED, !val, -1);
    }

    gtk_tree_path_free(sort_path);
    gtk_tree_path_free(filter_path);
    gtk_tree_path_free(child_path);
    return;
}

static void
on_exclude_clicked(GtkWidget *b, gpointer data) {
    AppWidgets *w;
    GtkWidget *dialog;
    GtkWidget *scroll;
    GtkWidget *view;
    GtkTextBuffer *buffer;
    char *text;
    gsize len;

    w = (AppWidgets *)data;
    dialog = gtk_dialog_new_with_buttons(
        "Exclusions", GTK_WINDOW(w->gtk_window), GTK_DIALOG_MODAL, "_Save",
        GTK_RESPONSE_ACCEPT, "_Close", GTK_RESPONSE_CLOSE, NULL);
    (void)b;
    gtk_window_set_default_size(GTK_WINDOW(dialog), 600, 500);
    scroll = gtk_scrolled_window_new(NULL, NULL);
    view = gtk_text_view_new();
    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));

    if (g_file_get_contents(w->exclude_path, &text, &len, NULL)) {
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
        g_file_set_contents(w->exclude_path, content, -1, NULL);
        g_free(content);
        on_preview_clicked(NULL, w);
    }
    gtk_widget_destroy(dialog);
    return;
}

static void
on_invert_clicked(GtkWidget *b, gpointer data) {
    AppWidgets *w;
    char *path_src;
    char *path_dst;

    (void)b;
    w = (AppWidgets *)data;
    path_src = g_strdup(gtk_entry_get_text(GTK_ENTRY(w->src_entry)));
    path_dst = g_strdup(gtk_entry_get_text(GTK_ENTRY(w->dst_entry)));
    gtk_entry_set_text(GTK_ENTRY(w->src_entry), path_dst);
    gtk_entry_set_text(GTK_ENTRY(w->dst_entry), path_src);
    g_free(path_src);
    g_free(path_dst);
    on_preview_clicked(NULL, w);
    return;
}

static void
on_sync_clicked(GtkWidget *b, gpointer data) {
    AppWidgets *w;
    char *path_src;
    char *path_dst;
    GtkWidget *dialog;

    w = (AppWidgets *)data;
    path_src = (char *)gtk_entry_get_text(GTK_ENTRY(w->src_entry));
    path_dst = (char *)gtk_entry_get_text(GTK_ENTRY(w->dst_entry));
    (void)b;
    dialog = gtk_message_dialog_new(GTK_WINDOW(w->gtk_window), GTK_DIALOG_MODAL,
                                    GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
                                    "Sync %s -> %s?", path_src, path_dst);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES) {
        ThreadData *thread_data;

        w->cancel_sync = 0;
        gtk_widget_set_sensitive(w->preview_button, FALSE);
        gtk_widget_set_sensitive(w->sync_button, FALSE);
        gtk_widget_set_sensitive(w->stop_button, TRUE);
        thread_data = g_new0(ThreadData, 1);
        thread_data->widgets = w;
        thread_data->is_preview = 0;
        thread_data->show_equal = 0;
        thread_data->check_different_fs = gtk_toggle_button_get_active(
            GTK_TOGGLE_BUTTON(w->check_fs_toggle));
        strncpy(thread_data->src_path, path_src, 1023);
        strncpy(thread_data->dst_path, path_dst, 1023);
        g_thread_new("worker", sync_worker, thread_data);
    }
    gtk_widget_destroy(dialog);
    return;
}

static void
on_browse_src(GtkWidget *b, gpointer data) {
    AppWidgets *w;
    GtkWidget *dialog;

    w = (AppWidgets *)data;
    (void)b;
    dialog = gtk_file_chooser_dialog_new(
        "Src", GTK_WINDOW(w->gtk_window), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
        "_Cancel", GTK_RESPONSE_CANCEL, "_Select", GTK_RESPONSE_ACCEPT, NULL);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *path;

        path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        gtk_entry_set_text(GTK_ENTRY(w->src_entry), path);
        g_free(path);
    }
    gtk_widget_destroy(dialog);
    return;
}

static void
on_browse_dst(GtkWidget *b, gpointer data) {
    AppWidgets *w;
    GtkWidget *dialog;

    w = (AppWidgets *)data;
    (void)b;
    dialog = gtk_file_chooser_dialog_new(
        "Dst", GTK_WINDOW(w->gtk_window), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
        "_Cancel", GTK_RESPONSE_CANCEL, "_Select", GTK_RESPONSE_ACCEPT, NULL);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *path;

        path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        gtk_entry_set_text(GTK_ENTRY(w->dst_entry), path);
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
    AppWidgets *w;
    int32 side;

    w = (AppWidgets *)data;
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
                char *action;
                char *other_path;
                UIUpdateData *ud;
                GtkWidget *menu;
                GtkWidget *item;
                GtkWidget *sub;
                GtkWidget *sub_ext;
                GtkWidget *sub_dir;
                int32 is_disabled;

                path_col = (side == 0) ? COL_SRC_PATH : COL_DST_PATH;
                act_col = (side == 0) ? COL_SRC_ACTION : COL_DST_ACTION;
                other_col = (side == 0) ? COL_DST_PATH : COL_SRC_PATH;
                gtk_tree_model_get(model, &iter, path_col, &f_path, act_col,
                                   &action, other_col, &other_path, -1);

                ud = g_new0(UIUpdateData, 1);
                ud->widgets = w;
                ud->filepath = g_strdup(f_path);
                ud->action = g_strdup(action);
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
                               || g_strcmp0(action, "Hardlink") == 0);
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
                g_free(action);
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
