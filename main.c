#include <gtk/gtk.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "util.c"
#include "hooks.c"

static void setup_tree_columns(GtkWidget *tree, AppWidgets *w, int32 col_act,
                               int32 col_path, int32 col_color);

static gboolean
is_row_visible(GtkTreeModel *model, GtkTreeIter *iter, gpointer data) {
    AppWidgets *w;
    char *action_src;
    gboolean visible;

    w = (AppWidgets *)data;
    visible = FALSE;
    gtk_tree_model_get(model, iter, COL_SRC_ACTION, &action_src, -1);

    if (g_strcmp0(action_src, "New") == 0) {
        visible
            = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->filter_new));
    } else if (g_strcmp0(action_src, "Hardlink") == 0) {
        visible
            = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->filter_hard));
    } else if (g_strcmp0(action_src, "Update") == 0) {
        visible = TRUE;
    } else if (g_strcmp0(action_src, "Deleted") == 0) {
        visible
            = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->filter_delete));
    } else if (g_strcmp0(action_src, "Ignore") == 0) {
        visible
            = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->filter_ignore));
    } else if (g_strcmp0(action_src, "Equal") == 0) {
        visible
            = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->filter_equal));
    }

    g_free(action_src);
    return visible;
}

int32
main(int32 argc, char *argv[]) {
    AppWidgets *w;
    GtkWidget *main_vbox;
    GtkWidget *header_vbox;
    GtkWidget *invert_btn;
    GtkWidget *btn_hbox;
    GtkWidget *filter_hbox;
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
    GtkTreeModel *sort_model;
    char *cwd;
    char *default_src;
    char *default_dst;
    char *config_base;

    gtk_init(&argc, &argv);
    w = g_new0(AppWidgets, 1);

    config_base = g_build_filename(g_get_user_config_dir(), "cecup", NULL);
    g_mkdir_with_parents(config_base, 0755);
    w->exclude_path = g_build_filename(config_base, "exclude.conf", NULL);
    w->config_path = g_build_filename(config_base, "cecup.conf", NULL);

    w->gtk_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(w->gtk_window), "cecup");
    gtk_window_set_default_size(GTK_WINDOW(w->gtk_window), 1100, 800);
    g_signal_connect(w->gtk_window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    main_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(w->gtk_window), main_vbox);

    header_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(header_vbox), 10);
    gtk_box_pack_start(GTK_BOX(main_vbox), header_vbox, FALSE, FALSE, 0);

    invert_btn = gtk_button_new_with_label("<--->");
    gtk_box_pack_start(GTK_BOX(header_vbox), invert_btn, FALSE, FALSE, 0);

    btn_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    w->preview_button = gtk_button_new_with_label("1. Preview");
    w->exclude_button = gtk_button_new_with_label("Edit Exclusions");
    w->sync_button = gtk_button_new_with_label("2. Sync");
    gtk_box_pack_start(GTK_BOX(btn_hbox), w->exclude_button, FALSE, FALSE, 5);
    gtk_box_pack_end(GTK_BOX(btn_hbox), w->sync_button, FALSE, FALSE, 5);
    gtk_box_pack_end(GTK_BOX(btn_hbox), w->preview_button, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(header_vbox), btn_hbox, FALSE, FALSE, 5);

    filter_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    w->filter_new = gtk_toggle_button_new_with_label("Show New");
    w->filter_hard = gtk_toggle_button_new_with_label("Show Hardlink");
    w->filter_equal = gtk_toggle_button_new_with_label("Show Equal");
    w->filter_delete = gtk_toggle_button_new_with_label("Show Deleted");
    w->filter_ignore = gtk_toggle_button_new_with_label("Show Ignored");

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->filter_new), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->filter_hard), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->filter_equal), FALSE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->filter_delete), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->filter_ignore), TRUE);

    gtk_box_pack_start(GTK_BOX(filter_hbox), w->filter_new, FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(filter_hbox), w->filter_hard, FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(filter_hbox), w->filter_equal, FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(filter_hbox), w->filter_delete, FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(filter_hbox), w->filter_ignore, FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(header_vbox), filter_hbox, FALSE, FALSE, 5);

    options_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    w->check_fs_toggle
        = gtk_check_button_new_with_label("Require different filesystems");
    w->diff_entry = gtk_entry_new();
    w->term_entry = gtk_entry_new();
    gtk_box_pack_start(GTK_BOX(options_hbox), w->check_fs_toggle, FALSE, FALSE,
                       5);
    gtk_box_pack_start(GTK_BOX(options_hbox), gtk_label_new("Diff Tool:"),
                       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(options_hbox), w->diff_entry, FALSE, FALSE, 0);
    gtk_entry_set_text(GTK_ENTRY(w->diff_entry), "unidiff.bash");
    gtk_box_pack_start(GTK_BOX(options_hbox), gtk_label_new("Terminal:"), FALSE,
                       FALSE, 0);
    gtk_box_pack_start(GTK_BOX(options_hbox), w->term_entry, FALSE, FALSE, 0);
    gtk_entry_set_text(GTK_ENTRY(w->term_entry), "xterm");

    gtk_box_pack_start(GTK_BOX(header_vbox), options_hbox, FALSE, FALSE, 0);

    w->store = gtk_list_store_new(NUM_COLS, G_TYPE_BOOLEAN, G_TYPE_STRING,
                                  G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                                  G_TYPE_STRING, G_TYPE_INT64, G_TYPE_STRING,
                                  G_TYPE_STRING, G_TYPE_STRING);

    w->filter_model = GTK_TREE_MODEL_FILTER(
        gtk_tree_model_filter_new(GTK_TREE_MODEL(w->store), NULL));
    gtk_tree_model_filter_set_visible_func(w->filter_model, is_row_visible, w,
                                           NULL);

    sort_model
        = gtk_tree_model_sort_new_with_model(GTK_TREE_MODEL(w->filter_model));
    w->sort_model = GTK_TREE_MODEL_SORT(sort_model);

    g_signal_connect(w->filter_new, "toggled", G_CALLBACK(on_filter_toggled),
                     w);
    g_signal_connect(w->filter_hard, "toggled", G_CALLBACK(on_filter_toggled),
                     w);
    g_signal_connect(w->filter_equal, "toggled", G_CALLBACK(on_filter_toggled),
                     w);
    g_signal_connect(w->filter_delete, "toggled", G_CALLBACK(on_filter_toggled),
                     w);
    g_signal_connect(w->filter_ignore, "toggled", G_CALLBACK(on_filter_toggled),
                     w);

    v_paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    gtk_box_pack_start(GTK_BOX(main_vbox), v_paned, TRUE, TRUE, 0);
    paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_pack1(GTK_PANED(v_paned), paned, TRUE, FALSE);

    cwd = g_get_current_dir();
    default_src = g_strdup_printf("%s/a/", cwd);
    default_dst = g_strdup_printf("%s/b/", cwd);

    l_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    w->src_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(w->src_entry), default_src);
    browse_src = gtk_button_new_with_label("Browse");
    l_entry_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(l_entry_hbox), w->src_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(l_entry_hbox), browse_src, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(l_vbox), l_entry_hbox, FALSE, FALSE, 0);
    l_scroll = gtk_scrolled_window_new(NULL, NULL);
    l_tree = gtk_tree_view_new_with_model(sort_model);
    g_object_set_data(G_OBJECT(l_tree), "side", GINT_TO_POINTER(0));
    setup_tree_columns(l_tree, w, COL_SRC_ACTION, COL_SRC_PATH, COL_SRC_COLOR);
    gtk_container_add(GTK_CONTAINER(l_scroll), l_tree);
    gtk_box_pack_start(GTK_BOX(l_vbox), l_scroll, TRUE, TRUE, 0);
    gtk_paned_pack1(GTK_PANED(paned), l_vbox, TRUE, FALSE);

    r_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    w->dst_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(w->dst_entry), default_dst);
    browse_dst = gtk_button_new_with_label("Browse");
    r_entry_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(r_entry_hbox), w->dst_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(r_entry_hbox), browse_dst, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(r_vbox), r_entry_hbox, FALSE, FALSE, 0);
    r_scroll = gtk_scrolled_window_new(NULL, NULL);
    r_tree = gtk_tree_view_new_with_model(sort_model);
    g_object_set_data(G_OBJECT(r_tree), "side", GINT_TO_POINTER(1));
    setup_tree_columns(r_tree, w, COL_DST_ACTION, COL_DST_PATH, COL_DST_COLOR);
    gtk_container_add(GTK_CONTAINER(r_scroll), r_tree);
    gtk_box_pack_start(GTK_BOX(r_vbox), r_scroll, TRUE, TRUE, 0);
    gtk_paned_pack2(GTK_PANED(paned), r_vbox, TRUE, FALSE);

    l_adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(l_scroll));
    r_adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(r_scroll));
    g_signal_connect(l_adj, "value-changed", G_CALLBACK(on_scroll_sync), r_adj);
    g_signal_connect(r_adj, "value-changed", G_CALLBACK(on_scroll_sync), l_adj);
    g_signal_connect(sort_model, "sort-column-changed",
                     G_CALLBACK(on_sort_changed), l_tree);
    g_signal_connect(sort_model, "sort-column-changed",
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
    gtk_main();
    return 0;
}

static gboolean
update_ui_handler(gpointer user_data) {
    UIUpdateData *data;
    GtkListStore *store;

    data = (UIUpdateData *)user_data;
    store = data->widgets->store;

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
        GtkTreeIter iter;
        char *sz_str;
        char *bg_src;
        char *bg_dst;
        char *path_src;
        char *path_dst;
        char *act_src;
        char *act_dst;

        sz_str = bytes_pretty(data->size);
        bg_src = "#FFFFFF";
        bg_dst = "#FFFFFF";
        path_src = data->filepath;
        path_dst = data->filepath;
        act_src = data->action;
        act_dst = data->action;

        if (g_strcmp0(data->action, "New") == 0) {
            bg_src = "#D4EDDA";
            path_dst = "-";
        } else if (g_strcmp0(data->action, "Update") == 0) {
            bg_src = "#CCE5FF";
            bg_dst = "#CCE5FF";
        } else if (g_strcmp0(data->action, "Hardlink") == 0) {
            bg_src = "#E2D1F9";
            bg_dst = "#E2D1F9";
        } else if (g_strcmp0(data->action, "Equal") == 0) {
            bg_src = "#F0F0F0";
            bg_dst = "#F0F0F0";
        } else if (g_strcmp0(data->action, "Delete") == 0) {
            char *rl;

            rl = g_utf8_strdown(data->reason ? data->reason : "", -1);
            if (g_strrstr(rl, "exclude")) {
                bg_src = "#FFF3CD";
                bg_dst = "#FFF3CD";
                act_src = "Ignore";
                act_dst = "Delete";
            } else {
                bg_dst = "#F8D7DA";
                act_src = "Deleted";
                act_dst = "Delete";
                path_src = "-";
            }
            g_free(rl);
        }

        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter, COL_SELECTED, FALSE, COL_SRC_ACTION,
                           act_src, COL_DST_ACTION, act_dst, COL_SRC_PATH,
                           path_src, COL_DST_PATH, path_dst, COL_SIZE_TEXT,
                           sz_str, COL_SIZE_RAW, data->size, COL_SRC_COLOR,
                           bg_src, COL_DST_COLOR, bg_dst, COL_REASON,
                           data->reason, -1);
        g_free(sz_str);
        break;
    }
    case DATA_TYPE_REMOVE_TREE_ROW: {
        GtkTreeIter iter;
        gboolean valid;

        valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter);
        while (valid) {
            char *ps;
            char *pd;

            gtk_tree_model_get(GTK_TREE_MODEL(store), &iter, COL_SRC_PATH, &ps,
                               COL_DST_PATH, &pd, -1);
            if (g_strcmp0(ps, data->filepath) == 0
                || g_strcmp0(pd, data->filepath) == 0) {
                gtk_list_store_remove(store, &iter);
                g_free(ps);
                g_free(pd);
                break;
            }
            g_free(ps);
            g_free(pd);
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
        break;
    }

    g_free(data->message);
    g_free(data->action);
    g_free(data->filepath);
    g_free(data->reason);
    g_free(data);
    return G_SOURCE_REMOVE;
}

static void
setup_tree_columns(GtkWidget *tree, AppWidgets *w, int32 col_act,
                   int32 col_path, int32 col_color) {
    GtkCellRenderer *r;
    GtkTreeViewColumn *c;

    r = gtk_cell_renderer_toggle_new();
    c = gtk_tree_view_column_new_with_attributes("", r, "active", COL_SELECTED,
                                                 NULL);
    g_signal_connect(r, "toggled", G_CALLBACK(on_cell_toggled), w);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), c);

    r = gtk_cell_renderer_text_new();
    c = gtk_tree_view_column_new_with_attributes(
        "Action", r, "text", col_act, "cell-background", col_color, NULL);
    gtk_tree_view_column_set_sort_column_id(c, col_act);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), c);

    c = gtk_tree_view_column_new_with_attributes(
        "File Path", r, "text", col_path, "cell-background", col_color, NULL);
    gtk_tree_view_column_set_sort_column_id(c, col_path);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), c);

    c = gtk_tree_view_column_new_with_attributes(
        "Size", r, "text", COL_SIZE_TEXT, "cell-background", col_color, NULL);
    gtk_tree_view_column_set_sort_column_id(c, COL_SIZE_RAW);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), c);

    gtk_widget_set_has_tooltip(tree, TRUE);
    g_signal_connect(tree, "query-tooltip", G_CALLBACK(on_tree_tooltip), NULL);
    g_signal_connect(tree, "button-press-event",
                     G_CALLBACK(on_tree_button_press), w);
    return;
}
