#include <gtk/gtk.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

#include "util.c"

enum {
    COL_ACTION = 0,
    COL_PATH,
    COL_SIZE_TEXT,
    COL_SIZE_RAW,
    COL_COLOR,
    COL_REASON,
    NUM_COLS
};

typedef struct AppWidgets {
    GtkWidget *gtk_window;
    GtkWidget *src_entry;
    GtkWidget *dst_entry;
    GtkWidget *preview_button;
    GtkWidget *sync_button;
    GtkWidget *exclude_button;
    GtkListStore *src_store;
    GtkListStore *dst_store;
    GtkTextBuffer *log_buffer;
    char *exclude_path;
} AppWidgets;

typedef struct ThreadData {
    AppWidgets *widgets;
    char src_path[1024];
    char dst_path[1024];
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
    char *reason;
    int64 size;
    int32 side;
    enum DataType type;
} UIUpdateData;

static void setup_tree_columns(GtkWidget *tree);
static void on_browse_src(GtkWidget *b, gpointer data);
static void on_browse_dest(GtkWidget *b, gpointer data);
static void on_preview_clicked(GtkWidget *b, gpointer data);
static void on_sync_clicked(GtkWidget *b, gpointer data);
static void on_exclude_clicked(GtkWidget *b, gpointer data);

int32
main(int32 argc, char *argv[]) {
    AppWidgets *w;
    GtkWidget *main_vbox;
    GtkWidget *header_vbox;
    GtkWidget *src_hbox;
    GtkWidget *browse_src;
    GtkWidget *dst_hbox;
    GtkWidget *browse_dest;
    GtkWidget *btn_hbox;
    GtkWidget *paned;
    GtkWidget *l_vbox;
    GtkWidget *l_scroll;
    GtkWidget *l_tree;
    GtkWidget *r_vbox;
    GtkWidget *r_scroll;
    GtkWidget *r_tree;
    GtkWidget *log_scroll;
    GtkWidget *log_view;
    char *cwd;
    char *default_src;
    char *default_dest;
    const char *config_dir;

    gtk_init(&argc, &argv);

    w = g_new0(AppWidgets, 1);

    config_dir = g_get_user_config_dir();
    w->exclude_path
        = g_build_filename(config_dir, "cecup_exclude_patterns.conf", NULL);

    w->gtk_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(w->gtk_window), "Btrfs Rsync Sync GUI");
    gtk_window_set_default_size(GTK_WINDOW(w->gtk_window), 1100, 800);
    g_signal_connect(w->gtk_window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    main_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(w->gtk_window), main_vbox);

    header_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(header_vbox), 10);

    cwd = g_get_current_dir();
    default_src = g_strdup_printf("%s/a/", cwd);
    default_dest = g_strdup_printf("%s/b/", cwd);

    src_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(src_hbox), gtk_label_new("Source:      "), FALSE,
                       FALSE, 5);
    w->src_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(w->src_entry), default_src);
    browse_src = gtk_button_new_with_label("Browse");
    gtk_box_pack_start(GTK_BOX(src_hbox), w->src_entry, TRUE, TRUE, 5);
    gtk_box_pack_start(GTK_BOX(src_hbox), browse_src, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(header_vbox), src_hbox, FALSE, FALSE, 0);

    dst_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(dst_hbox), gtk_label_new("Destination:"), FALSE,
                       FALSE, 5);
    w->dst_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(w->dst_entry), default_dest);
    browse_dest = gtk_button_new_with_label("Browse");
    gtk_box_pack_start(GTK_BOX(dst_hbox), w->dst_entry, TRUE, TRUE, 5);
    gtk_box_pack_start(GTK_BOX(dst_hbox), browse_dest, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(header_vbox), dst_hbox, FALSE, FALSE, 0);

    g_free(cwd);
    g_free(default_src);
    g_free(default_dest);

    btn_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    w->preview_button = gtk_button_new_with_label("1. Preview");
    w->exclude_button = gtk_button_new_with_label("Edit Exclusions");
    w->sync_button = gtk_button_new_with_label("2. Sync");
    gtk_widget_set_sensitive(w->sync_button, FALSE);
    gtk_box_pack_start(GTK_BOX(btn_hbox), w->exclude_button, FALSE, FALSE, 5);
    gtk_box_pack_end(GTK_BOX(btn_hbox), w->sync_button, FALSE, FALSE, 5);
    gtk_box_pack_end(GTK_BOX(btn_hbox), w->preview_button, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(header_vbox), btn_hbox, FALSE, FALSE, 5);

    gtk_box_pack_start(GTK_BOX(main_vbox), header_vbox, FALSE, FALSE, 0);

    paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(main_vbox), paned, TRUE, TRUE, 0);

    l_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_box_pack_start(GTK_BOX(l_vbox),
                       gtk_label_new("Origin: To be Transferred"), FALSE, FALSE,
                       0);
    l_scroll = gtk_scrolled_window_new(NULL, NULL);
    w->src_store = gtk_list_store_new(NUM_COLS, G_TYPE_STRING, G_TYPE_STRING,
                                      G_TYPE_STRING, G_TYPE_INT64,
                                      G_TYPE_STRING, G_TYPE_STRING);
    l_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(w->src_store));
    setup_tree_columns(l_tree);
    gtk_container_add(GTK_CONTAINER(l_scroll), l_tree);
    gtk_box_pack_start(GTK_BOX(l_vbox), l_scroll, TRUE, TRUE, 0);
    gtk_paned_pack1(GTK_PANED(paned), l_vbox, TRUE, FALSE);

    r_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_box_pack_start(GTK_BOX(r_vbox),
                       gtk_label_new("Destination: To be Deleted"), FALSE,
                       FALSE, 0);
    r_scroll = gtk_scrolled_window_new(NULL, NULL);
    w->dst_store = gtk_list_store_new(NUM_COLS, G_TYPE_STRING, G_TYPE_STRING,
                                      G_TYPE_STRING, G_TYPE_INT64,
                                      G_TYPE_STRING, G_TYPE_STRING);
    r_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(w->dst_store));
    setup_tree_columns(r_tree);
    gtk_container_add(GTK_CONTAINER(r_scroll), r_tree);
    gtk_box_pack_start(GTK_BOX(r_vbox), r_scroll, TRUE, TRUE, 0);
    gtk_paned_pack2(GTK_PANED(paned), r_vbox, TRUE, FALSE);

    log_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(log_scroll, -1, 150);
    log_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(log_view), FALSE);
    w->log_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(log_view));
    gtk_container_add(GTK_CONTAINER(log_scroll), log_view);
    gtk_box_pack_start(GTK_BOX(main_vbox), log_scroll, FALSE, FALSE, 5);

    g_signal_connect(browse_src, "clicked", G_CALLBACK(on_browse_src), w);
    g_signal_connect(browse_dest, "clicked", G_CALLBACK(on_browse_dest), w);
    g_signal_connect(w->preview_button, "clicked",
                     G_CALLBACK(on_preview_clicked), w);
    g_signal_connect(w->sync_button, "clicked", G_CALLBACK(on_sync_clicked), w);
    g_signal_connect(w->exclude_button, "clicked",
                     G_CALLBACK(on_exclude_clicked), w);

    gtk_widget_show_all(w->gtk_window);
    gtk_main();

    g_free(w->exclude_path);
    g_free(w);
    exit(EXIT_SUCCESS);
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
    case DATA_TYPE_TREE_ROW: {
        GtkListStore *target = (data->side == 0) ? data->widgets->src_store
                                                 : data->widgets->dst_store;
        GtkTreeIter iter;
        char *size_str = bytes_pretty(data->size);
        const char *bg_color = "#FFFFFF";

        if (g_strcmp0(data->action, "New") == 0) {
            bg_color = "#D4EDDA";
        }
        if (g_strcmp0(data->action, "Update") == 0) {
            bg_color = "#CCE5FF";
        }
        if (g_strcmp0(data->action, "Delete") == 0) {
            bg_color = "#F8D7DA";
        }

        gtk_list_store_append(target, &iter);
        gtk_list_store_set(target, &iter, COL_ACTION, data->action, COL_PATH,
                           data->filepath, COL_SIZE_TEXT, size_str,
                           COL_SIZE_RAW, data->size, COL_COLOR, bg_color,
                           COL_REASON, data->reason, -1);
        g_free(size_str);
        break;
    }
    case DATA_TYPE_ENABLE_BUTTONS:
        gtk_widget_set_sensitive(data->widgets->sync_button, TRUE);
        gtk_widget_set_sensitive(data->widgets->preview_button, TRUE);
        break;
    case DATA_TYPE_CLEAR_TREES:
        gtk_list_store_clear(data->widgets->src_store);
        gtk_list_store_clear(data->widgets->dst_store);
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
    if (data->reason) {
        g_free(data->reason);
    }
    g_free(data);
    return G_SOURCE_REMOVE;
}

static gboolean
on_tree_tooltip(GtkWidget *widget, gint x, gint y, gboolean keyboard_mode,
                GtkTooltip *tooltip, gpointer user_data) {
    GtkTreePath *gtk_tree_path;
    GtkTreeViewColumn *gtk_tree_view_column;

    (void)keyboard_mode;
    (void)user_data;

    if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), x, y,
                                      &gtk_tree_path, &gtk_tree_view_column,
                                      NULL, NULL)) {
        GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(widget));
        GtkTreeIter iter;

        if (gtk_tree_model_get_iter(model, &iter, gtk_tree_path)) {
            gchar *reason = NULL;
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
              int64 size, const char *reason) {
    UIUpdateData *data = g_new0(UIUpdateData, 1);
    data->widgets = w;
    data->type = DATA_TYPE_TREE_ROW;
    data->side = side;
    data->action = g_strdup(act);
    data->filepath = g_strdup(path);
    data->reason = g_strdup(reason);
    data->size = size;
    g_idle_add(update_ui_handler, data);
    return;
}

gpointer
sync_worker(gpointer user_data) {
    ThreadData *tdata = (ThreadData *)user_data;
    char cmd[4096];
    char buffer[2048];
    FILE *rsync_pipe;

    if (tdata->is_preview) {
        UIUpdateData *clear = g_new0(UIUpdateData, 1);
        clear->widgets = tdata->widgets;
        clear->type = DATA_TYPE_CLEAR_TREES;
        g_idle_add(update_ui_handler, clear);
    }

    snprintf(cmd, sizeof(cmd),
             "rsync --verbose --update --recursive --partial"
             " --links --hard-links"
             " --itemize-changes --perms --times --owner --group --delete"
             " --delete-after"
             " --delete-excluded --stats %s %s %s '%s/' '%s/' 2>&1",
             tdata->is_preview ? "--dry-run" : "--info=progress2",
             access(tdata->widgets->exclude_path, F_OK) != -1
                 ? g_strdup_printf("--exclude-from='%s'",
                                   tdata->widgets->exclude_path)
                 : "",
             tdata->is_preview ? "" : "| tee /tmp/rsyncfiles", tdata->src_path,
             tdata->dst_path);

    rsync_pipe = popen(cmd, "r");
    if (!rsync_pipe) {
        dispatch_log(tdata->widgets, "ERROR: Failed to start rsync process.");
    } else {
        while (fgets(buffer, sizeof(buffer), rsync_pipe)) {
            buffer[strcspn(buffer, "\n")] = 0;
            if (tdata->is_preview) {
                if (strncmp(buffer, "*deleting", 9) == 0) {
                    char full_path[2048];
                    struct stat st;
                    snprintf(full_path, sizeof(full_path), "%s/%s",
                             tdata->dst_path, buffer + 10);
                    int64 sz = (stat(full_path, &st) == 0) ? st.st_size : 0;
                    dispatch_tree(tdata->widgets, 1, "Delete", buffer + 10, sz,
                                  "File removed from source directory");
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
                        const char *reason
                            = (strncmp(buffer, ">f+++++", 7) == 0)
                                  ? "New file in source directory"
                                  : "File updated in source directory";
                        dispatch_tree(tdata->widgets, 0, act, space + 1, sz,
                                      reason);
                    }
                }
            } else {
                dispatch_log(tdata->widgets, buffer);
            }
        }
        pclose(rsync_pipe);
    }

    if (!tdata->is_preview) {
        dispatch_log(tdata->widgets, ">>> Starting Checksum Verification...");
        system("sed -nE '/^[>ch]f/{s/^[^ ]+ //; p}' /tmp/rsyncfiles > "
               "/tmp/sync.files");
        snprintf(cmd, sizeof(cmd),
                 "rsync --verbose --checksum --files-from=/tmp/sync.files "
                 "'%s/' '%s/' 2>&1",
                 tdata->src_path, tdata->dst_path);

        rsync_pipe = popen(cmd, "r");
        if (!rsync_pipe) {
            dispatch_log(tdata->widgets,
                         "ERROR: Failed to start checksum verification rsync.");
        } else {
            while (fgets(buffer, sizeof(buffer), rsync_pipe)) {
                buffer[strcspn(buffer, "\n")] = 0;
                dispatch_log(tdata->widgets, buffer);
            }
            pclose(rsync_pipe);
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
    const char *src = gtk_entry_get_text(GTK_ENTRY(w->src_entry));
    const char *dest = gtk_entry_get_text(GTK_ENTRY(w->dst_entry));

    (void)b;
    if (strlen64(src) < 1 || strlen64(dest) < 1) {
        return;
    }
    gtk_widget_set_sensitive(w->preview_button, FALSE);
    gtk_widget_set_sensitive(w->sync_button, FALSE);
    ThreadData *thread_data = g_new0(ThreadData, 1);
    thread_data->widgets = w;
    thread_data->is_preview = 1;
    strncpy(thread_data->src_path, src, 1023);
    strncpy(thread_data->dst_path, dest, 1023);
    g_thread_new("worker", sync_worker, thread_data);
    return;
}

static void
on_sync_clicked(GtkWidget *b, gpointer data) {
    AppWidgets *w = (AppWidgets *)data;
    const char *src = gtk_entry_get_text(GTK_ENTRY(w->src_entry));
    const char *dest = gtk_entry_get_text(GTK_ENTRY(w->dst_entry));
    GtkWidget *dialog;

    (void)b;
    dialog = gtk_message_dialog_new(GTK_WINDOW(w->gtk_window), GTK_DIALOG_MODAL,
                                    GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
                                    "Confirm sync from %s to %s?", src, dest);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES) {
        gtk_widget_set_sensitive(w->preview_button, FALSE);
        gtk_widget_set_sensitive(w->sync_button, FALSE);
        ThreadData *thread_data = g_new0(ThreadData, 1);
        thread_data->widgets = w;
        thread_data->is_preview = 0;
        strncpy(thread_data->src_path, src, 1023);
        strncpy(thread_data->dst_path, dest, 1023);
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
    GtkWidget *dlg = gtk_file_chooser_dialog_new(
        "Select Source Directory", GTK_WINDOW(w->gtk_window),
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
        "Select Destination Directory", GTK_WINDOW(w->gtk_window),
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "_Cancel", GTK_RESPONSE_CANCEL,
        "_Select", GTK_RESPONSE_ACCEPT, NULL);

    (void)b;
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        gtk_entry_set_text(GTK_ENTRY(w->dst_entry), path);
        g_free(path);
    }
    gtk_widget_destroy(dlg);
    return;
}

static void
setup_tree_columns(GtkWidget *tree) {
    GtkCellRenderer *gtk_cell_renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *gtk_tree_view_column;

    gtk_tree_view_column = gtk_tree_view_column_new_with_attributes(
        "Action", gtk_cell_renderer, "text", COL_ACTION, "cell-background",
        COL_COLOR, NULL);
    gtk_tree_view_column_set_sort_column_id(gtk_tree_view_column, COL_ACTION);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), gtk_tree_view_column);

    gtk_tree_view_column = gtk_tree_view_column_new_with_attributes(
        "File Path", gtk_cell_renderer, "text", COL_PATH, "cell-background",
        COL_COLOR, NULL);
    gtk_tree_view_column_set_sort_column_id(gtk_tree_view_column, COL_PATH);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), gtk_tree_view_column);

    gtk_tree_view_column = gtk_tree_view_column_new_with_attributes(
        "Size", gtk_cell_renderer, "text", COL_SIZE_TEXT, "cell-background",
        COL_COLOR, NULL);
    gtk_tree_view_column_set_sort_column_id(gtk_tree_view_column, COL_SIZE_RAW);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), gtk_tree_view_column);

    gtk_widget_set_has_tooltip(tree, TRUE);
    g_signal_connect(tree, "query-tooltip", G_CALLBACK(on_tree_tooltip), NULL);

    return;
}
