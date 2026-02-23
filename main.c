#include <gtk/gtk.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

#include "util.c"

#define EXCLUDE_FILE "sync_gui.exclude"

enum {
    COL_ACTION = 0,
    COL_PATH,
    COL_SIZE_TEXT,
    COL_SIZE_RAW,
    COL_COLOR,
    NUM_COLS
};

typedef struct AppWidgets {
    GtkWidget *window;
    GtkWidget *src_entry;
    GtkWidget *dest_entry;
    GtkWidget *preview_btn;
    GtkWidget *sync_btn;
    GtkListStore *src_store;
    GtkListStore *dest_store;
    GtkTextBuffer *log_buffer;
} AppWidgets;

typedef struct ThreadData {
    AppWidgets *widgets;
    char src_path[1024];
    char dest_path[1024];
    int32 is_preview;
} ThreadData;

enum DataType {
    DATA_TYPE_LOG,
    DATA_TYPE_TREE_ROW,
    DATA_TYPE_ENABLE_BUTTONS,
    DATA_TYPE_CLEAR_TREES,
};

typedef struct UIUpdateData {
    AppWidgets *widgets;
    char *message;
    char *action;
    char *filepath;
    int64 size;
    int32 side;
    enum DataType type;
} UIUpdateData;

static char *
format_size(int64 bytes) {
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    int32 i = 0;
    double d_bytes = (double)bytes;
    while (d_bytes >= 1024 && i < LENGTH(units)) {
        d_bytes /= 1024;
        i++;
    }
    return g_strdup_printf("%.2f %s", d_bytes, units[i]);
}

gboolean
update_ui_handler(gpointer user_data) {
    UIUpdateData *data = (UIUpdateData *)user_data;

    switch (data->type) {
    case DATA_TYPE_LOG: {
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(data->widgets->log_buffer, &end);
        gtk_text_buffer_insert(data->widgets->log_buffer, &end, data->message,
                               -1);
        gtk_text_buffer_insert(data->widgets->log_buffer, &end, "\n", -1);
        break;
    }
    case DATA_TYPE_TREE_ROW:
        GtkListStore *target = (data->side == 0) ? data->widgets->src_store
                                                 : data->widgets->dest_store;
        GtkTreeIter iter;
        char *size_str = format_size(data->size);

        const char *bg_color = "#FFFFFF";  // Default White
        if (g_strcmp0(data->action, "New") == 0) {
            bg_color = "#D4EDDA";  // Light Green
        }
        if (g_strcmp0(data->action, "Update") == 0) {
            bg_color = "#CCE5FF";  // Light Blue
        }
        if (g_strcmp0(data->action, "Delete") == 0) {
            bg_color = "#F8D7DA";  // Light Red
        }

        gtk_list_store_append(target, &iter);
        gtk_list_store_set(target, &iter, COL_ACTION, data->action, COL_PATH,
                           data->filepath, COL_SIZE_TEXT, size_str,
                           COL_SIZE_RAW, data->size, COL_COLOR,
                           bg_color,  // Set the color column
                           -1);
        g_free(size_str);
        break;
    case DATA_TYPE_ENABLE_BUTTONS:
        gtk_widget_set_sensitive(data->widgets->sync_btn, TRUE);
        gtk_widget_set_sensitive(data->widgets->preview_btn, TRUE);
        break;
    case DATA_TYPE_CLEAR_TREES:
        gtk_list_store_clear(data->widgets->src_store);
        gtk_list_store_clear(data->widgets->dest_store);
        break;
    }

    if (data->message) {
        g_free(data->message);
    }
    if (data->action) {
        g_free(data->action);
    }
    if (data->filepath) {
        g_free(data->filepath);
    }
    g_free(data);
    return G_SOURCE_REMOVE;
}

static void
dispatch_log(AppWidgets *w, const char *msg) {
    UIUpdateData *data = g_new0(UIUpdateData, 1);
    data->widgets = w;
    data->type = DATA_TYPE_LOG;
    data->message = g_strdup(msg);
    g_idle_add(update_ui_handler, data);
    return;
}

static void
dispatch_tree(AppWidgets *w, int32 side, const char *act, const char *path,
              int64 size) {
    UIUpdateData *data = g_new0(UIUpdateData, 1);
    data->widgets = w;
    data->type = DATA_TYPE_TREE_ROW;
    data->side = side;
    data->action = g_strdup(act);
    data->filepath = g_strdup(path);
    data->size = size;
    g_idle_add(update_ui_handler, data);
    return;
}

gpointer
sync_worker(gpointer user_data) {
    ThreadData *tdata = (ThreadData *)user_data;
    char cmd[4096];
    char buffer[2048];
    FILE *fp;

    if (tdata->is_preview) {
        UIUpdateData *clear = g_new0(UIUpdateData, 1);
        clear->widgets = tdata->widgets;
        clear->type = DATA_TYPE_CLEAR_TREES;
        g_idle_add(update_ui_handler, clear);
    }

    snprintf(
        cmd, sizeof(cmd),
        "rsync --verbose --update --recursive --partial --links --hard-links "
        "--itemize-changes --perms --times --owner --group --delete "
        "--delete-after "
        "--delete-excluded --stats %s %s %s '%s/' '%s/' 2>&1",
        tdata->is_preview ? "--dry-run" : "--info=progress2",
        access(EXCLUDE_FILE, F_OK) != -1 ? "--exclude-from=" EXCLUDE_FILE : "",
        tdata->is_preview ? "" : "| tee /tmp/rsyncfiles", tdata->src_path,
        tdata->dest_path);

    fp = popen(cmd, "r");
    if (fp) {
        while (fgets(buffer, sizeof(buffer), fp)) {
            buffer[strcspn(buffer, "\n")] = 0;
            if (tdata->is_preview) {
                if (strncmp(buffer, "*deleting", 9) == 0) {
                    char full_path[2048];
                    struct stat st;
                    snprintf(full_path, sizeof(full_path), "%s/%s",
                             tdata->dest_path, buffer + 10);
                    int64 sz = (stat(full_path, &st) == 0) ? st.st_size : 0;
                    dispatch_tree(tdata->widgets, 1, "Delete", buffer + 10, sz);
                } else if (strncmp(buffer, ">f", 2) == 0
                           || strncmp(buffer, ">c", 2) == 0) {
                    char *space = strchr(buffer, ' ');
                    if (space) {
                        const char *act = (strncmp(buffer, ">f+++++", 7) == 0)
                                              ? "New"
                                              : "Update";
                        char full_path[2048];
                        struct stat st;
                        snprintf(full_path, sizeof(full_path), "%s/%s",
                                 tdata->src_path, space + 1);
                        int64 sz = (stat(full_path, &st) == 0) ? st.st_size : 0;
                        dispatch_tree(tdata->widgets, 0, act, space + 1, sz);
                    }
                }
            } else {
                dispatch_log(tdata->widgets, buffer);
            }
        }
        pclose(fp);
    }

    if (!tdata->is_preview) {
        dispatch_log(tdata->widgets, ">>> Starting Checksum Verification...");
        system("sed -nE '/^[>ch]f/{s/^[^ ]+ //; p}' /tmp/rsyncfiles > "
               "/tmp/sync.files");
        snprintf(cmd, sizeof(cmd),
                 "rsync --verbose --checksum --files-from=/tmp/sync.files "
                 "'%s/' '%s/' 2>&1",
                 tdata->src_path, tdata->dest_path);
        fp = popen(cmd, "r");
        if (fp) {
            while (fgets(buffer, sizeof(buffer), fp)) {
                buffer[strcspn(buffer, "\n")] = 0;
                dispatch_log(tdata->widgets, buffer);
            }
            pclose(fp);
        }
    }

    dispatch_log(tdata->widgets, ">>> Finished.");
    UIUpdateData *ready = g_new0(UIUpdateData, 1);
    ready->widgets = tdata->widgets;
    ready->type = DATA_TYPE_ENABLE_BUTTONS;
    g_idle_add(update_ui_handler, ready);

    g_free(tdata);
    return NULL;
}

static void
on_preview_clicked(GtkWidget *b, gpointer data) {
    AppWidgets *w = (AppWidgets *)data;
    char *src = gtk_entry_get_text(GTK_ENTRY(w->src_entry));
    char *dest = gtk_entry_get_text(GTK_ENTRY(w->dest_entry));
    (void)b;
    if (strlen64(src) < 1 || strlen64(dest) < 1) {
        return;
    }
    gtk_widget_set_sensitive(w->preview_btn, FALSE);
    gtk_widget_set_sensitive(w->sync_btn, FALSE);
    ThreadData *td = g_new0(ThreadData, 1);
    td->widgets = w;
    td->is_preview = 1;
    strncpy(td->src_path, src, 1023);
    strncpy(td->dest_path, dest, 1023);
    g_thread_new("worker", sync_worker, td);
    return;
}

static void
on_sync_clicked(GtkWidget *b, gpointer data) {
    AppWidgets *w = (AppWidgets *)data;
    const char *src = gtk_entry_get_text(GTK_ENTRY(w->src_entry));
    const char *dest = gtk_entry_get_text(GTK_ENTRY(w->dest_entry));
    (void)b;
    GtkWidget *dialog = gtk_message_dialog_new(
        GTK_WINDOW(w->window), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
        GTK_BUTTONS_YES_NO, "Confirm sync from %s to %s?", src, dest);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES) {
        gtk_widget_set_sensitive(w->preview_btn, FALSE);
        gtk_widget_set_sensitive(w->sync_btn, FALSE);
        ThreadData *td = g_new0(ThreadData, 1);
        td->widgets = w;
        td->is_preview = 0;
        strncpy(td->src_path, src, 1023);
        strncpy(td->dest_path, dest, 1023);
        g_thread_new("worker", sync_worker, td);
    }
    gtk_widget_destroy(dialog);
    return;
}

static void
on_browse_src(GtkWidget *b, gpointer data) {
    AppWidgets *w = (AppWidgets *)data;
    GtkWidget *dlg = gtk_file_chooser_dialog_new(
        "Select Source Directory", GTK_WINDOW(w->window),
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "_Cancel", GTK_RESPONSE_CANCEL,
        "_Select", GTK_RESPONSE_ACCEPT, NULL);
    (void)b;
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        gtk_entry_set_text(GTK_ENTRY(w->src_entry), path);
        g_free(path);
    }
    gtk_widget_destroy(dlg);
    return;
}

static void
on_browse_dest(GtkWidget *b, gpointer data) {
    AppWidgets *w = (AppWidgets *)data;
    GtkWidget *dlg = gtk_file_chooser_dialog_new(
        "Select Destination Directory", GTK_WINDOW(w->window),
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "_Cancel", GTK_RESPONSE_CANCEL,
        "_Select", GTK_RESPONSE_ACCEPT, NULL);
    (void)b;
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        gtk_entry_set_text(GTK_ENTRY(w->dest_entry), path);
        g_free(path);
    }
    gtk_widget_destroy(dlg);
    return;
}

static void
setup_tree_columns(GtkWidget *tree) {
    GtkCellRenderer *r = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *col;

    // Link "cell-background" property of the renderer to COL_COLOR (Index 4)
    col = gtk_tree_view_column_new_with_attributes(
        "Action", r, "text", COL_ACTION, "cell-background", COL_COLOR, NULL);
    gtk_tree_view_column_set_sort_column_id(col, COL_ACTION);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), col);

    col = gtk_tree_view_column_new_with_attributes(
        "File Path", r, "text", COL_PATH, "cell-background", COL_COLOR, NULL);
    gtk_tree_view_column_set_sort_column_id(col, COL_PATH);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), col);

    col = gtk_tree_view_column_new_with_attributes(
        "Size", r, "text", COL_SIZE_TEXT, "cell-background", COL_COLOR, NULL);
    gtk_tree_view_column_set_sort_column_id(col, COL_SIZE_RAW);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), col);

    return;
}

int32
main(int32 argc, char *argv[]) {
    gtk_init(&argc, &argv);

    AppWidgets *w = g_new0(AppWidgets, 1);
    w->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(w->window), "Btrfs Rsync Sync GUI");
    gtk_window_set_default_size(GTK_WINDOW(w->window), 1100, 800);
    g_signal_connect(w->window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *main_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(w->window), main_vbox);

    GtkWidget *header_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(header_vbox), 10);

    char *cwd = g_get_current_dir();
    char *default_src = g_strdup_printf("%s/a/", cwd);
    char *default_dest = g_strdup_printf("%s/b/", cwd);

    GtkWidget *src_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(src_hbox), gtk_label_new("Source:     "), FALSE,
                       FALSE, 5);
    w->src_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(w->src_entry), default_src);
    GtkWidget *browse_src = gtk_button_new_with_label("Browse");
    gtk_box_pack_start(GTK_BOX(src_hbox), w->src_entry, TRUE, TRUE, 5);
    gtk_box_pack_start(GTK_BOX(src_hbox), browse_src, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(header_vbox), src_hbox, FALSE, FALSE, 0);

    GtkWidget *dest_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(dest_hbox), gtk_label_new("Destination:"), FALSE,
                       FALSE, 5);
    w->dest_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(w->dest_entry), default_dest);
    GtkWidget *browse_dest = gtk_button_new_with_label("Browse");
    gtk_box_pack_start(GTK_BOX(dest_hbox), w->dest_entry, TRUE, TRUE, 5);
    gtk_box_pack_start(GTK_BOX(dest_hbox), browse_dest, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(header_vbox), dest_hbox, FALSE, FALSE, 0);

    g_free(cwd);
    g_free(default_src);
    g_free(default_dest);

    GtkWidget *btn_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    w->preview_btn = gtk_button_new_with_label("1. Preview");
    w->sync_btn = gtk_button_new_with_label("2. Sync");
    gtk_widget_set_sensitive(w->sync_btn, FALSE);
    gtk_box_pack_end(GTK_BOX(btn_hbox), w->sync_btn, FALSE, FALSE, 5);
    gtk_box_pack_end(GTK_BOX(btn_hbox), w->preview_btn, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(header_vbox), btn_hbox, FALSE, FALSE, 5);

    gtk_box_pack_start(GTK_BOX(main_vbox), header_vbox, FALSE, FALSE, 0);

    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(main_vbox), paned, TRUE, TRUE, 0);

    GtkWidget *l_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_box_pack_start(GTK_BOX(l_vbox),
                       gtk_label_new("Origin: To be Transferred"), FALSE, FALSE,
                       0);
    GtkWidget *l_scroll = gtk_scrolled_window_new(NULL, NULL);
    // Updated Store with 5 columns
    w->src_store
        = gtk_list_store_new(NUM_COLS, G_TYPE_STRING, G_TYPE_STRING,
                             G_TYPE_STRING, G_TYPE_INT64, G_TYPE_STRING);
    GtkWidget *l_tree
        = gtk_tree_view_new_with_model(GTK_TREE_MODEL(w->src_store));
    setup_tree_columns(l_tree);
    gtk_container_add(GTK_CONTAINER(l_scroll), l_tree);
    gtk_box_pack_start(GTK_BOX(l_vbox), l_scroll, TRUE, TRUE, 0);
    gtk_paned_pack1(GTK_PANED(paned), l_vbox, TRUE, FALSE);

    GtkWidget *r_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_box_pack_start(GTK_BOX(r_vbox),
                       gtk_label_new("Destination: To be Deleted"), FALSE,
                       FALSE, 0);
    GtkWidget *r_scroll = gtk_scrolled_window_new(NULL, NULL);
    // Updated Store with 5 columns
    w->dest_store
        = gtk_list_store_new(NUM_COLS, G_TYPE_STRING, G_TYPE_STRING,
                             G_TYPE_STRING, G_TYPE_INT64, G_TYPE_STRING);
    GtkWidget *r_tree
        = gtk_tree_view_new_with_model(GTK_TREE_MODEL(w->dest_store));
    setup_tree_columns(r_tree);
    gtk_container_add(GTK_CONTAINER(r_scroll), r_tree);
    gtk_box_pack_start(GTK_BOX(r_vbox), r_scroll, TRUE, TRUE, 0);
    gtk_paned_pack2(GTK_PANED(paned), r_vbox, TRUE, FALSE);

    GtkWidget *log_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(log_scroll, -1, 150);
    GtkWidget *log_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(log_view), FALSE);
    w->log_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(log_view));
    gtk_container_add(GTK_CONTAINER(log_scroll), log_view);
    gtk_box_pack_start(GTK_BOX(main_vbox), log_scroll, FALSE, FALSE, 5);

    g_signal_connect(browse_src, "clicked", G_CALLBACK(on_browse_src), w);
    g_signal_connect(browse_dest, "clicked", G_CALLBACK(on_browse_dest), w);
    g_signal_connect(w->preview_btn, "clicked", G_CALLBACK(on_preview_clicked),
                     w);
    g_signal_connect(w->sync_btn, "clicked", G_CALLBACK(on_sync_clicked), w);

    gtk_widget_show_all(w->window);
    gtk_main();

    g_free(w);
    return 0;
}
