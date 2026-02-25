#include <gtk/gtk.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "util.c"
#include "hooks.c"
#include "config.c"

static void setup_tree_columns(GtkWidget *tree, int32 col_act, int32 col_path);

int32
main(int32 argc, char *argv[]) {
    GtkWidget *main_vbox;
    GtkWidget *header_vbox;
    GtkWidget *invert_btn;
    GtkWidget *btn_hbox;
    GtkWidget *filter_hbox;
    GtkWidget *options_hbox;
    GtkWidget *reset_btn;
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
    char *config_base;
    GType column_types[NUM_COLS];

    gtk_init(&argc, &argv);

    cecup_state.rows_count = 0;
    cecup_state.rows_capacity = 4096;
    cecup_state.rows = xmalloc(cecup_state.rows_capacity*SIZEOF(CecupRow *));
    cecup_state.visible_rows
        = xmalloc(cecup_state.rows_capacity*SIZEOF(CecupRow *));

    cecup_state.sort_col = COL_SRC_PATH;
    cecup_state.sort_order = GTK_SORT_ASCENDING;
    cecup_state.refresh_id = 0;

    config_base = g_build_filename(g_get_user_config_dir(), "cecup", NULL);
    g_mkdir_with_parents(config_base, 0755);
    cecup_state.exclude_path
        = g_build_filename(config_base, "exclude.conf", NULL);
    cecup_state.config_path = g_build_filename(config_base, "cecup.conf", NULL);

    cecup_state.gtk_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(cecup_state.gtk_window), "cecup");
    gtk_window_set_wmclass(GTK_WINDOW(cecup_state.gtk_window), "cecup",
                           "Cecup");
    gtk_window_set_default_size(GTK_WINDOW(cecup_state.gtk_window), 1100, 800);
    g_signal_connect(cecup_state.gtk_window, "destroy",
                     G_CALLBACK(gtk_main_quit), NULL);

    main_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(cecup_state.gtk_window), main_vbox);

    header_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(header_vbox), 10);
    gtk_box_pack_start(GTK_BOX(main_vbox), header_vbox, FALSE, FALSE, 0);

    invert_btn = gtk_button_new_with_label("<--->");
    gtk_box_pack_start(GTK_BOX(header_vbox), invert_btn, FALSE, FALSE, 0);

    btn_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    cecup_state.preview_button = gtk_button_new_with_label("1. Preview");
    cecup_state.exclude_button = gtk_button_new_with_label("Edit Exclusions");
    cecup_state.stop_button = gtk_button_new_with_label("Stop");
    cecup_state.sync_button = gtk_button_new_with_label("2. Sync");
    gtk_widget_set_sensitive(cecup_state.stop_button, FALSE);
    gtk_box_pack_start(GTK_BOX(btn_hbox), cecup_state.exclude_button, FALSE,
                       FALSE, 5);
    gtk_box_pack_end(GTK_BOX(btn_hbox), cecup_state.sync_button, FALSE, FALSE,
                     5);
    gtk_box_pack_end(GTK_BOX(btn_hbox), cecup_state.stop_button, FALSE, FALSE,
                     5);
    gtk_box_pack_end(GTK_BOX(btn_hbox), cecup_state.preview_button, FALSE,
                     FALSE, 5);
    gtk_box_pack_start(GTK_BOX(header_vbox), btn_hbox, FALSE, FALSE, 5);

    filter_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    cecup_state.filter_new = gtk_toggle_button_new_with_label("Show New");
    cecup_state.filter_hard = gtk_toggle_button_new_with_label("Show Hardlink");
    cecup_state.filter_update = gtk_toggle_button_new_with_label("Show Update");
    cecup_state.filter_equal = gtk_toggle_button_new_with_label("Show Equal");
    cecup_state.filter_delete
        = gtk_toggle_button_new_with_label("Show Deleted");
    cecup_state.filter_ignore
        = gtk_toggle_button_new_with_label("Show Ignored");

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup_state.filter_new),
                                 TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup_state.filter_hard),
                                 TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup_state.filter_update),
                                 TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup_state.filter_equal),
                                 FALSE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup_state.filter_delete),
                                 TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup_state.filter_ignore),
                                 TRUE);

    gtk_box_pack_start(GTK_BOX(filter_hbox), cecup_state.filter_new, FALSE,
                       FALSE, 2);
    gtk_box_pack_start(GTK_BOX(filter_hbox), cecup_state.filter_hard, FALSE,
                       FALSE, 2);
    gtk_box_pack_start(GTK_BOX(filter_hbox), cecup_state.filter_update, FALSE,
                       FALSE, 2);
    gtk_box_pack_start(GTK_BOX(filter_hbox), cecup_state.filter_equal, FALSE,
                       FALSE, 2);
    gtk_box_pack_start(GTK_BOX(filter_hbox), cecup_state.filter_delete, FALSE,
                       FALSE, 2);
    gtk_box_pack_start(GTK_BOX(filter_hbox), cecup_state.filter_ignore, FALSE,
                       FALSE, 2);
    gtk_box_pack_start(GTK_BOX(header_vbox), filter_hbox, FALSE, FALSE, 5);

    options_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    cecup_state.check_fs_toggle
        = gtk_check_button_new_with_label("Require different filesystems");
    cecup_state.diff_entry = gtk_entry_new();
    cecup_state.term_entry = gtk_entry_new();
    reset_btn = gtk_button_new_with_label("Reset");
    gtk_box_pack_start(GTK_BOX(options_hbox), cecup_state.check_fs_toggle,
                       FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(options_hbox), gtk_label_new("Diff Tool:"),
                       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(options_hbox), cecup_state.diff_entry, FALSE,
                       FALSE, 0);
    gtk_entry_set_text(GTK_ENTRY(cecup_state.diff_entry), "unidiff.bash");
    gtk_box_pack_start(GTK_BOX(options_hbox), gtk_label_new("Terminal:"), FALSE,
                       FALSE, 0);
    gtk_box_pack_start(GTK_BOX(options_hbox), cecup_state.term_entry, FALSE,
                       FALSE, 0);
    gtk_entry_set_text(GTK_ENTRY(cecup_state.term_entry), "xterm");
    gtk_box_pack_start(GTK_BOX(options_hbox), reset_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header_vbox), options_hbox, FALSE, FALSE, 0);

    for (int32 i = 0; i < NUM_COLS; i += 1) {
        column_types[i] = G_TYPE_INT;
    }
    cecup_state.store = gtk_list_store_newv(NUM_COLS, column_types);

    v_paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    gtk_box_pack_start(GTK_BOX(main_vbox), v_paned, TRUE, TRUE, 0);
    paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_pack1(GTK_PANED(v_paned), paned, TRUE, FALSE);

    cwd = g_get_current_dir();
    default_src = g_strdup_printf("%s/a/", cwd);
    default_dst = g_strdup_printf("%s/b/", cwd);

    l_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    cecup_state.src_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(cecup_state.src_entry), default_src);
    browse_src = gtk_button_new_with_label("Browse");
    l_entry_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(l_entry_hbox), cecup_state.src_entry, TRUE, TRUE,
                       0);
    gtk_box_pack_start(GTK_BOX(l_entry_hbox), browse_src, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(l_vbox), l_entry_hbox, FALSE, FALSE, 0);
    l_scroll = gtk_scrolled_window_new(NULL, NULL);
    l_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(cecup_state.store));
    cecup_state.l_tree = l_tree;
    g_object_set_data(G_OBJECT(l_tree), "side", GINT_TO_POINTER(0));
    setup_tree_columns(l_tree, COL_SRC_ACTION, COL_SRC_PATH);
    gtk_container_add(GTK_CONTAINER(l_scroll), l_tree);
    gtk_box_pack_start(GTK_BOX(l_vbox), l_scroll, TRUE, TRUE, 0);
    gtk_paned_pack1(GTK_PANED(paned), l_vbox, TRUE, FALSE);

    r_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    cecup_state.dst_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(cecup_state.dst_entry), default_dst);
    browse_dst = gtk_button_new_with_label("Browse");
    r_entry_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(r_entry_hbox), cecup_state.dst_entry, TRUE, TRUE,
                       0);
    gtk_box_pack_start(GTK_BOX(r_entry_hbox), browse_dst, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(r_vbox), r_entry_hbox, FALSE, FALSE, 0);
    r_scroll = gtk_scrolled_window_new(NULL, NULL);
    r_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(cecup_state.store));
    cecup_state.r_tree = r_tree;
    g_object_set_data(G_OBJECT(r_tree), "side", GINT_TO_POINTER(1));
    setup_tree_columns(r_tree, COL_DST_ACTION, COL_DST_PATH);
    gtk_container_add(GTK_CONTAINER(r_scroll), r_tree);
    gtk_box_pack_start(GTK_BOX(r_vbox), r_scroll, TRUE, TRUE, 0);
    gtk_paned_pack2(GTK_PANED(paned), r_vbox, TRUE, FALSE);

    l_adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(l_scroll));
    r_adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(r_scroll));
    g_signal_connect(l_adj, "value-changed", G_CALLBACK(on_scroll_sync), r_adj);
    g_signal_connect(r_adj, "value-changed", G_CALLBACK(on_scroll_sync), l_adj);
    g_signal_connect(cecup_state.store, "sort-column-changed",
                     G_CALLBACK(on_sort_changed), NULL);

    log_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(log_scroll, -1, 150);
    log_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(log_view), FALSE);
    cecup_state.log_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(log_view));
    gtk_container_add(GTK_CONTAINER(log_scroll), log_view);
    gtk_paned_pack2(GTK_PANED(v_paned), log_scroll, FALSE, FALSE);

    read_config();

    g_signal_connect(browse_src, "clicked", G_CALLBACK(on_browse_src), NULL);
    g_signal_connect(browse_dst, "clicked", G_CALLBACK(on_browse_dst), NULL);
    g_signal_connect(invert_btn, "clicked", G_CALLBACK(on_invert_clicked),
                     NULL);
    g_signal_connect(cecup_state.preview_button, "clicked",
                     G_CALLBACK(on_preview_clicked), NULL);
    g_signal_connect(cecup_state.stop_button, "clicked",
                     G_CALLBACK(on_stop_clicked), NULL);
    g_signal_connect(cecup_state.sync_button, "clicked",
                     G_CALLBACK(on_sync_clicked), NULL);
    g_signal_connect(cecup_state.exclude_button, "clicked",
                     G_CALLBACK(on_exclude_clicked), NULL);
    g_signal_connect(reset_btn, "clicked", G_CALLBACK(on_reset_clicked), NULL);

    g_signal_connect(cecup_state.filter_new, "toggled",
                     G_CALLBACK(on_filter_toggled), NULL);
    g_signal_connect(cecup_state.filter_hard, "toggled",
                     G_CALLBACK(on_filter_toggled), NULL);
    g_signal_connect(cecup_state.filter_update, "toggled",
                     G_CALLBACK(on_filter_toggled), NULL);
    g_signal_connect(cecup_state.filter_equal, "toggled",
                     G_CALLBACK(on_filter_toggled), NULL);
    g_signal_connect(cecup_state.filter_delete, "toggled",
                     G_CALLBACK(on_filter_toggled), NULL);
    g_signal_connect(cecup_state.filter_ignore, "toggled",
                     G_CALLBACK(on_filter_toggled), NULL);

    g_signal_connect(cecup_state.diff_entry, "changed",
                     G_CALLBACK(on_config_changed), NULL);
    g_signal_connect(cecup_state.term_entry, "changed",
                     G_CALLBACK(on_config_changed), NULL);
    g_signal_connect(cecup_state.src_entry, "changed",
                     G_CALLBACK(on_config_changed), NULL);
    g_signal_connect(cecup_state.dst_entry, "changed",
                     G_CALLBACK(on_config_changed), NULL);
    g_signal_connect(cecup_state.check_fs_toggle, "toggled",
                     G_CALLBACK(on_config_changed), NULL);

    gtk_widget_show_all(cecup_state.gtk_window);
    gtk_main();
    return 0;
}

static void
cell_data_func(GtkTreeViewColumn *col, GtkCellRenderer *renderer,
               GtkTreeModel *model, GtkTreeIter *iter, gpointer data) {
    int32 col_id;
    int32 row_idx;
    GtkTreePath *path;
    CecupRow *row;

    col_id = GPOINTER_TO_INT(data);
    path = gtk_tree_model_get_path(model, iter);
    row_idx = gtk_tree_path_get_indices(path)[0];
    gtk_tree_path_free(path);

    if (row_idx < 0 || row_idx >= cecup_state.visible_count) {
        return;
    }
    row = cecup_state.visible_rows[row_idx];

    switch (col_id) {
    case COL_SELECTED:
        g_object_set(renderer, "active", row->selected, NULL);
        break;
    case COL_SRC_ACTION:
        g_object_set(renderer, "text", action_strings[row->src_action],
                     "cell-background", row->src_color, NULL);
        break;
    case COL_DST_ACTION:
        g_object_set(renderer, "text", action_strings[row->dst_action],
                     "cell-background", row->dst_color, NULL);
        break;
    case COL_SRC_PATH:
        g_object_set(renderer, "text", row->src_path, "cell-background",
                     row->src_color, NULL);
        break;
    case COL_DST_PATH:
        g_object_set(renderer, "text", row->dst_path, "cell-background",
                     row->dst_color, NULL);
        break;
    case COL_SIZE_TEXT: {
        char *bg = (col
                    == gtk_tree_view_get_column(
                        GTK_TREE_VIEW(cecup_state.l_tree), 3))
                       ? row->src_color
                       : row->dst_color;
        g_object_set(renderer, "text", row->size_text, "cell-background", bg,
                     NULL);
        break;
    }
    }
    return;
}

static void
setup_tree_columns(GtkWidget *tree, int32 col_act, int32 col_path) {
    GtkCellRenderer *r;
    GtkTreeViewColumn *c;

    r = gtk_cell_renderer_toggle_new();
    c = gtk_tree_view_column_new();
    gtk_tree_view_column_pack_start(c, r, TRUE);
    gtk_tree_view_column_set_cell_data_func(
        c, r, cell_data_func, GINT_TO_POINTER(COL_SELECTED), NULL);
    g_signal_connect(r, "toggled", G_CALLBACK(on_cell_toggled), NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), c);

    r = gtk_cell_renderer_text_new();
    c = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(c, "Action");
    gtk_tree_view_column_pack_start(c, r, TRUE);
    gtk_tree_view_column_set_cell_data_func(c, r, cell_data_func,
                                            GINT_TO_POINTER(col_act), NULL);
    gtk_tree_view_column_set_sort_column_id(c, col_act);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), c);

    c = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(c, "File Path");
    gtk_tree_view_column_pack_start(c, r, TRUE);
    gtk_tree_view_column_set_cell_data_func(c, r, cell_data_func,
                                            GINT_TO_POINTER(col_path), NULL);
    gtk_tree_view_column_set_sort_column_id(c, col_path);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), c);

    c = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(c, "Size");
    gtk_tree_view_column_pack_start(c, r, TRUE);
    gtk_tree_view_column_set_cell_data_func(
        c, r, cell_data_func, GINT_TO_POINTER(COL_SIZE_TEXT), NULL);
    gtk_tree_view_column_set_sort_column_id(c, COL_SIZE_RAW);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), c);

    gtk_widget_set_has_tooltip(tree, TRUE);
    g_signal_connect(tree, "query-tooltip", G_CALLBACK(on_tree_tooltip), NULL);
    g_signal_connect(tree, "button-press-event",
                     G_CALLBACK(on_tree_button_press), NULL);
    return;
}

static gboolean
update_ui_handler(gpointer user_data) {
    UIUpdateData *data;

    data = (UIUpdateData *)user_data;

    switch (data->type) {
    case DATA_TYPE_LOG: {
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(cecup_state.log_buffer, &end);
        gtk_text_buffer_insert(cecup_state.log_buffer, &end, data->message, -1);
        gtk_text_buffer_insert(cecup_state.log_buffer, &end, "\n", -1);
        break;
    }
    case DATA_TYPE_TREE_ROW: {
        CecupRow *row;
        char *bg_src = "#FFFFFF";
        char *bg_dst = "#FFFFFF";
        char *src_path_final = data->filepath;
        char *dst_path_final = data->filepath;
        enum CecupAction action_src = data->action;
        enum CecupAction action_dst = data->action;

        if (data->action == UI_ACTION_NEW) {
            bg_src = "#D4EDDA";
            dst_path_final = "-";
        } else if (data->action == UI_ACTION_UPDATE) {
            bg_src = "#CCE5FF";
            bg_dst = "#CCE5FF";
        } else if (data->action == UI_ACTION_HARDLINK) {
            bg_src = "#E2D1F9";
            bg_dst = "#E2D1F9";
        } else if (data->action == UI_ACTION_EQUAL) {
            bg_src = "#F0F0F0";
            bg_dst = "#F0F0F0";
        } else if (data->action == UI_ACTION_DELETE) {
            if (data->reason == UI_REASON_EXCLUDED) {
                bg_src = "#FFF3CD";
                bg_dst = "#FFF3CD";
                action_src = UI_ACTION_IGNORE;
                action_dst = UI_ACTION_DELETE;
            } else {
                bg_dst = "#F8D7DA";
                action_src = UI_ACTION_DELETED;
                action_dst = UI_ACTION_DELETE;
                src_path_final = "-";
            }
        }

        row = g_new0(CecupRow, 1);
        row->src_action = action_src;
        row->dst_action = action_dst;
        row->size_text = bytes_pretty(data->size);
        row->size_raw = data->size;
        row->src_color = bg_src;
        row->dst_color = bg_dst;
        row->reason = data->reason;

        if (g_strcmp0(src_path_final, "-") == 0) {
            row->src_path = g_strdup("-");
            row->dst_path = data->filepath;
            data->filepath = NULL;
        } else if (g_strcmp0(dst_path_final, "-") == 0) {
            row->src_path = data->filepath;
            data->filepath = NULL;
            row->dst_path = g_strdup("-");
        } else {
            row->src_path = data->filepath;
            data->filepath = NULL;
            row->dst_path = g_strdup(row->src_path);
        }

        if (cecup_state.rows_count >= cecup_state.rows_capacity) {
            cecup_state.rows_capacity *= 2;
            cecup_state.rows
                = g_realloc(cecup_state.rows,
                            cecup_state.rows_capacity*sizeof(CecupRow *));
            cecup_state.visible_rows
                = g_realloc(cecup_state.visible_rows,
                            cecup_state.rows_capacity*sizeof(CecupRow *));
        }
        cecup_state.rows[cecup_state.rows_count] = row;
        cecup_state.rows_count += 1;

        if (cecup_state.refresh_id == 0) {
            cecup_state.refresh_id
                = g_timeout_add(200, refresh_ui_timeout_callback, NULL);
        }
        break;
    }
    case DATA_TYPE_REMOVE_TREE_ROW: {
        for (int32 i = 0; i < cecup_state.rows_count; i += 1) {
            CecupRow *row = cecup_state.rows[i];
            if (g_strcmp0(row->src_path, data->filepath) == 0
                || g_strcmp0(row->dst_path, data->filepath) == 0) {
                free_cecup_row(row);
                for (int32 j = i; j < cecup_state.rows_count - 1; j += 1) {
                    cecup_state.rows[j] = cecup_state.rows[j + 1];
                }
                cecup_state.rows_count -= 1;
                break;
            }
        }
        if (cecup_state.refresh_id == 0) {
            cecup_state.refresh_id
                = g_timeout_add(100, refresh_ui_timeout_callback, NULL);
        }
        break;
    }
    case DATA_TYPE_ENABLE_BUTTONS:
        if (cecup_state.refresh_id != 0) {
            g_source_remove(cecup_state.refresh_id);
            cecup_state.refresh_id = 0;
        }
        refresh_ui_list();
        gtk_widget_set_sensitive(cecup_state.sync_button, TRUE);
        gtk_widget_set_sensitive(cecup_state.preview_button, TRUE);
        gtk_widget_set_sensitive(cecup_state.stop_button, FALSE);
        break;
    case DATA_TYPE_CLEAR_TREES:
        if (cecup_state.refresh_id != 0) {
            g_source_remove(cecup_state.refresh_id);
            cecup_state.refresh_id = 0;
        }
        for (int32 i = 0; i < cecup_state.rows_count; i += 1) {
            free_cecup_row(cecup_state.rows[i]);
        }
        cecup_state.rows_count = 0;
        cecup_state.visible_count = 0;
        gtk_list_store_clear(cecup_state.store);
        break;
    default:
        break;
    }

    g_free(data->message);
    g_free(data->filepath);
    g_free(data);
    return G_SOURCE_REMOVE;
}
