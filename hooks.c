#include <gtk/gtk.h>
#include <stdlib.h>

#include "cecup.h"
#include "rsync.c"

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
    full = g_build_filename(base, ud->filepath, NULL);
    dir = g_path_get_dirname(full);
    cmd = g_strdup_printf("xdg-open '%s' &", dir);
    system(cmd);
    g_free(cmd);
    g_free(dir);
    g_free(full);
    g_free(ud->filepath);
    g_free(ud->action);
    g_free(ud);
    return;
}

static void
on_menu_apply(GtkWidget *m, gpointer data) {
    UIUpdateData *ud;

    (void)m;
    ud = (UIUpdateData *)data;
    g_thread_new("single_worker", single_sync_worker, ud);
    return;
}

static void
on_menu_diff(GtkWidget *m, gpointer data) {
    UIUpdateData *ud;
    char *base_s;
    char *base_d;
    char *full_s;
    char *full_d;
    char *cmd;

    (void)m;
    ud = (UIUpdateData *)data;
    base_s = (char *)gtk_entry_get_text(GTK_ENTRY(ud->widgets->src_entry));
    base_d = (char *)gtk_entry_get_text(GTK_ENTRY(ud->widgets->dst_entry));
    full_s = g_build_filename(base_s, ud->filepath, NULL);
    full_d = g_build_filename(base_d, ud->filepath, NULL);
    cmd = g_strdup_printf("meld '%s' '%s' &", full_s, full_d);
    system(cmd);
    g_free(cmd);
    g_free(full_s);
    g_free(full_d);
    g_free(ud->filepath);
    g_free(ud->action);
    g_free(ud);
    return;
}

static void
on_menu_exclude_ext(GtkWidget *m, gpointer data) {
    UIUpdateData *ud;
    char *ext;
    FILE *fp;

    (void)m;
    ud = (UIUpdateData *)data;
    ext = strrchr(ud->filepath, '.');
    if (ext) {
        fp = fopen(ud->widgets->exclude_path, "a");
        if (fp) {
            fprintf(fp, "\n*%s", ext);
            fclose(fp);
            dispatch_log(ud->widgets, "Pattern added to exclusions.");
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
    dir = g_path_get_dirname(ud->filepath);
    if (g_strcmp0(dir, ".") != 0) {
        fp = fopen(ud->widgets->exclude_path, "a");
        if (fp) {
            fprintf(fp, "\n/%s/", dir);
            fclose(fp);
            dispatch_log(ud->widgets, "Pattern added to exclusions.");
        }
    }
    g_free(dir);
    g_free(ud->filepath);
    g_free(ud->action);
    g_free(ud);
    return;
}

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
    dialog = gtk_message_dialog_new(
        GTK_WINDOW(w->gtk_window), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
        GTK_BUTTONS_YES_NO, "Confirm sync from %s to %s?", path_src, path_dst);
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
on_save_exclude(AppWidgets *w, GtkTextBuffer *buffer) {
    GtkTextIter start;
    GtkTextIter end;
    char *text;
    FILE *fp;

    gtk_text_buffer_get_start_iter(buffer, &start);
    gtk_text_buffer_get_end_iter(buffer, &end);
    text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    fp = fopen(w->exclude_path, "w");
    if (fp) {
        fputs(text, fp);
        fclose(fp);
    }
    g_free(text);
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

    w = (AppWidgets *)data;
    (void)b;
    dialog = gtk_dialog_new_with_buttons(
        "Edit Exclusions", GTK_WINDOW(w->gtk_window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "_Save",
        GTK_RESPONSE_ACCEPT, "_Close", GTK_RESPONSE_CLOSE, NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 400, 500);
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
        on_save_exclude(w, buffer);
    }
    gtk_widget_destroy(dialog);
    return;
}

static void
on_browse_src(GtkWidget *b, gpointer data) {
    AppWidgets *w;
    GtkWidget *gtk_file_chooser_dialog;
    char *path_src;

    w = (AppWidgets *)data;
    gtk_file_chooser_dialog = gtk_file_chooser_dialog_new(
        "Select Source Directory", GTK_WINDOW(w->gtk_window),
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "_Cancel", GTK_RESPONSE_CANCEL,
        "_Select", GTK_RESPONSE_ACCEPT, NULL);
    (void)b;
    if (gtk_dialog_run(GTK_DIALOG(gtk_file_chooser_dialog))
        == GTK_RESPONSE_ACCEPT) {
        path_src = gtk_file_chooser_get_filename(
            GTK_FILE_CHOOSER(gtk_file_chooser_dialog));
        gtk_entry_set_text(GTK_ENTRY(w->src_entry), path_src);
        g_free(path_src);
    }
    gtk_widget_destroy(gtk_file_chooser_dialog);
    return;
}

static void
on_browse_dst(GtkWidget *b, gpointer data) {
    AppWidgets *w;
    GtkWidget *gtk_file_chooser_dialog;
    char *path_dst;

    w = (AppWidgets *)data;
    gtk_file_chooser_dialog = gtk_file_chooser_dialog_new(
        "Select Destination Directory", GTK_WINDOW(w->gtk_window),
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "_Cancel", GTK_RESPONSE_CANCEL,
        "_Select", GTK_RESPONSE_ACCEPT, NULL);
    (void)b;
    if (gtk_dialog_run(GTK_DIALOG(gtk_file_chooser_dialog))
        == GTK_RESPONSE_ACCEPT) {
        path_dst = gtk_file_chooser_get_filename(
            GTK_FILE_CHOOSER(gtk_file_chooser_dialog));
        gtk_entry_set_text(GTK_ENTRY(w->dst_entry), path_dst);
        g_free(path_dst);
    }
    gtk_widget_destroy(gtk_file_chooser_dialog);
    return;
}

static void
on_scroll_sync(GtkAdjustment *src, gpointer data) {
    GtkAdjustment *target;
    double val;

    target = GTK_ADJUSTMENT(data);
    val = gtk_adjustment_get_value(src);
    if (gtk_adjustment_get_value(target) != val) {
        gtk_adjustment_set_value(target, val);
    }
    return;
}

static void
on_sort_changed(GtkTreeSortable *sortable, gpointer data) {
    GtkTreeView *target_view;
    GtkTreeViewColumn *col;
    int32 sort_column_id;
    GtkSortType order;

    target_view = GTK_TREE_VIEW(data);
    if (gtk_tree_sortable_get_sort_column_id(sortable, &sort_column_id,
                                             &order)) {
        col = gtk_tree_view_get_column(target_view, 1);
        if (col) {
            gtk_tree_view_column_set_sort_indicator(col, TRUE);
            gtk_tree_view_column_set_sort_order(col, order);
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
    UIUpdateData *ud;
    char *f_path;
    char *action;
    AppWidgets *w;
    int32 side;
    gint bin_x;
    gint bin_y;

    w = (AppWidgets *)data;
    side = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "side"));
    gtk_tree_view_convert_widget_to_bin_window_coords(
        GTK_TREE_VIEW(widget), (gint)event->x, (gint)event->y, &bin_x, &bin_y);

    if (event->type == GDK_BUTTON_PRESS && event->button == 3) {
        if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), bin_x, bin_y,
                                          &path, NULL, NULL, NULL)) {
            model = gtk_tree_view_get_model(GTK_TREE_VIEW(widget));
            if (gtk_tree_model_get_iter(model, &iter, path)) {
                gtk_tree_model_get(model, &iter, COL_SRC_PATH, &f_path,
                                   COL_SRC_ACTION, &action, -1);

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

                item = gtk_menu_item_new_with_label("Apply (This file only)");
                g_signal_connect(item, "activate", G_CALLBACK(on_menu_apply),
                                 ud);
                gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

                item = gtk_menu_item_new_with_label("Exclude via filter...");
                sub = gtk_menu_new();
                gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), sub);

                GtkWidget *sub_ext
                    = gtk_menu_item_new_with_label("By Extension");
                g_signal_connect(sub_ext, "activate",
                                 G_CALLBACK(on_menu_exclude_ext), ud);
                gtk_menu_shell_append(GTK_MENU_SHELL(sub), sub_ext);

                GtkWidget *sub_dir
                    = gtk_menu_item_new_with_label("By Directory");
                g_signal_connect(sub_dir, "activate",
                                 G_CALLBACK(on_menu_exclude_dir), ud);
                gtk_menu_shell_append(GTK_MENU_SHELL(sub), sub_dir);
                gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

                item = gtk_menu_item_new_with_label("Diff");
                if (g_strcmp0(f_path, "-") == 0 || g_strcmp0(action, "New") == 0
                    || g_strcmp0(action, "Deleted") == 0) {
                    gtk_widget_set_sensitive(item, FALSE);
                } else {
                    g_signal_connect(item, "activate", G_CALLBACK(on_menu_diff),
                                     ud);
                }
                gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

                gtk_widget_show_all(menu);
                gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);

                g_free(f_path);
                g_free(action);
            }
            gtk_tree_path_free(path);
            return TRUE;
        }
    } else if (event->type == GDK_BUTTON_PRESS && event->button == 1) {
        if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), bin_x, bin_y,
                                          &path, NULL, NULL, NULL)) {
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
on_tree_tooltip(GtkWidget *widget, gint x, gint y, gboolean keyboard_mode,
                GtkTooltip *tooltip, gpointer user_data) {
    GtkTreePath *gtk_tree_path;
    GtkTreeModel *model;
    GtkTreeIter iter;
    char *reason;
    gint bin_x;
    gint bin_y;

    (void)keyboard_mode;
    (void)user_data;

    gtk_tree_view_convert_widget_to_bin_window_coords(GTK_TREE_VIEW(widget), x,
                                                      y, &bin_x, &bin_y);

    if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), bin_x, bin_y,
                                      &gtk_tree_path, NULL, NULL, NULL)) {
        model = gtk_tree_view_get_model(GTK_TREE_VIEW(widget));
        if (gtk_tree_model_get_iter(model, &iter, gtk_tree_path)) {
            gtk_tree_model_get(model, &iter, COL_REASON, &reason, -1);
            if (reason && strlen(reason) > 0) {
                gtk_tooltip_set_text(tooltip, reason);
                g_free(reason);
                gtk_tree_path_free(gtk_tree_path);
                return TRUE;
            }
            g_free(reason);
        }
        gtk_tree_path_free(gtk_tree_path);
    }
    return FALSE;
}
