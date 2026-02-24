#include <gtk/gtk.h>
#include <stdlib.h>

#include "cecup.h"
#include "rsync.c"

static void
on_preview_clicked(GtkWidget *b, gpointer data) {
    AppWidgets *w;
    char *path_src;
    char *path_dst;
    ThreadData *thread_data;

    w = (AppWidgets *)data;
    path_src = (char *)gtk_entry_get_text(GTK_ENTRY(w->src_entry));
    path_dst = (char *)gtk_entry_get_text(GTK_ENTRY(w->dst_entry));
    (void)b;

    if (strlen64(path_src) < 1 || strlen64(path_dst) < 1) {
        return;
    }

    gtk_widget_set_sensitive(w->preview_button, FALSE);
    gtk_widget_set_sensitive(w->sync_button, FALSE);
    thread_data = g_new0(ThreadData, 1);
    thread_data->widgets = w;
    thread_data->is_preview = 1;
    thread_data->check_different_fs
        = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->check_fs_toggle));
    strncpy(thread_data->src_path, path_src, 1023);
    strncpy(thread_data->dst_path, path_dst, 1023);
    g_thread_new("worker", sync_worker, thread_data);
    return;
}

static void
on_menu_open(GtkWidget *m, gpointer data) {
    UIUpdateData *ud;
    char *base;
    char *full;
    char *cmd;

    (void)m;
    ud = (UIUpdateData *)data;
    base = (char *)gtk_entry_get_text(GTK_ENTRY(
        ud->side == 0 ? ud->widgets->src_entry : ud->widgets->dst_entry));
    if (g_strcmp0(ud->filepath, "-") == 0) {
        g_free(ud->filepath);
        g_free(ud->action);
        g_free(ud);
        return;
    }
    full = g_build_filename(base, ud->filepath, NULL);
    cmd = g_strdup_printf("xdg-open '%s' &", full);
    system(cmd);
    g_free(cmd);
    g_free(full);
    g_free(ud->filepath);
    g_free(ud->action);
    g_free(ud);
    return;
}

static void
on_menu_open_dir(GtkWidget *m, gpointer data) {
    UIUpdateData *ud;
    char *base;
    char *full;
    char *dir;
    char *cmd;

    (void)m;
    ud = (UIUpdateData *)data;
    base = (char *)gtk_entry_get_text(GTK_ENTRY(
        ud->side == 0 ? ud->widgets->src_entry : ud->widgets->dst_entry));
    if (g_strcmp0(ud->filepath, "-") == 0) {
        g_free(ud->filepath);
        g_free(ud->action);
        g_free(ud);
        return;
    }
    full = g_build_filename(base, ud->filepath, NULL);
    if ((dir = g_path_get_dirname(full)) != NULL) {
        cmd = g_strdup_printf("xdg-open '%s' &", dir);
        system(cmd);
        g_free(cmd);
        g_free(dir);
    }
    g_free(full);
    g_free(ud->filepath);
    g_free(ud->action);
    g_free(ud);
    return;
}

static void
on_menu_apply(GtkWidget *m, gpointer data) {
    UIUpdateData *ud;
    GtkTreeView *tree;
    GtkTreeSelection *sel;
    GtkTreeModel *model;
    GList *paths;
    GList *l;
    GtkTreeIter iter;
    char *f_path;
    char *action;
    int32 side;

    ud = (UIUpdateData *)data;
    tree = GTK_TREE_VIEW(g_object_get_data(G_OBJECT(m), "target_tree"));
    sel = gtk_tree_view_get_selection(tree);
    side = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(tree), "side"));
    paths = gtk_tree_selection_get_selected_rows(sel, &model);

    for (l = paths; l != NULL; l = l->next) {
        if (gtk_tree_model_get_iter(model, &iter, (GtkTreePath *)l->data)) {
            int32 path_col = (side == 0) ? COL_SRC_PATH : COL_DST_PATH;
            int32 act_col = (side == 0) ? COL_SRC_ACTION : COL_DST_ACTION;
            UIUpdateData *task;
            gtk_tree_model_get(model, &iter, path_col, &f_path, act_col,
                               &action, -1);

            if (g_strcmp0(f_path, "-") != 0) {
                task = g_new0(UIUpdateData, 1);
                task->widgets = ud->widgets;
                task->filepath = g_strdup(f_path);
                task->action = g_strdup(action);
                task->side = side;
                g_thread_new("single_worker", single_sync_worker, task);
            }
            g_free(f_path);
            g_free(action);
        }
    }
    g_list_free_full(paths, (GDestroyNotify)gtk_tree_path_free);
    g_free(ud->filepath);
    g_free(ud->action);
    g_free(ud);
    return;
}

static void
on_menu_diff(GtkWidget *m, gpointer data) {
    UIUpdateData *ud;
    (void)m;
    ud = (UIUpdateData *)data;
    g_thread_new("diff_worker", diff_worker, ud);
    return;
}

static void
on_menu_exclude_ext(GtkWidget *m, gpointer data) {
    UIUpdateData *ud;
    char *ext;
    FILE *fp;
    (void)m;
    ud = (UIUpdateData *)data;
    if ((ext = strrchr(ud->filepath, '.')) != NULL) {
        if ((fp = fopen(ud->widgets->exclude_path, "a")) != NULL) {
            fprintf(fp, "\n*%s", ext);
            fclose(fp);
            on_preview_clicked(NULL, ud->widgets);
        }
    }
    g_free(ud->filepath);
    g_free(ud->action);
    g_free(ud);
    return;
}

static void
on_menu_exclude_dir(GtkWidget *m, gpointer data) {
    UIUpdateData *ud;
    char *dir;
    FILE *fp;
    (void)m;
    ud = (UIUpdateData *)data;
    if ((dir = g_path_get_dirname(ud->filepath)) != NULL) {
        if (g_strcmp0(dir, ".") != 0) {
            if ((fp = fopen(ud->widgets->exclude_path, "a")) != NULL) {
                fprintf(fp, "\n/%s/", dir);
                fclose(fp);
                on_preview_clicked(NULL, ud->widgets);
            }
        }
        g_free(dir);
    }
    g_free(ud->filepath);
    g_free(ud->action);
    g_free(ud);
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
    ThreadData *thread_data;

    w = (AppWidgets *)data;
    path_src = (char *)gtk_entry_get_text(GTK_ENTRY(w->src_entry));
    path_dst = (char *)gtk_entry_get_text(GTK_ENTRY(w->dst_entry));
    (void)b;
    dialog = gtk_message_dialog_new(GTK_WINDOW(w->gtk_window), GTK_DIALOG_MODAL,
                                    GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
                                    "Sync %s -> %s?", path_src, path_dst);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES) {
        gtk_widget_set_sensitive(w->preview_button, FALSE);
        gtk_widget_set_sensitive(w->sync_button, FALSE);
        thread_data = g_new0(ThreadData, 1);
        thread_data->widgets = w;
        thread_data->is_preview = 0;
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
on_exclude_clicked(GtkWidget *b, gpointer data) {
    AppWidgets *w;
    GtkWidget *dialog;
    GtkWidget *scroll;
    GtkWidget *view;
    GtkTextBuffer *buffer;
    char *content;
    gsize length;
    (void)b;
    w = (AppWidgets *)data;
    dialog = gtk_dialog_new_with_buttons(
        "Exclusions", GTK_WINDOW(w->gtk_window), GTK_DIALOG_MODAL, "_Save",
        GTK_RESPONSE_ACCEPT, "_Close", GTK_RESPONSE_CLOSE, NULL);
    scroll = gtk_scrolled_window_new(NULL, NULL);
    view = gtk_text_view_new();
    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    if (g_file_get_contents(w->exclude_path, &content, &length, NULL)) {
        gtk_text_buffer_set_text(buffer, content, -1);
        g_free(content);
    }
    gtk_container_add(GTK_CONTAINER(scroll), view);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
                       scroll, TRUE, TRUE, 5);
    gtk_widget_show_all(dialog);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        GtkTextIter start, end;
        gtk_text_buffer_get_start_iter(buffer, &start);
        gtk_text_buffer_get_end_iter(buffer, &end);
        char *text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
        FILE *fp;
        if ((fp = fopen(w->exclude_path, "w")) != NULL) {
            fputs(text, fp);
            fclose(fp);
        }
        g_free(text);
        on_preview_clicked(NULL, w);
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
        char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
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
        char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        gtk_entry_set_text(GTK_ENTRY(w->dst_entry), path);
        g_free(path);
    }
    gtk_widget_destroy(dialog);
    return;
}

static void
on_scroll_sync(GtkAdjustment *s, gpointer d) {
    double v = gtk_adjustment_get_value(s);
    if (gtk_adjustment_get_value(GTK_ADJUSTMENT(d)) != v) {
        gtk_adjustment_set_value(GTK_ADJUSTMENT(d), v);
    }
    return;
}

static void
on_sort_changed(GtkTreeSortable *s, gpointer d) {
    int32 id;
    GtkSortType o;
    if (gtk_tree_sortable_get_sort_column_id(s, &id, &o)) {
        GtkTreeViewColumn *c = gtk_tree_view_get_column(GTK_TREE_VIEW(d), 1);
        if (c) {
            gtk_tree_view_column_set_sort_indicator(c, TRUE);
            gtk_tree_view_column_set_sort_order(c, o);
        }
    }
    return;
}

static gboolean
on_tree_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    GtkTreePath *path;
    GtkTreeIter iter;
    GtkTreeModel *model;
    GtkWidget *menu;
    GtkWidget *item;
    GtkWidget *sub;
    GtkWidget *sub_ext;
    GtkWidget *sub_dir;
    UIUpdateData *ud;
    char *f_path;
    char *action;
    char *other_path;
    AppWidgets *w;
    int32 side;
    int32 path_col;
    int32 act_col;
    int32 other_col;
    int32 is_disabled;

    w = (AppWidgets *)data;
    side = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "side"));

    if (event->type == GDK_BUTTON_PRESS && event->button == 3) {
        if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), (gint)event->x,
                                          (gint)event->y, &path, NULL, NULL,
                                          NULL)) {
            model = gtk_tree_view_get_model(GTK_TREE_VIEW(widget));
            if (gtk_tree_model_get_iter(model, &iter, path)) {
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

                item = gtk_menu_item_new_with_label("Apply (Selected items)");
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
        if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), (gint)event->x,
                                          (gint)event->y, &path, NULL, NULL,
                                          NULL)) {
            GtkTreeSelection *sel
                = gtk_tree_view_get_selection(GTK_TREE_VIEW(widget));
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
    GtkTreeModel *m;
    GtkTreeIter i;
    char *r;
    gint bx, by;
    (void)k;
    (void)d;
    gtk_tree_view_convert_widget_to_bin_window_coords(GTK_TREE_VIEW(w), x, y,
                                                      &bx, &by);
    if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(w), bx, by, &p, NULL, NULL,
                                      NULL)) {
        m = gtk_tree_view_get_model(GTK_TREE_VIEW(w));
        if (gtk_tree_model_get_iter(m, &i, p)) {
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
