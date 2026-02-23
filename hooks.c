#include <gtk/gtk.h>

#include "cecup.h"
#include "rsync.c"

static void
on_preview_clicked(GtkWidget *b, gpointer data) {
    AppWidgets *w = (AppWidgets *)data;
    char *src = gtk_entry_get_text(GTK_ENTRY(w->src_entry));
    char *dst = gtk_entry_get_text(GTK_ENTRY(w->dst_entry));
    ThreadData *thread_data;

    (void)b;
    if (strlen64(src) < 1 || strlen64(dst) < 1) {
        return;
    }
    gtk_widget_set_sensitive(w->preview_button, FALSE);
    gtk_widget_set_sensitive(w->sync_button, FALSE);
    thread_data = g_new0(ThreadData, 1);
    thread_data->widgets = w;
    thread_data->is_preview = 1;
    strncpy(thread_data->src_path, src, 1023);
    strncpy(thread_data->dst_path, dst, 1023);
    g_thread_new("worker", sync_worker, thread_data);
    return;
}

static void
on_sync_clicked(GtkWidget *b, gpointer data) {
    AppWidgets *w = (AppWidgets *)data;
    char *src = gtk_entry_get_text(GTK_ENTRY(w->src_entry));
    char *dst = gtk_entry_get_text(GTK_ENTRY(w->dst_entry));
    GtkWidget *dialog;
    ThreadData *thread_data;

    (void)b;
    dialog = gtk_message_dialog_new(GTK_WINDOW(w->gtk_window), GTK_DIALOG_MODAL,
                                    GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
                                    "Confirm sync from %s to %s?", src, dst);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES) {
        gtk_widget_set_sensitive(w->preview_button, FALSE);
        gtk_widget_set_sensitive(w->sync_button, FALSE);
        thread_data = g_new0(ThreadData, 1);
        thread_data->widgets = w;
        thread_data->is_preview = 0;
        strncpy(thread_data->src_path, src, 1023);
        strncpy(thread_data->dst_path, dst, 1023);
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
    AppWidgets *w = (AppWidgets *)data;
    GtkWidget *dialog;
    GtkWidget *scroll;
    GtkWidget *view;
    GtkTextBuffer *buffer;
    char *content;
    gsize length;

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
    AppWidgets *w = (AppWidgets *)data;
    GtkWidget *gtk_file_chooser_dialog = gtk_file_chooser_dialog_new(
        "Select Source Directory", GTK_WINDOW(w->gtk_window),
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "_Cancel", GTK_RESPONSE_CANCEL,
        "_Select", GTK_RESPONSE_ACCEPT, NULL);

    (void)b;
    if (gtk_dialog_run(GTK_DIALOG(gtk_file_chooser_dialog))
        == GTK_RESPONSE_ACCEPT) {
        char *path = gtk_file_chooser_get_filename(
            GTK_FILE_CHOOSER(gtk_file_chooser_dialog));
        gtk_entry_set_text(GTK_ENTRY(w->src_entry), path);
        g_free(path);
    }
    gtk_widget_destroy(gtk_file_chooser_dialog);
    return;
}

static void
on_browse_dst(GtkWidget *b, gpointer data) {
    AppWidgets *w = (AppWidgets *)data;
    GtkWidget *gtk_file_chooser_dialog = gtk_file_chooser_dialog_new(
        "Select Destination Directory", GTK_WINDOW(w->gtk_window),
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "_Cancel", GTK_RESPONSE_CANCEL,
        "_Select", GTK_RESPONSE_ACCEPT, NULL);

    (void)b;
    if (gtk_dialog_run(GTK_DIALOG(gtk_file_chooser_dialog))
        == GTK_RESPONSE_ACCEPT) {
        char *path = gtk_file_chooser_get_filename(
            GTK_FILE_CHOOSER(gtk_file_chooser_dialog));
        gtk_entry_set_text(GTK_ENTRY(w->dst_entry), path);
        g_free(path);
    }
    gtk_widget_destroy(gtk_file_chooser_dialog);
    return;
}
