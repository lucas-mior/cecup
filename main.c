#include <gtk/gtk.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

#define SOURCE_DIR "/home/lucas/"
#define EXCLUDE_FILE "sync_gui.exclude"

typedef struct {
    GtkWidget *dest_entry;
    GtkWidget *preview_btn;
    GtkWidget *sync_btn;
    GtkWidget *tree_view;
    GtkListStore *list_store;
    GtkTextBuffer *log_buffer;
} AppWidgets;

typedef struct {
    AppWidgets *widgets;
    char dest_path[1024];
    int is_preview;
} ThreadData;

typedef struct {
    AppWidgets *widgets;
    char *message;
    char *action;
    char *filepath;
    int type; // 0 = log, 1 = tree row, 2 = enable sync button, 3 = clear tree
} UIUpdateData;

/* --- Utility / Memory Management --- */

void free_ui_update(UIUpdateData *data) {
    if (data->message) g_free(data->message);
    if (data->action) g_free(data->action);
    if (data->filepath) g_free(data->filepath);
    g_free(data);
    return;
}

/* --- UI Thread Updaters (Called via g_idle_add) --- */

gboolean update_ui_callback(gpointer user_data) {
    UIUpdateData *data = (UIUpdateData *)user_data;

    if (data->type == 0) {
        // Append to log
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(data->widgets->log_buffer, &end);
        gtk_text_buffer_insert(data->widgets->log_buffer, &end, data->message, -1);
        gtk_text_buffer_insert(data->widgets->log_buffer, &end, "\n", -1);
    } 
    else if (data->type == 1) {
        // Add to tree view
        GtkTreeIter iter;
        gtk_list_store_append(data->widgets->list_store, &iter);
        gtk_list_store_set(data->widgets->list_store, &iter, 
                           0, data->action, 
                           1, data->filepath, 
                           -1);
    }
    else if (data->type == 2) {
        // Enable sync button
        gtk_widget_set_sensitive(data->widgets->sync_btn, TRUE);
        gtk_widget_set_sensitive(data->widgets->preview_btn, TRUE);
    }
    else if (data->type == 3) {
        // Clear tree
        gtk_list_store_clear(data->widgets->list_store);
    }

    free_ui_update(data);
    return G_SOURCE_REMOVE;
}

void dispatch_log(AppWidgets *widgets, const char *msg) {
    UIUpdateData *data = g_new0(UIUpdateData, 1);
    data->widgets = widgets;
    data->type = 0;
    data->message = g_strdup(msg);
    g_idle_add(update_ui_callback, data);
    return;
}

void dispatch_tree_row(AppWidgets *widgets, const char *action, const char *filepath) {
    UIUpdateData *data = g_new0(UIUpdateData, 1);
    data->widgets = widgets;
    data->type = 1;
    data->action = g_strdup(action);
    data->filepath = g_strdup(filepath);
    g_idle_add(update_ui_callback, data);
    return;
}

void dispatch_state(AppWidgets *widgets, int state_type) {
    UIUpdateData *data = g_new0(UIUpdateData, 1);
    data->widgets = widgets;
    data->type = state_type;
    g_idle_add(update_ui_callback, data);
    return;
}

/* --- Worker Thread --- */

gpointer worker_thread(gpointer user_data) {
    ThreadData *tdata = (ThreadData *)user_data;
    char cmd[2048];
    char buffer[1024];
    FILE *fp;

    dispatch_log(tdata->widgets, "=== Starting Operation ===");

    // Build the rsync command
    snprintf(cmd, sizeof(cmd), 
        "rsync --verbose --update --recursive --partial --links --hard-links "
        "--itemize-changes --perms --times --owner --group --delete --delete-after "
        "--delete-excluded --stats %s %s %s '%s' '%s/' 2>&1",
        tdata->is_preview ? "--dry-run" : "--info=progress2",
        access(EXCLUDE_FILE, F_OK) != -1 ? "--exclude-from=" EXCLUDE_FILE : "",
        tdata->is_preview ? "" : "| tee /tmp/rsyncfiles", // Mirroring your bash tee
        SOURCE_DIR, tdata->dest_path);

    // If it's not a preview, do the Bash script's pre-sync steps
    if (!tdata->is_preview) {
        dispatch_log(tdata->widgets, "[*] Running BTRFS Snapshot...");
        char snap_cmd[1024];
        time_t t = time(NULL);
        struct tm tm = *localtime(&t);
        snprintf(snap_cmd, sizeof(snap_cmd), 
            "mkdir -p '%s/../snapshots/' && btrfs subvolume snapshot '%s/..' '/%s/../snapshots/%d%02d%02d_%02d%02d%02d'",
            tdata->dest_path, tdata->dest_path, tdata->dest_path,
            tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
        
        system(snap_cmd); // Executing synchronously in background thread
    }

    dispatch_log(tdata->widgets, "[*] Running Rsync...");
    
    fp = popen(cmd, "r");
    if (fp == NULL) {
        dispatch_log(tdata->widgets, "Error: Failed to run rsync command.");
        g_free(tdata);
        return NULL; // Threads returning gpointer, so returning NULL is correct
    }

    // Read output line by line
    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
        buffer[strcspn(buffer, "\n")] = 0; // Remove newline

        if (tdata->is_preview) {
            // Parse itemize-changes to populate the table
            if (strncmp(buffer, "*deleting", 9) == 0) {
                dispatch_tree_row(tdata->widgets, "Deleted", buffer + 10);
            } else if (strncmp(buffer, ">f+++++", 7) == 0) {
                dispatch_tree_row(tdata->widgets, "New", strchr(buffer, ' ') + 1);
            } else if (strncmp(buffer, ">f", 2) == 0 || strncmp(buffer, ">c", 2) == 0) {
                char *space = strchr(buffer, ' ');
                if (space) dispatch_tree_row(tdata->widgets, "Modified", space + 1);
            }
        } else {
            // Live sync, just print to log
            if (strlen(buffer) > 0) {
                dispatch_log(tdata->widgets, buffer);
            }
        }
    }
    pclose(fp);

    // If actual sync, run the checksum phase as your bash script did
    if (!tdata->is_preview) {
        dispatch_log(tdata->widgets, "[*] Running Checksum verification...");
        system("sed -nE '/^[>ch]f/{s/^[^ ]+ //; p}' /tmp/rsyncfiles > /tmp/sync_gui.files");
        
        snprintf(cmd, sizeof(cmd), 
            "rsync --verbose --update --recursive --partial --links --hard-links --itemize-changes "
            "--perms --times --owner --group --delete --delete-after --delete-excluded --stats "
            "--files-from=/tmp/sync_gui.files --checksum '%s' '%s/' 2>&1",
            SOURCE_DIR, tdata->dest_path);
        
        fp = popen(cmd, "r");
        if (fp != NULL) {
            while (fgets(buffer, sizeof(buffer), fp) != NULL) {
                buffer[strcspn(buffer, "\n")] = 0;
                dispatch_log(tdata->widgets, buffer);
            }
            pclose(fp);
        }
    }

    dispatch_log(tdata->widgets, "=== Operation Complete ===");

    if (tdata->is_preview) {
        dispatch_state(tdata->widgets, 2); // Enable Sync button
    }

    g_free(tdata);
    return NULL;
}

/* --- Signal Handlers --- */

void on_browse_clicked(GtkWidget *widget, gpointer user_data) {
    AppWidgets *widgets = (AppWidgets *)user_data;
    GtkWidget *dialog;
    GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER;
    gint res;

    dialog = gtk_file_chooser_dialog_new("Select Destination",
                                         GTK_WINDOW(gtk_widget_get_toplevel(widget)),
                                         action,
                                         "_Cancel", GTK_RESPONSE_CANCEL,
                                         "_Open", GTK_RESPONSE_ACCEPT,
                                         NULL);

    res = gtk_dialog_run(GTK_DIALOG(dialog));
    if (res == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        gtk_entry_set_text(GTK_ENTRY(widgets->dest_entry), filename);
        g_free(filename);
    }

    gtk_widget_destroy(dialog);
    return;
}

int validate_devices(const char *dest) {
    struct stat src_stat, dest_stat;
    
    if (stat(SOURCE_DIR, &src_stat) != 0) return 0;
    if (stat(dest, &dest_stat) != 0) return 0;

    if (src_stat.st_dev == dest_stat.st_dev) {
        return 0; // Same device
    }
    return 1;
}

void on_preview_clicked(GtkWidget *widget, gpointer user_data) {
    AppWidgets *widgets = (AppWidgets *)user_data;
    const char *dest = gtk_entry_get_text(GTK_ENTRY(widgets->dest_entry));

    if (strlen(dest) == 0 || !validate_devices(dest)) {
        dispatch_log(widgets, "Error: Invalid destination or same storage device as source.");
        return;
    }

    gtk_widget_set_sensitive(widgets->preview_btn, FALSE);
    gtk_widget_set_sensitive(widgets->sync_btn, FALSE);
    dispatch_state(widgets, 3); // Clear the tree

    ThreadData *tdata = g_new0(ThreadData, 1);
    tdata->widgets = widgets;
    tdata->is_preview = 1;
    strncpy(tdata->dest_path, dest, sizeof(tdata->dest_path) - 1);

    g_thread_new("preview_thread", worker_thread, tdata);
    return;
}

void on_sync_clicked(GtkWidget *widget, gpointer user_data) {
    AppWidgets *widgets = (AppWidgets *)user_data;
    const char *dest = gtk_entry_get_text(GTK_ENTRY(widgets->dest_entry));

    // Confirmation dialog
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(gtk_widget_get_toplevel(widget)),
                                               GTK_DIALOG_DESTROY_WITH_PARENT,
                                               GTK_MESSAGE_QUESTION,
                                               GTK_BUTTONS_YES_NO,
                                               "Are you sure you want to execute these changes?");
    int response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    if (response != GTK_RESPONSE_YES) {
        return;
    }

    gtk_widget_set_sensitive(widgets->preview_btn, FALSE);
    gtk_widget_set_sensitive(widgets->sync_btn, FALSE);

    ThreadData *tdata = g_new0(ThreadData, 1);
    tdata->widgets = widgets;
    tdata->is_preview = 0;
    strncpy(tdata->dest_path, dest, sizeof(tdata->dest_path) - 1);

    g_thread_new("sync_thread", worker_thread, tdata);
    return;
}

/* --- Main / UI Setup --- */

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    AppWidgets *widgets = g_new0(AppWidgets, 1);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Rsync Backup Manager");
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    // Top Controls
    GtkWidget *hbox_top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), hbox_top, FALSE, FALSE, 5);

    GtkWidget *src_label = gtk_label_new("Source: " SOURCE_DIR);
    gtk_box_pack_start(GTK_BOX(hbox_top), src_label, FALSE, FALSE, 5);

    GtkWidget *hbox_dest = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), hbox_dest, FALSE, FALSE, 5);

    GtkWidget *dest_label = gtk_label_new("Destination:");
    gtk_box_pack_start(GTK_BOX(hbox_dest), dest_label, FALSE, FALSE, 5);

    widgets->dest_entry = gtk_entry_new();
    gtk_widget_set_hexpand(widgets->dest_entry, TRUE);
    gtk_box_pack_start(GTK_BOX(hbox_dest), widgets->dest_entry, TRUE, TRUE, 5);

    GtkWidget *browse_btn = gtk_button_new_with_label("Browse...");
    g_signal_connect(browse_btn, "clicked", G_CALLBACK(on_browse_clicked), widgets);
    gtk_box_pack_start(GTK_BOX(hbox_dest), browse_btn, FALSE, FALSE, 5);

    // Buttons
    GtkWidget *hbox_btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), hbox_btns, FALSE, FALSE, 5);

    widgets->preview_btn = gtk_button_new_with_label("1. Preview Changes");
    g_signal_connect(widgets->preview_btn, "clicked", G_CALLBACK(on_preview_clicked), widgets);
    gtk_box_pack_start(GTK_BOX(hbox_btns), widgets->preview_btn, FALSE, FALSE, 5);

    widgets->sync_btn = gtk_button_new_with_label("2. Execute Sync");
    gtk_widget_set_sensitive(widgets->sync_btn, FALSE); // Locked until preview
    g_signal_connect(widgets->sync_btn, "clicked", G_CALLBACK(on_sync_clicked), widgets);
    gtk_box_pack_start(GTK_BOX(hbox_btns), widgets->sync_btn, FALSE, FALSE, 5);

    // TreeView for Preview
    GtkWidget *tree_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_vexpand(tree_scroll, TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), tree_scroll, TRUE, TRUE, 5);

    widgets->list_store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
    widgets->tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(widgets->list_store));
    g_object_unref(widgets->list_store); // TreeView holds the reference now

    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(widgets->tree_view), -1, "Action", renderer, "text", 0, NULL);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(widgets->tree_view), -1, "File Path", renderer, "text", 1, NULL);
    gtk_container_add(GTK_CONTAINER(tree_scroll), widgets->tree_view);

    // Log View
    GtkWidget *log_label = gtk_label_new("Log:");
    gtk_widget_set_halign(log_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(vbox), log_label, FALSE, FALSE, 0);

    GtkWidget *log_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(log_scroll, -1, 150);
    gtk_box_pack_start(GTK_BOX(vbox), log_scroll, FALSE, FALSE, 5);

    GtkWidget *log_text_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(log_text_view), FALSE);
    widgets->log_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(log_text_view));
    gtk_container_add(GTK_CONTAINER(log_scroll), log_text_view);

    gtk_widget_show_all(window);
    gtk_main();

    g_free(widgets);
    return 0;
}
