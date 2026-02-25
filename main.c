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
    GtkWidget *progress_vbox;
    GtkWidget *paths_hbox;
    GtkAdjustment *l_adj;
    GtkAdjustment *r_adj;
    char *cwd;
    char *default_src;
    char *default_dst;
    char config_base[MAX_PATH_LENGTH];
    char *user_config_dir;
    GType column_types[NUM_COLS];
    char source_path_buffer[4096];
    int64 source_path_length;
    char destination_path_buffer[4096];
    int64 destination_path_length;

    gtk_init(&argc, &argv);

    cecup_state.row_arena = arena_create(SIZEMB(64));
    cecup_state.ui_arena = arena_create(SIZEMB(16));
    g_mutex_init(&cecup_state.ui_arena_mutex);

    cecup_state.rows_count = 0;
    cecup_state.rows_capacity = 4096;
    cecup_state.rows = arena_push(
        cecup_state.row_arena, cecup_state.rows_capacity*SIZEOF(CecupRow *));
    cecup_state.visible_rows = arena_push(
        cecup_state.row_arena, cecup_state.rows_capacity*SIZEOF(CecupRow *));

    cecup_state.sort_col = COL_SRC_PATH;
    cecup_state.sort_order = GTK_SORT_ASCENDING;
    cecup_state.refresh_id = 0;

    user_config_dir = (char *)g_get_user_config_dir();
    SNPRINTF(config_base, "%s/cecup", user_config_dir);

    if (access(config_base, F_OK) == -1) {
        char cmd[4096];
        g_mkdir_with_parents(config_base, 0755);

        SNPRINTF(cmd, "cp -r /etc/cecup/* '%s/'", config_base);
        system(cmd);
    }

    SNPRINTF(cecup_state.ignore_path, "%s/ignore.conf", config_base);
    SNPRINTF(cecup_state.config_path, "%s/cecup.conf", config_base);

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

    btn_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    cecup_state.preview_button = gtk_button_new_with_label("🔎 Preview");
    gtk_widget_set_tooltip_text(cecup_state.preview_button,
                                "Run rsync --dry-run to identify changes");
    cecup_state.ignore_button = gtk_button_new_with_label("Edit Ignore List");
    gtk_widget_set_tooltip_text(cecup_state.ignore_button,
                                "Modify ignore.conf");
    cecup_state.fix_button = gtk_button_new_with_label("🛠️ Fix FS");
    gtk_widget_set_tooltip_text(cecup_state.fix_button,
                                "Run external tool to fix filename issues");
    cecup_state.stop_button = gtk_button_new_with_label("Stop");
    gtk_widget_set_tooltip_text(cecup_state.stop_button,
                                "Cancel current operation");
    cecup_state.sync_button = gtk_button_new_with_label("⏩ Sync");
    gtk_widget_set_tooltip_text(cecup_state.sync_button,
                                "Apply all selected changes");
    gtk_widget_set_sensitive(cecup_state.stop_button, FALSE);
    gtk_box_pack_start(GTK_BOX(btn_hbox), cecup_state.ignore_button, FALSE,
                       FALSE, 5);
    gtk_box_pack_start(GTK_BOX(btn_hbox), cecup_state.fix_button, FALSE, FALSE,
                       5);
    gtk_box_pack_end(GTK_BOX(btn_hbox), cecup_state.sync_button, FALSE, FALSE,
                     5);
    gtk_box_pack_end(GTK_BOX(btn_hbox), cecup_state.stop_button, FALSE, FALSE,
                     5);
    gtk_box_pack_end(GTK_BOX(btn_hbox), cecup_state.preview_button, FALSE,
                     FALSE, 5);
    gtk_box_pack_start(GTK_BOX(header_vbox), btn_hbox, FALSE, FALSE, 5);

    options_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    cecup_state.check_fs_toggle
        = gtk_check_button_new_with_label("Require different filesystems");
    gtk_widget_set_tooltip_text(
        cecup_state.check_fs_toggle,
        "Block sync if source and destination are on the same device");
    cecup_state.check_equal_toggle
        = gtk_check_button_new_with_label("Scan for equal files");
    gtk_widget_set_tooltip_text(
        cecup_state.check_equal_toggle,
        "Perform parallel directory scan to find identical files");
    gtk_toggle_button_set_active(
        GTK_TOGGLE_BUTTON(cecup_state.check_equal_toggle), TRUE);
    cecup_state.diff_entry = gtk_entry_new();
    gtk_widget_set_tooltip_text(cecup_state.diff_entry,
                                "Executable used for comparing files");
    cecup_state.term_entry = gtk_entry_new();
    gtk_widget_set_tooltip_text(
        cecup_state.term_entry,
        "Terminal emulator used to launch the diff tool");
    reset_btn = gtk_button_new_with_label("Reset");
    gtk_widget_set_tooltip_text(reset_btn, "Restore default settings");
    gtk_box_pack_start(GTK_BOX(options_hbox), cecup_state.check_fs_toggle,
                       FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(options_hbox), cecup_state.check_equal_toggle,
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

    progress_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    cecup_state.progress_rsync = gtk_progress_bar_new();
    gtk_widget_set_tooltip_text(cecup_state.progress_rsync,
                                "Rsync transfer progress");
    cecup_state.progress_equal = gtk_progress_bar_new();
    gtk_widget_set_tooltip_text(cecup_state.progress_equal,
                                "Equality scanner progress");
    cecup_state.progress_preview = gtk_progress_bar_new();
    gtk_widget_set_tooltip_text(cecup_state.progress_preview,
                                "Preview analysis progress");

    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(cecup_state.progress_rsync),
                                   TRUE);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(cecup_state.progress_rsync),
                              "rsync");
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(cecup_state.progress_equal),
                                   TRUE);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(cecup_state.progress_equal),
                              "equal scanner");
    gtk_progress_bar_set_show_text(
        GTK_PROGRESS_BAR(cecup_state.progress_preview), TRUE);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(cecup_state.progress_preview),
                              "preview analysis");

    gtk_box_pack_start(GTK_BOX(progress_vbox), cecup_state.progress_rsync,
                       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(progress_vbox), cecup_state.progress_equal,
                       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(progress_vbox), cecup_state.progress_preview,
                       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header_vbox), progress_vbox, FALSE, FALSE, 5);

    cwd = (char *)g_get_current_dir();

    source_path_length = SNPRINTF(source_path_buffer, "%s/a/", cwd);
    default_src = arena_push(cecup_state.ui_arena, source_path_length + 1);
    memcpy64(default_src, source_path_buffer, source_path_length + 1);

    destination_path_length = SNPRINTF(destination_path_buffer, "%s/b/", cwd);
    default_dst = arena_push(cecup_state.ui_arena, destination_path_length + 1);
    memcpy64(default_dst, destination_path_buffer, destination_path_length + 1);

    paths_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(paths_hbox), 10);
    gtk_box_pack_start(GTK_BOX(main_vbox), paths_hbox, FALSE, FALSE, 0);

    l_entry_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    cecup_state.src_entry = gtk_entry_new();
    gtk_widget_set_tooltip_text(cecup_state.src_entry,
                                "Base source directory path");
    gtk_entry_set_text(GTK_ENTRY(cecup_state.src_entry), default_src);
    browse_src = gtk_button_new_with_label("Browse");
    gtk_box_pack_start(GTK_BOX(l_entry_hbox), cecup_state.src_entry, TRUE, TRUE,
                       0);
    gtk_box_pack_start(GTK_BOX(l_entry_hbox), browse_src, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(paths_hbox), l_entry_hbox, TRUE, TRUE, 0);

    invert_btn = gtk_button_new_with_label("<--->");
    gtk_widget_set_tooltip_text(invert_btn,
                                "Swap Source and Destination paths");
    gtk_box_pack_start(GTK_BOX(paths_hbox), invert_btn, FALSE, FALSE, 0);

    r_entry_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    cecup_state.dst_entry = gtk_entry_new();
    gtk_widget_set_tooltip_text(cecup_state.dst_entry,
                                "Base destination directory path");
    gtk_entry_set_text(GTK_ENTRY(cecup_state.dst_entry), default_dst);
    browse_dst = gtk_button_new_with_label("Browse");
    gtk_box_pack_start(GTK_BOX(r_entry_hbox), cecup_state.dst_entry, TRUE, TRUE,
                       0);
    gtk_box_pack_start(GTK_BOX(r_entry_hbox), browse_dst, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(paths_hbox), r_entry_hbox, TRUE, TRUE, 0);

    for (int32 i = 0; i < NUM_COLS; i += 1) {
        column_types[i] = G_TYPE_INT;
    }
    cecup_state.store = gtk_list_store_newv(NUM_COLS, column_types);

    v_paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    gtk_box_pack_start(GTK_BOX(main_vbox), v_paned, TRUE, TRUE, 0);
    paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_pack1(GTK_PANED(v_paned), paned, TRUE, FALSE);

    l_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    l_scroll = gtk_scrolled_window_new(NULL, NULL);
    l_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(cecup_state.store));
    cecup_state.l_tree = l_tree;
    g_object_set_data(G_OBJECT(l_tree), "side", GINT_TO_POINTER(0));
    setup_tree_columns(l_tree, COL_SRC_ACTION, COL_SRC_PATH);
    gtk_container_add(GTK_CONTAINER(l_scroll), l_tree);
    gtk_box_pack_start(GTK_BOX(l_vbox), l_scroll, TRUE, TRUE, 0);
    gtk_paned_pack1(GTK_PANED(paned), l_vbox, TRUE, FALSE);

    r_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
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

    filter_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_set_halign(filter_hbox, GTK_ALIGN_CENTER);
    cecup_state.filter_new = gtk_toggle_button_new_with_label(EMOJI_NEW);
    cecup_state.filter_hard = gtk_toggle_button_new_with_label(EMOJI_LINK);
    cecup_state.filter_update = gtk_toggle_button_new_with_label(EMOJI_UPDATE);
    cecup_state.filter_equal = gtk_toggle_button_new_with_label(EMOJI_EQUAL);
    cecup_state.filter_delete = gtk_toggle_button_new_with_label(EMOJI_DELETE);
    cecup_state.filter_ignore = gtk_toggle_button_new_with_label(EMOJI_IGNORE);

    gtk_widget_set_tooltip_text(cecup_state.filter_new, "Show New Files");
    gtk_widget_set_tooltip_text(cecup_state.filter_hard, "Show Hardlinks");
    gtk_widget_set_tooltip_text(cecup_state.filter_update, "Show Updates");
    gtk_widget_set_tooltip_text(cecup_state.filter_equal, "Show Identical");
    gtk_widget_set_tooltip_text(cecup_state.filter_delete, "Show Deletions");
    gtk_widget_set_tooltip_text(cecup_state.filter_ignore, "Show Ignored");

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
    gtk_box_pack_start(GTK_BOX(main_vbox), filter_hbox, FALSE, FALSE, 0);

    cecup_state.stats_label = gtk_label_new("✅ Ready");
    gtk_box_pack_start(GTK_BOX(main_vbox), cecup_state.stats_label, FALSE,
                       FALSE, 5);

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
    g_signal_connect(cecup_state.ignore_button, "clicked",
                     G_CALLBACK(on_ignore_clicked), NULL);
    g_signal_connect(cecup_state.fix_button, "clicked",
                     G_CALLBACK(on_fix_clicked), NULL);
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
    g_signal_connect(cecup_state.check_equal_toggle, "toggled",
                     G_CALLBACK(on_config_changed), NULL);

    gtk_widget_show_all(cecup_state.gtk_window);
    gtk_main();

    arena_destroy(cecup_state.row_arena);
    arena_destroy(cecup_state.ui_arena);
    g_mutex_clear(&cecup_state.ui_arena_mutex);
    exit(EXIT_SUCCESS);
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

    if ((row_idx < 0) || (row_idx >= cecup_state.visible_count)) {
        return;
    }
    row = cecup_state.visible_rows[row_idx];

    switch (col_id) {
    case COL_SELECTED:
        g_object_set(renderer, "active", row->selected, NULL);
        break;
    case COL_SRC_ACTION:
        g_object_set(renderer, "text", action_emojis[row->src_action],
                     "cell-background", row->src_color, NULL);
        break;
    case COL_DST_ACTION:
        g_object_set(renderer, "text", action_emojis[row->dst_action],
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
        char *background;
        if (col
            == gtk_tree_view_get_column(GTK_TREE_VIEW(cecup_state.l_tree), 3)) {
            background = row->src_color;
        } else {
            background = row->dst_color;
        }
        g_object_set(renderer, "text", row->size_text, "cell-background",
                     background, NULL);
        break;
    }
    default:
        error("Invalid col_id = %d\n", col_id);
        exit(EXIT_FAILURE);
    }
    return;
}

static void
setup_tree_columns(GtkWidget *tree, int32 col_act, int32 col_path) {
    GtkCellRenderer *renderer_toggle;
    GtkCellRenderer *renderer_text;
    GtkTreeViewColumn *column;

    renderer_toggle = gtk_cell_renderer_toggle_new();
    column = gtk_tree_view_column_new();
    gtk_tree_view_column_pack_start(column, renderer_toggle, TRUE);
    gtk_tree_view_column_set_cell_data_func(
        column, renderer_toggle, cell_data_func, GINT_TO_POINTER(COL_SELECTED),
        NULL);
    g_signal_connect(renderer_toggle, "toggled", G_CALLBACK(on_cell_toggled),
                     NULL);
    gtk_tree_view_column_set_resizable(column, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);

    renderer_text = gtk_cell_renderer_text_new();
    g_object_set(renderer_text, "ellipsize", PANGO_ELLIPSIZE_END, NULL);

    column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(column, "Action");
    gtk_tree_view_column_pack_start(column, renderer_text, TRUE);
    gtk_tree_view_column_set_cell_data_func(
        column, renderer_text, cell_data_func, GINT_TO_POINTER(col_act), NULL);
    gtk_tree_view_column_set_sort_column_id(column, col_act);
    gtk_tree_view_column_set_resizable(column, TRUE);
    gtk_tree_view_column_set_min_width(column, 80);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);

    column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(column, "File Path");
    gtk_tree_view_column_pack_start(column, renderer_text, TRUE);
    gtk_tree_view_column_set_cell_data_func(
        column, renderer_text, cell_data_func, GINT_TO_POINTER(col_path), NULL);
    gtk_tree_view_column_set_sort_column_id(column, col_path);
    gtk_tree_view_column_set_resizable(column, TRUE);
    gtk_tree_view_column_set_expand(column, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);

    column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(column, "Size");
    gtk_tree_view_column_pack_start(column, renderer_text, TRUE);
    gtk_tree_view_column_set_cell_data_func(
        column, renderer_text, cell_data_func, GINT_TO_POINTER(COL_SIZE_TEXT),
        NULL);
    gtk_tree_view_column_set_sort_column_id(column, COL_SIZE_RAW);
    gtk_tree_view_column_set_resizable(column, TRUE);
    gtk_tree_view_column_set_min_width(column, 100);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);

    gtk_widget_set_has_tooltip(tree, TRUE);
    g_signal_connect(tree, "query-tooltip", G_CALLBACK(on_tree_tooltip), NULL);
    g_signal_connect(tree, "button-press-event",
                     G_CALLBACK(on_tree_button_press), NULL);
    return;
}
