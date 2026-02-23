#include <gtk/gtk.h>

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
    GtkTreeSortable *target;
    int sort_column_id;
    GtkSortType order;
    int target_id;
    GtkSortType target_order;

    target = GTK_TREE_SORTABLE(data);
    if (gtk_tree_sortable_get_sort_column_id(sortable, &sort_column_id,
                                             &order)) {
        if (gtk_tree_sortable_get_sort_column_id(target, &target_id,
                                                 &target_order)) {
            if (target_id == sort_column_id && target_order == order) {
                return;
            }
        }
        gtk_tree_sortable_set_sort_column_id(target, sort_column_id, order);
    }
    return;
}

static gboolean
on_tree_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    GtkTreePath *gtk_tree_path;
    GtkTreeSelection *gtk_tree_selection;

    (void)data;
    if (event->type == GDK_BUTTON_PRESS && event->button == 1) {
        if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), (gint)event->x,
                                          (gint)event->y, &gtk_tree_path, NULL,
                                          NULL, NULL)) {
            gtk_tree_selection
                = gtk_tree_view_get_selection(GTK_TREE_VIEW(widget));
            if (gtk_tree_selection_path_is_selected(gtk_tree_selection,
                                                    gtk_tree_path)) {
                gtk_tree_selection_unselect_path(gtk_tree_selection,
                                                 gtk_tree_path);
                gtk_tree_path_free(gtk_tree_path);
                return TRUE;
            }
            gtk_tree_path_free(gtk_tree_path);
        }
    }
    return FALSE;
}

static gboolean
on_tree_tooltip(GtkWidget *widget, gint x, gint y, gboolean keyboard_mode,
                GtkTooltip *tooltip, gpointer user_data) {
    GtkTreePath *gtk_tree_path;
    GtkTreeViewColumn *gtk_tree_view_column;
    GtkTreeModel *model;
    GtkTreeIter iter;
    char *reason;

    (void)keyboard_mode;
    (void)user_data;
    if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), x, y,
                                      &gtk_tree_path, &gtk_tree_view_column,
                                      NULL, NULL)) {
        model = gtk_tree_view_get_model(GTK_TREE_VIEW(widget));
        reason = NULL;
        if (gtk_tree_model_get_iter(model, &iter, gtk_tree_path)) {
            gtk_tree_model_get(model, &iter, COL_REASON, &reason, -1);
            if (reason && strlen(reason) > 0) {
                gtk_tooltip_set_text(tooltip, reason);
                gtk_tree_path_free(gtk_tree_path);
                g_free(reason);
                return TRUE;
            }
            g_free(reason);
        }
        gtk_tree_path_free(gtk_tree_path);
    }
    return FALSE;
}
