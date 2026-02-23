#include <gtk/gtk.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "util.c"
#include "hooks.c"

static void setup_tree_columns(GtkWidget *tree);

int32
main(int32 argc, char *argv[]) {
    AppWidgets *w;
    GtkWidget *main_vbox;
    GtkWidget *header_vbox;
    GtkWidget *invert_hbox;
    GtkWidget *invert_btn;
    GtkWidget *btn_hbox;
    GtkWidget *options_hbox;
    GtkWidget *v_paned;
    GtkWidget *paned;
    GtkWidget *l_vbox;
    GtkWidget *l_entry_hbox;
    GtkWidget *browse_src;
    GtkWidget *l_scroll;
    GtkWidget *l_tree;
    GtkWidget *r_vbox;
    GtkWidget *r_entry_hbox;
    GtkWidget *browse_dst;
    GtkWidget *r_scroll;
    GtkWidget *r_tree;
    GtkWidget *log_scroll;
    GtkWidget *log_view;
    GtkAdjustment *l_adj;
    GtkAdjustment *r_adj;
    char *cwd;
    char *default_src;
    char *default_dst;
    char *config_dir;

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
    gtk_box_pack_start(GTK_BOX(main_vbox), header_vbox, FALSE, FALSE, 0);

    invert_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    invert_btn = gtk_button_new_with_label("<--->");
    gtk_widget_set_size_request(invert_btn, 80, -1);
    gtk_box_pack_start(GTK_BOX(invert_hbox), invert_btn, TRUE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header_vbox), invert_hbox, FALSE, FALSE, 0);

    btn_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    w->preview_button = gtk_button_new_with_label("1. Preview");
    w->exclude_button = gtk_button_new_with_label("Edit Exclusions");
    w->sync_button = gtk_button_new_with_label("2. Sync");
    gtk_widget_set_sensitive(w->sync_button, FALSE);
    gtk_box_pack_start(GTK_BOX(btn_hbox), w->exclude_button, FALSE, FALSE, 5);
    gtk_box_pack_end(GTK_BOX(btn_hbox), w->sync_button, FALSE, FALSE, 5);
    gtk_box_pack_end(GTK_BOX(btn_hbox), w->preview_button, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(header_vbox), btn_hbox, FALSE, FALSE, 5);

    options_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    w->check_fs_toggle
        = gtk_check_button_new_with_label("Require different filesystems");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->check_fs_toggle), TRUE);
    gtk_box_pack_start(GTK_BOX(options_hbox), w->check_fs_toggle, FALSE, FALSE,
                       5);
    gtk_box_pack_start(GTK_BOX(header_vbox), options_hbox, FALSE, FALSE, 0);

    v_paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    gtk_box_pack_start(GTK_BOX(main_vbox), v_paned, TRUE, TRUE, 0);

    paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_pack1(GTK_PANED(v_paned), paned, TRUE, FALSE);

    cwd = g_get_current_dir();
    default_src = g_strdup_printf("%s/a/", cwd);
    default_dst = g_strdup_printf("%s/b/", cwd);

    l_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(l_vbox), 5);
    l_entry_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    w->src_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(w->src_entry), default_src);
    browse_src = gtk_button_new_with_label("Browse");
    gtk_box_pack_start(GTK_BOX(l_entry_hbox), gtk_label_new("Source:"), FALSE,
                       FALSE, 0);
    gtk_box_pack_start(GTK_BOX(l_entry_hbox), w->src_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(l_entry_hbox), browse_src, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(l_vbox), l_entry_hbox, FALSE, FALSE, 0);

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
    gtk_container_set_border_width(GTK_CONTAINER(r_vbox), 5);
    r_entry_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    w->dst_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(w->dst_entry), default_dst);
    browse_dst = gtk_button_new_with_label("Browse");
    gtk_box_pack_start(GTK_BOX(r_entry_hbox), gtk_label_new("Dest:"), FALSE,
                       FALSE, 0);
    gtk_box_pack_start(GTK_BOX(r_entry_hbox), w->dst_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(r_entry_hbox), browse_dst, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(r_vbox), r_entry_hbox, FALSE, FALSE, 0);

    r_scroll = gtk_scrolled_window_new(NULL, NULL);
    w->dst_store = gtk_list_store_new(NUM_COLS, G_TYPE_STRING, G_TYPE_STRING,
                                      G_TYPE_STRING, G_TYPE_INT64,
                                      G_TYPE_STRING, G_TYPE_STRING);
    r_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(w->dst_store));
    setup_tree_columns(r_tree);
    gtk_container_add(GTK_CONTAINER(r_scroll), r_tree);
    gtk_box_pack_start(GTK_BOX(r_vbox), r_scroll, TRUE, TRUE, 0);
    gtk_paned_pack2(GTK_PANED(paned), r_vbox, TRUE, FALSE);

    g_free(cwd);
    g_free(default_src);
    g_free(default_dst);

    l_adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(l_scroll));
    r_adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(r_scroll));
    g_signal_connect(l_adj, "value-changed", G_CALLBACK(on_scroll_sync), r_adj);
    r_adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(r_scroll));
    g_signal_connect(r_adj, "value-changed", G_CALLBACK(on_scroll_sync), l_adj);
    g_signal_connect(w->src_store, "sort-column-changed",
                     G_CALLBACK(on_sort_changed), w->dst_store);
    g_signal_connect(w->dst_store, "sort-column-changed",
                     G_CALLBACK(on_sort_changed), w->src_store);

    log_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(log_scroll, -1, 150);
    log_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(log_view), FALSE);
    w->log_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(log_view));
    gtk_container_add(GTK_CONTAINER(log_scroll), log_view);
    gtk_paned_pack2(GTK_PANED(v_paned), log_scroll, FALSE, FALSE);

    g_signal_connect(browse_src, "clicked", G_CALLBACK(on_browse_src), w);
    g_signal_connect(browse_dst, "clicked", G_CALLBACK(on_browse_dst), w);
    g_signal_connect(invert_btn, "clicked", G_CALLBACK(on_invert_clicked), w);
    g_signal_connect(w->preview_button, "clicked",
                     G_CALLBACK(on_preview_clicked), w);
    g_signal_connect(w->sync_button, "clicked", G_CALLBACK(on_sync_clicked), w);
    g_signal_connect(w->exclude_button, "clicked",
                     G_CALLBACK(on_exclude_clicked), w);

    gtk_widget_show_all(w->gtk_window);
    gtk_paned_set_position(GTK_PANED(v_paned), 550);
    gtk_main();
    g_free(w->exclude_path);
    g_free(w);
    exit(EXIT_SUCCESS);
}

static gboolean
update_ui_handler(gpointer user_data) {
    UIUpdateData *data;
    GtkListStore *src_store;
    GtkListStore *dst_store;
    GtkTreeIter iter_src;
    GtkTreeIter iter_dst;
    char *size_str;
    char *bg_src;
    char *bg_dst;
    char *path_src;
    char *path_dst;
    char *a_src;
    char *a_dst;

    data = (UIUpdateData *)user_data;
    src_store = data->widgets->src_store;
    dst_store = data->widgets->dst_store;

    switch (data->type) {
    case DATA_TYPE_LOG: {
        GtkTextIter end;
        char *ptr;
        int32 current_len;
        int32 last_space_idx;
        int32 i;
        char wrapped[8192];
        int32 w_idx;

        ptr = data->message;
        w_idx = 0;
        current_len = 0;
        last_space_idx = -1;

        for (i = 0; ptr[i] != '\0'; i++) {
            wrapped[w_idx++] = ptr[i];
            current_len++;
            if (isspace((unsigned char)ptr[i])) {
                last_space_idx = w_idx - 1;
            }
            if (current_len >= 160) {
                if (last_space_idx != -1) {
                    wrapped[last_space_idx] = '\n';
                    memmove(&wrapped[last_space_idx + 5],
                            &wrapped[last_space_idx + 1],
                            w_idx - (last_space_idx + 1));
                    wrapped[last_space_idx + 1] = ' ';
                    wrapped[last_space_idx + 2] = ' ';
                    wrapped[last_space_idx + 3] = ' ';
                    wrapped[last_space_idx + 4] = ' ';
                    w_idx += 4;
                    current_len = (w_idx - 1) - last_space_idx;
                    last_space_idx = -1;
                }
            }
        }
        wrapped[w_idx] = '\0';

        gtk_text_buffer_get_end_iter(data->widgets->log_buffer, &end);
        gtk_text_buffer_insert(data->widgets->log_buffer, &end, wrapped, -1);
        gtk_text_buffer_insert(data->widgets->log_buffer, &end, "\n", -1);
        break;
    }
    case DATA_TYPE_TREE_ROW: {
        size_str = bytes_pretty(data->size);
        bg_src = "#FFFFFF";
        bg_dst = "#FFFFFF";
        path_src = data->filepath;
        path_dst = data->filepath;
        a_src = data->action;
        a_dst = data->action;

        if (g_strcmp0(data->action, "New") == 0) {
            bg_src = "#D4EDDA";
            bg_dst = "#FFFFFF";
            path_dst = "-";
        } else if (g_strcmp0(data->action, "Update") == 0) {
            bg_src = "#CCE5FF";
            bg_dst = "#CCE5FF";
        } else if (g_strcmp0(data->action, "Delete") == 0) {
            if (data->reason && g_strrstr(data->reason, "excluded")) {
                bg_src = "#FFF3CD";
                bg_dst = "#FFF3CD";
                a_src = "Ignore";
                a_dst = "Delete";
            } else {
                bg_src = "#FFFFFF";
                bg_dst = "#F8D7DA";
                a_src = "Deleted";
                a_dst = "Delete";
                path_src = "-";
            }
        }
        gtk_list_store_append(src_store, &iter_src);
        gtk_list_store_append(dst_store, &iter_dst);
        gtk_list_store_set(src_store, &iter_src, COL_ACTION, a_src, COL_PATH,
                           path_src, COL_SIZE_TEXT, size_str, COL_SIZE_RAW,
                           data->size, COL_COLOR, bg_src, COL_REASON,
                           data->reason, -1);
        gtk_list_store_set(dst_store, &iter_dst, COL_ACTION, a_dst, COL_PATH,
                           path_dst, COL_SIZE_TEXT, size_str, COL_SIZE_RAW,
                           data->size, COL_COLOR, bg_dst, COL_REASON,
                           data->reason, -1);
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
    default:
        error("Invalid data->type: %d.\n", (int)data->type);
        exit(EXIT_FAILURE);
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

static void
setup_tree_columns(GtkWidget *gtk_tree) {
    GtkCellRenderer *gtk_cell_renderer;
    GtkTreeViewColumn *gtk_tree_view_column;

    gtk_cell_renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_column = gtk_tree_view_column_new_with_attributes(
        "Action", gtk_cell_renderer, "text", COL_ACTION, "cell-background",
        COL_COLOR, NULL);
    gtk_tree_view_column_set_sort_column_id(gtk_tree_view_column, COL_ACTION);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gtk_tree), gtk_tree_view_column);

    gtk_tree_view_column = gtk_tree_view_column_new_with_attributes(
        "File Path", gtk_cell_renderer, "text", COL_PATH, "cell-background",
        COL_COLOR, NULL);
    gtk_tree_view_column_set_sort_column_id(gtk_tree_view_column, COL_PATH);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gtk_tree), gtk_tree_view_column);

    gtk_tree_view_column = gtk_tree_view_column_new_with_attributes(
        "Size", gtk_cell_renderer, "text", COL_SIZE_TEXT, "cell-background",
        COL_COLOR, NULL);
    gtk_tree_view_column_set_sort_column_id(gtk_tree_view_column, COL_SIZE_RAW);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gtk_tree), gtk_tree_view_column);

    gtk_widget_set_has_tooltip(gtk_tree, TRUE);
    g_signal_connect(gtk_tree, "query-tooltip", G_CALLBACK(on_tree_tooltip),
                     NULL);
    g_signal_connect(gtk_tree, "button-press-event",
                     G_CALLBACK(on_tree_button_press), NULL);
    return;
}
