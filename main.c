#include <gtk/gtk.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "util.c"
#include "hooks.c"

static void setup_tree_columns(GtkWidget *tree, AppWidgets *w, int32 col_act,
                               int32 col_path, int32 col_color);

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
    GtkTreeSelection *l_sel;
    GtkWidget *r_vbox;
    GtkWidget *r_entry_hbox;
    GtkWidget *browse_dst;
    GtkWidget *r_scroll;
    GtkWidget *r_tree;
    GtkTreeSelection *r_sel;
    GtkWidget *log_scroll;
    GtkWidget *log_view;
    GtkAdjustment *l_adj;
    GtkAdjustment *r_adj;
    GtkWidget *diff_lbl;
    GtkWidget *term_lbl;
    char *cwd;
    char *default_src;
    char *default_dst;
    char *config_dir;
    char *auto_terminal;

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

    diff_lbl = gtk_label_new("Diff Tool:");
    w->diff_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(w->diff_entry), "unidiff.bash");
    gtk_widget_set_tooltip_text(
        w->diff_entry,
        "Command for comparing files (e.g., diffoscope, meld, diffuse)");

    term_lbl = gtk_label_new("Terminal:");
    w->term_entry = gtk_entry_new();
    auto_terminal = find_terminal();
    gtk_entry_set_text(GTK_ENTRY(w->term_entry),
                       auto_terminal ? auto_terminal : "");
    gtk_widget_set_tooltip_text(
        w->term_entry,
        "Terminal emulator to launch (e.g., xterm, kitty, konsole)");

    gtk_box_pack_start(GTK_BOX(options_hbox), w->check_fs_toggle, FALSE, FALSE,
                       5);
    gtk_box_pack_start(GTK_BOX(options_hbox), diff_lbl, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(options_hbox), w->diff_entry, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(options_hbox), term_lbl, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(options_hbox), w->term_entry, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header_vbox), options_hbox, FALSE, FALSE, 0);

    v_paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    gtk_box_pack_start(GTK_BOX(main_vbox), v_paned, TRUE, TRUE, 0);

    paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_pack1(GTK_PANED(v_paned), paned, TRUE, FALSE);

    cwd = g_get_current_dir();
    default_src = g_strdup_printf("%s/a/", cwd);
    default_dst = g_strdup_printf("%s/b/", cwd);

    w->store = gtk_list_store_new(NUM_COLS, G_TYPE_STRING, G_TYPE_STRING,
                                  G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                                  G_TYPE_INT64, G_TYPE_STRING, G_TYPE_STRING,
                                  G_TYPE_STRING);

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
    l_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(w->store));
    l_sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(l_tree));
    gtk_tree_selection_set_mode(l_sel, GTK_SELECTION_MULTIPLE);
    g_object_set_data(G_OBJECT(l_tree), "side", GINT_TO_POINTER(0));
    setup_tree_columns(l_tree, w, COL_SRC_ACTION, COL_SRC_PATH, COL_SRC_COLOR);
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
    r_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(w->store));
    r_sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(r_tree));
    gtk_tree_selection_set_mode(r_sel, GTK_SELECTION_MULTIPLE);
    g_object_set_data(G_OBJECT(r_tree), "side", GINT_TO_POINTER(1));
    setup_tree_columns(r_tree, w, COL_DST_ACTION, COL_DST_PATH, COL_DST_COLOR);
    gtk_container_add(GTK_CONTAINER(r_scroll), r_tree);
    gtk_box_pack_start(GTK_BOX(r_vbox), r_scroll, TRUE, TRUE, 0);
    gtk_paned_pack2(GTK_PANED(paned), r_vbox, TRUE, FALSE);

    g_free(cwd);
    g_free(default_src);
    g_free(default_dst);

    l_adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(l_scroll));
    r_adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(r_scroll));
    g_signal_connect(l_adj, "value-changed", G_CALLBACK(on_scroll_sync), r_adj);
    g_signal_connect(r_adj, "value-changed", G_CALLBACK(on_scroll_sync), l_adj);
    g_signal_connect(w->store, "sort-column-changed",
                     G_CALLBACK(on_sort_changed), l_tree);
    g_signal_connect(w->store, "sort-column-changed",
                     G_CALLBACK(on_sort_changed), r_tree);

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
    GtkListStore *store;
    GtkTreeIter iter;
    char *size_str;
    char *bg_src;
    char *bg_dst;
    char *path_src;
    char *path_dst;
    char *action_src;
    char *action_dst;
    gboolean valid;

    data = (UIUpdateData *)user_data;
    store = data->widgets->store;

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
        action_src = data->action;
        action_dst = data->action;

        if (g_strcmp0(data->action, "New") == 0) {
            bg_src = "#D4EDDA";
            bg_dst = "#FFFFFF";
            path_dst = "-";
        } else if (g_strcmp0(data->action, "Update") == 0) {
            bg_src = "#CCE5FF";
            bg_dst = "#CCE5FF";
        } else if (g_strcmp0(data->action, "Hardlink") == 0) {
            bg_src = "#E2D1F9";
            bg_dst = "#E2D1F9";
        } else if (g_strcmp0(data->action, "Delete") == 0) {
            if (data->reason && g_strrstr(data->reason, "excluded")) {
                bg_src = "#FFF3CD";
                bg_dst = "#FFF3CD";
                action_src = "Ignore";
                action_dst = "Delete";
            } else {
                bg_src = "#FFFFFF";
                bg_dst = "#F8D7DA";
                action_src = "Deleted";
                action_dst = "Delete";
                path_src = "-";
            }
        }
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter, COL_SRC_ACTION, action_src,
                           COL_DST_ACTION, action_dst, COL_SRC_PATH, path_src,
                           COL_DST_PATH, path_dst, COL_SIZE_TEXT, size_str,
                           COL_SIZE_RAW, data->size, COL_SRC_COLOR, bg_src,
                           COL_DST_COLOR, bg_dst, COL_REASON, data->reason, -1);
        g_free(size_str);
        break;
    }
    case DATA_TYPE_REMOVE_TREE_ROW: {
        valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter);
        while (valid) {
            char *row_path_src;
            char *row_path_dst;
            gtk_tree_model_get(GTK_TREE_MODEL(store), &iter, COL_SRC_PATH,
                               &row_path_src, COL_DST_PATH, &row_path_dst, -1);

            if (g_strcmp0(row_path_src, data->filepath) == 0
                || g_strcmp0(row_path_dst, data->filepath) == 0) {
                gtk_list_store_remove(store, &iter);
                g_free(row_path_src);
                g_free(row_path_dst);
                break;
            }
            g_free(row_path_src);
            g_free(row_path_dst);
            valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter);
        }
        break;
    }
    case DATA_TYPE_ENABLE_BUTTONS:
        gtk_widget_set_sensitive(data->widgets->sync_button, TRUE);
        gtk_widget_set_sensitive(data->widgets->preview_button, TRUE);
        break;
    case DATA_TYPE_CLEAR_TREES:
        gtk_list_store_clear(data->widgets->store);
        break;
    default:
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
setup_tree_columns(GtkWidget *tree, AppWidgets *w, int32 col_act,
                   int32 col_path, int32 col_color) {
    GtkCellRenderer *gtk_cell_renderer;
    GtkTreeViewColumn *gtk_tree_view_column;

    gtk_cell_renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_column = gtk_tree_view_column_new_with_attributes(
        "Action", gtk_cell_renderer, "text", col_act, "cell-background",
        col_color, NULL);
    gtk_tree_view_column_set_sort_column_id(gtk_tree_view_column, col_act);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), gtk_tree_view_column);

    gtk_tree_view_column = gtk_tree_view_column_new_with_attributes(
        "File Path", gtk_cell_renderer, "text", col_path, "cell-background",
        col_color, NULL);
    gtk_tree_view_column_set_sort_column_id(gtk_tree_view_column, col_path);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), gtk_tree_view_column);

    gtk_tree_view_column = gtk_tree_view_column_new_with_attributes(
        "Size", gtk_cell_renderer, "text", COL_SIZE_TEXT, "cell-background",
        col_color, NULL);
    gtk_tree_view_column_set_sort_column_id(gtk_tree_view_column, COL_SIZE_RAW);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), gtk_tree_view_column);

    gtk_widget_set_has_tooltip(tree, TRUE);
    g_signal_connect(tree, "query-tooltip", G_CALLBACK(on_tree_tooltip), NULL);

    g_signal_connect(tree, "button-press-event",
                     G_CALLBACK(on_tree_button_press), w);
    return;
}
