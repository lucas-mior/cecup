#include <gtk/gtk.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

#define SOURCE_DIR "/home/lucas/"
#define EXCLUDE_FILE "sync_gui.exclude"

typedef struct AppWidgets {
    GtkWidget *window;
    GtkWidget *dest_entry;
    GtkWidget *preview_btn;
    GtkWidget *sync_btn;
    GtkListStore *src_store;
    GtkListStore *dest_store;
    GtkTextBuffer *log_buffer;
} AppWidgets;

typedef struct ThreadData {
    AppWidgets *widgets;
    char dest_path[1024];
    int is_preview;
} ThreadData;

typedef struct UIUpdateData {
    AppWidgets *widgets;
    char *message;
    char *action;
    char *filepath;
    int side;  // 0 = Origin (Left), 1 = Destination (Right)
    int type;  // 0 = log, 1 = tree row, 2 = enable buttons, 3 = clear trees
} UIUpdateData;

static void
free_ui_update(UIUpdateData *data) {
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
    return;
}

gboolean
update_ui_handler(gpointer user_data) {
    UIUpdateData *data = (UIUpdateData *)user_data;

    if (data->type == 0) {
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(data->widgets->log_buffer, &end);
        gtk_text_buffer_insert(data->widgets->log_buffer, &end, data->message,
                               -1);
        gtk_text_buffer_insert(data->widgets->log_buffer, &end, "\n", -1);
    } else if (data->type == 1) {
        GtkListStore *target = (data->side == 0) ? data->widgets->src_store
                                                 : data->widgets->dest_store;
        GtkTreeIter iter;
        gtk_list_store_append(target, &iter);
        gtk_list_store_set(target, &iter, 0, data->action, 1, data->filepath,
                           -1);
    } else if (data->type == 2) {
        gtk_widget_set_sensitive(data->widgets->sync_btn, TRUE);
        gtk_widget_set_sensitive(data->widgets->preview_btn, TRUE);
    } else if (data->type == 3) {
        gtk_list_store_clear(data->widgets->src_store);
        gtk_list_store_clear(data->widgets->dest_store);
    }

    free_ui_update(data);
    return G_SOURCE_REMOVE;
}

/* --- Dispatchers from Worker Thread --- */

static void
dispatch_log(AppWidgets *w, const char *msg) {
    UIUpdateData *data = g_new0(UIUpdateData, 1);
    data->widgets = w;
    data->type = 0;
    data->message = g_strdup(msg);
    g_idle_add(update_ui_handler, data);
    return;
}

static void
dispatch_tree(AppWidgets *w, int side, const char *act, const char *path) {
    UIUpdateData *data = g_new0(UIUpdateData, 1);
    data->widgets = w;
    data->type = 1;
    data->side = side;
    data->action = g_strdup(act);
    data->filepath = g_strdup(path);
    g_idle_add(update_ui_handler, data);
    return;
}

/* --- Worker Thread Logic --- */

gpointer
sync_worker(gpointer user_data) {
    ThreadData *tdata = (ThreadData *)user_data;
    char cmd[4096], buffer[2048];
    FILE *fp;

    if (tdata->is_preview) {
        UIUpdateData *clear = g_new0(UIUpdateData, 1);
        clear->widgets = tdata->widgets;
        clear->type = 3;
        g_idle_add(update_ui_handler, clear);
    }

    // Handle Ownership and Snapshotting if real Sync
    /* if (!tdata->is_preview) { */
    /*     dispatch_log(tdata->widgets, ">>> Initiating Pre-sync (Ownership &
     * Snapshot)..."); */
    /*     // Using pkexec for privileged operations */
    /*     snprintf(cmd, sizeof(cmd), */
    /*         "pkexec sh -c \"chown -R lucas:lucas %s %s && " */
    /*         "mkdir -p %s/../snapshots && " */
    /*         "btrfs subvolume snapshot %s/.. %s/../snapshots/$(date
     * +%%Y%%m%%d_%%H%%M%%S)\"", */
    /*         SOURCE_DIR, tdata->dest_path, tdata->dest_path, tdata->dest_path,
     * tdata->dest_path); */
    /*     system(cmd); */
    /* } */

    snprintf(
        cmd, sizeof(cmd),
        "rsync --verbose --update --recursive --partial --links --hard-links "
        "--itemize-changes --perms --times --owner --group --delete "
        "--delete-after "
        "--delete-excluded --stats %s %s %s '%s' '%s/' 2>&1",
        tdata->is_preview ? "--dry-run" : "--info=progress2",
        access(EXCLUDE_FILE, F_OK) != -1 ? "--exclude-from=" EXCLUDE_FILE : "",
        tdata->is_preview ? "" : "| tee /tmp/rsyncfiles", SOURCE_DIR,
        tdata->dest_path);

    fp = popen(cmd, "r");
    if (fp) {
        while (fgets(buffer, sizeof(buffer), fp)) {
            buffer[strcspn(buffer, "\n")] = 0;
            if (tdata->is_preview) {
                if (strncmp(buffer, "*deleting", 9) == 0) {
                    dispatch_tree(tdata->widgets, 1, "Delete", buffer + 10);
                } else if (strncmp(buffer, ">f", 2) == 0
                           || strncmp(buffer, ">c", 2) == 0) {
                    char *space = strchr(buffer, ' ');
                    if (space) {
                        const char *act = (strncmp(buffer, ">f+++++", 7) == 0)
                                              ? "New"
                                              : "Update";
                        dispatch_tree(tdata->widgets, 0, act, space + 1);
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
                 "rsync --verbose --checksum --files-from=/tmp/sync.files %s "
                 "%s/ 2>&1",
                 SOURCE_DIR, tdata->dest_path);
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
    ready->type = 2;
    g_idle_add(update_ui_handler, ready);

    g_free(tdata);
    return NULL;
}

static void
on_preview_clicked(GtkWidget *b, gpointer data) {
    AppWidgets *w = (AppWidgets *)data;
    const char *dest = gtk_entry_get_text(GTK_ENTRY(w->dest_entry));
    (void)b;

    if (strlen(dest) < 1) {
        return;
    }

    gtk_widget_set_sensitive(w->preview_btn, FALSE);
    gtk_widget_set_sensitive(w->sync_btn, FALSE);

    ThreadData *td = g_new0(ThreadData, 1);
    td->widgets = w;
    td->is_preview = 1;
    strncpy(td->dest_path, dest, 1023);

    g_thread_new("worker", sync_worker, td);
    return;
}

static void
on_sync_clicked(GtkWidget *b, gpointer data) {
    AppWidgets *w = (AppWidgets *)data;
    const char *dest = gtk_entry_get_text(GTK_ENTRY(w->dest_entry));
    (void)b;

    GtkWidget *dialog = gtk_message_dialog_new(
        GTK_WINDOW(w->window), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
        GTK_BUTTONS_YES_NO, "Confirm write to: %s?", dest);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES) {
        gtk_widget_set_sensitive(w->preview_btn, FALSE);
        gtk_widget_set_sensitive(w->sync_btn, FALSE);
        ThreadData *td = g_new0(ThreadData, 1);
        td->widgets = w;
        td->is_preview = 0;
        strncpy(td->dest_path, dest, 1023);
        g_thread_new("worker", sync_worker, td);
    }
    gtk_widget_destroy(dialog);
    return;
}

static void
on_browse(GtkWidget *b, gpointer data) {
    AppWidgets *w = (AppWidgets *)data;
    GtkWidget *dlg = gtk_file_chooser_dialog_new(
        "Select Destination", GTK_WINDOW(w->window),
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
    gtk_tree_view_append_column(
        GTK_TREE_VIEW(tree),
        gtk_tree_view_column_new_with_attributes("Action", r, "text", 0, NULL));
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree),
                                gtk_tree_view_column_new_with_attributes(
                                    "File Path", r, "text", 1, NULL));
    return;
}

int
main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    AppWidgets *w = g_new0(AppWidgets, 1);
    w->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(w->window), "Btrfs Rsync Sync GUI");
    gtk_window_set_default_size(GTK_WINDOW(w->window), 1100, 750);
    g_signal_connect(w->window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *main_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(w->window), main_vbox);

    // Header Area
    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(header), 10);
    w->dest_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(w->dest_entry),
                                   "Select Destination...");
    GtkWidget *browse = gtk_button_new_with_label("Browse");
    w->preview_btn = gtk_button_new_with_label("1. Preview");
    w->sync_btn = gtk_button_new_with_label("2. Sync");
    gtk_widget_set_sensitive(w->sync_btn, FALSE);

    gtk_box_pack_start(GTK_BOX(header), gtk_label_new("Source: " SOURCE_DIR),
                       FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(header), w->dest_entry, TRUE, TRUE, 5);
    gtk_box_pack_start(GTK_BOX(header), browse, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(header), w->preview_btn, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(header), w->sync_btn, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(main_vbox), header, FALSE, FALSE, 0);

    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(main_vbox), paned, TRUE, TRUE, 0);

    GtkWidget *l_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_box_pack_start(GTK_BOX(l_vbox),
                       gtk_label_new("Origin: To be Transferred"), FALSE, FALSE,
                       0);
    GtkWidget *l_scroll = gtk_scrolled_window_new(NULL, NULL);
    w->src_store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
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
    w->dest_store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
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

    g_signal_connect(browse, "clicked", G_CALLBACK(on_browse), w);
    g_signal_connect(w->preview_btn, "clicked", G_CALLBACK(on_preview_clicked),
                     w);
    g_signal_connect(w->sync_btn, "clicked", G_CALLBACK(on_sync_clicked), w);

    gtk_widget_show_all(w->window);
    gtk_main();

    g_free(w);
    return 0;
}
