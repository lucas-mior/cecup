/*
 * Copyright (C) 2025 Mior, Lucas;
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the*License,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <gtk/gtk.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "util.c"
#include "hooks.c"
#include "config.c"

#define BUTTON_PADDING 5
#define LABEL_PADDING 0

static void setup_tree_columns(GtkWidget *tree, int32 col_act, int32 col_path);

int32
main(int32 argc, char *argv[]) {
    GtkWidget *main_vbox;
    GtkWidget *header_vbox;
    GtkWidget *invert_button;
    GtkWidget *button_hbox;
    GtkWidget *filter_hbox;
    GtkWidget *options_hbox;
    GtkWidget *reset_button;
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

    char cwd[MAX_PATH_LENGTH];
    char xdg_buffer[MAX_PATH_LENGTH];
    char config_base[MAX_PATH_LENGTH];
    char *XDG_CONFIG_HOME;

    char *default_src;
    char *default_dst;

    GType column_types[NUM_COLS];
    char source_path_buffer[4096];
    int64 source_path_length;
    char destination_path_buffer[4096];
    int64 destination_path_length;

    int64 rows_size;

    gtk_init(&argc, &argv);

    cecup.row_arena = arena_create(SIZEMB(64));
    cecup.ui_arena = arena_create(SIZEMB(16));
    g_mutex_init(&cecup.ui_arena_mutex);

    cecup.rows_count = 0;
    cecup.rows_capacity = 4096;

    rows_size = cecup.rows_capacity*SIZEOF(CecupRow *);
    cecup.rows = xarena_push(cecup.row_arena, rows_size);
    cecup.visible_rows = xarena_push(cecup.row_arena, rows_size);

    cecup.sort_col = COL_SRC_PATH;
    cecup.sort_order = GTK_SORT_ASCENDING;
    cecup.refresh_id = 0;

    if ((XDG_CONFIG_HOME = getenv("XDG_CONFIG_HOME")) == NULL) {
        char *HOME;
        if ((HOME = getenv("HOME")) == NULL) {
            error("HOME is not defined. Fix your system.\n");
            exit(EXIT_FAILURE);
        }
        SNPRINTF(xdg_buffer, "%s/.config", HOME);
        XDG_CONFIG_HOME = xdg_buffer;
    }
    SNPRINTF(config_base, "%s/cecup", XDG_CONFIG_HOME);

    if (access(config_base, F_OK) == -1) {
        char cmd[4096];
        g_mkdir_with_parents(config_base, 0755);

        SNPRINTF(cmd, "cp -r /etc/cecup/* '%s/'", config_base);
        system(cmd);
    }

    SNPRINTF(cecup.ignore_path, "%s/ignore.conf", config_base);
    SNPRINTF(cecup.config_path, "%s/cecup.conf", config_base);

    // clang-format off
    cecup.gtk_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(cecup.gtk_window), "cecup");
    gtk_window_set_wmclass(GTK_WINDOW(cecup.gtk_window),
                           "cecup", "Cecup");
    gtk_window_set_default_size(GTK_WINDOW(cecup.gtk_window), 1100, 800);
    g_signal_connect(cecup.gtk_window,
                     "destroy", G_CALLBACK(gtk_main_quit), NULL);
    // clang-format on

    main_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(cecup.gtk_window), main_vbox);

    header_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(header_vbox), 10);
    gtk_box_pack_start(GTK_BOX(main_vbox), header_vbox, FALSE, FALSE, 0);

    button_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    cecup.preview_button = gtk_button_new_with_label("🔎 Preview");
    gtk_widget_set_tooltip_text(cecup.preview_button,
                                "Run rsync --dry-run to identify changes");
    cecup.ignore_button = gtk_button_new_with_label("Edit Ignore List");
    gtk_widget_set_tooltip_text(
        cecup.ignore_button, "Edit file with patterns of filenames to ignore");
    cecup.fix_button = gtk_button_new_with_label("🛠️ Fix FS");
    gtk_widget_set_tooltip_text(cecup.fix_button,
                                "Find and rename buggy filenames");
    cecup.stop_button = gtk_button_new_with_label("Stop");
    gtk_widget_set_tooltip_text(cecup.stop_button, "Cancel current operation");
    cecup.sync_button = gtk_button_new_with_label("⏩ Sync");
    gtk_widget_set_tooltip_text(cecup.sync_button,
                                "Apply all selected changes");
    gtk_widget_set_sensitive(cecup.stop_button, FALSE);

    // clang-format off
    gtk_box_pack_start(GTK_BOX(button_hbox), cecup.ignore_button,
                       FALSE, FALSE, BUTTON_PADDING);
    gtk_box_pack_start(GTK_BOX(button_hbox), cecup.fix_button,
                       FALSE, FALSE, BUTTON_PADDING);
    gtk_box_pack_end(GTK_BOX(button_hbox), cecup.sync_button,
                     FALSE, FALSE, BUTTON_PADDING);
    gtk_box_pack_end(GTK_BOX(button_hbox), cecup.stop_button,
                     FALSE, FALSE, BUTTON_PADDING);
    gtk_box_pack_end(GTK_BOX(button_hbox), cecup.preview_button,
                     FALSE, FALSE, BUTTON_PADDING);
    gtk_box_pack_start(GTK_BOX(header_vbox), button_hbox,
                       FALSE, FALSE, BUTTON_PADDING);
    // clang-format on

    options_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    cecup.check_fs
        = gtk_check_button_new_with_label("Require different filesystems");
    gtk_widget_set_tooltip_text(cecup.check_fs,
                                "Block sync if source and destination"
                                " are on the same device (file system)");
    cecup.check_equal = gtk_check_button_new_with_label("Scan for equal files");
    gtk_widget_set_tooltip_text(cecup.check_equal,
                                "Perform parallel directory scan"
                                " to find identical files");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup.check_equal), TRUE);
    cecup.delete_excluded = gtk_check_button_new_with_label("Delete ignored");
    gtk_widget_set_tooltip_text(cecup.delete_excluded,
                                "Delete ignored files on destination");
    cecup.diff_entry = gtk_entry_new();
    gtk_widget_set_tooltip_text(cecup.diff_entry,
                                "Executable used for comparing files");
    cecup.term_entry = gtk_entry_new();
    gtk_widget_set_tooltip_text(
        cecup.term_entry, "Terminal emulator used to launch the diff tool");
    reset_button = gtk_button_new_with_label("Reset");
    gtk_widget_set_tooltip_text(reset_button, "Restore default settings");
    gtk_box_pack_start(GTK_BOX(options_hbox), cecup.check_fs, FALSE, FALSE,
                       BUTTON_PADDING);
    gtk_box_pack_start(GTK_BOX(options_hbox), cecup.check_equal, FALSE, FALSE,
                       BUTTON_PADDING);
    gtk_box_pack_start(GTK_BOX(options_hbox), cecup.delete_excluded, FALSE,
                       FALSE, BUTTON_PADDING);
    gtk_box_pack_start(GTK_BOX(options_hbox), gtk_label_new("Diff Tool:"),
                       FALSE, FALSE, LABEL_PADDING);
    gtk_box_pack_start(GTK_BOX(options_hbox), cecup.diff_entry, FALSE, FALSE,
                       LABEL_PADDING);
    gtk_entry_set_text(GTK_ENTRY(cecup.diff_entry), "unidiff.bash");
    gtk_box_pack_start(GTK_BOX(options_hbox), gtk_label_new("Terminal:"), FALSE,
                       FALSE, LABEL_PADDING);
    gtk_box_pack_start(GTK_BOX(options_hbox), cecup.term_entry, FALSE, FALSE,
                       LABEL_PADDING);
    gtk_entry_set_text(GTK_ENTRY(cecup.term_entry), "xterm");
    gtk_box_pack_start(GTK_BOX(options_hbox), reset_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header_vbox), options_hbox, FALSE, FALSE, 0);

    progress_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    cecup.progress_rsync = gtk_progress_bar_new();
    gtk_widget_set_tooltip_text(cecup.progress_rsync,
                                "Rsync transfer progress");
    cecup.progress_equal = gtk_progress_bar_new();
    gtk_widget_set_tooltip_text(cecup.progress_equal,
                                "Equality scanner progress");
    cecup.progress_preview = gtk_progress_bar_new();
    gtk_widget_set_tooltip_text(cecup.progress_preview,
                                "Preview analysis progress");

    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(cecup.progress_rsync),
                                   TRUE);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(cecup.progress_rsync), "rsync");
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(cecup.progress_equal),
                                   TRUE);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(cecup.progress_equal),
                              "equal scanner");
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(cecup.progress_preview),
                                   TRUE);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(cecup.progress_preview),
                              "preview analysis");

    gtk_box_pack_start(GTK_BOX(progress_vbox), cecup.progress_rsync, FALSE,
                       FALSE, 0);
    gtk_box_pack_start(GTK_BOX(progress_vbox), cecup.progress_equal, FALSE,
                       FALSE, 0);
    gtk_box_pack_start(GTK_BOX(progress_vbox), cecup.progress_preview, FALSE,
                       FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header_vbox), progress_vbox, FALSE, FALSE, 5);

    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        error("Error getting current working directory: %s.\n",
              strerror(errno));
        exit(EXIT_FAILURE);
    }

    source_path_length = SNPRINTF(source_path_buffer, "%s/a/", cwd);
    default_src = xarena_push(cecup.ui_arena, source_path_length + 1);
    memcpy64(default_src, source_path_buffer, source_path_length + 1);

    destination_path_length = SNPRINTF(destination_path_buffer, "%s/b/", cwd);
    default_dst = xarena_push(cecup.ui_arena, destination_path_length + 1);
    memcpy64(default_dst, destination_path_buffer, destination_path_length + 1);

    paths_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(paths_hbox), 10);
    gtk_box_pack_start(GTK_BOX(main_vbox), paths_hbox, FALSE, FALSE, 0);

    l_entry_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    cecup.src_entry = gtk_entry_new();
    gtk_widget_set_tooltip_text(cecup.src_entry, "Base source directory path");
    gtk_entry_set_text(GTK_ENTRY(cecup.src_entry), default_src);
    browse_src = gtk_button_new_with_label("Browse");
    gtk_box_pack_start(GTK_BOX(l_entry_hbox), cecup.src_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(l_entry_hbox), browse_src, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(paths_hbox), l_entry_hbox, TRUE, TRUE, 0);

    invert_button = gtk_button_new_with_label("<--->");
    gtk_widget_set_tooltip_text(invert_button,
                                "Swap Source and Destination paths");
    gtk_box_pack_start(GTK_BOX(paths_hbox), invert_button, FALSE, FALSE, 0);

    r_entry_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    cecup.dst_entry = gtk_entry_new();
    gtk_widget_set_tooltip_text(cecup.dst_entry,
                                "Base destination directory path");
    gtk_entry_set_text(GTK_ENTRY(cecup.dst_entry), default_dst);
    browse_dst = gtk_button_new_with_label("Browse");
    gtk_box_pack_start(GTK_BOX(r_entry_hbox), cecup.dst_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(r_entry_hbox), browse_dst, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(paths_hbox), r_entry_hbox, TRUE, TRUE, 0);

    for (int32 i = 0; i < NUM_COLS; i += 1) {
        column_types[i] = G_TYPE_INT;
    }
    cecup.store = gtk_list_store_newv(NUM_COLS, column_types);

    v_paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    gtk_box_pack_start(GTK_BOX(main_vbox), v_paned, TRUE, TRUE, 0);
    paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_pack1(GTK_PANED(v_paned), paned, TRUE, FALSE);

    l_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    l_scroll = gtk_scrolled_window_new(NULL, NULL);
    l_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(cecup.store));
    cecup.l_tree = l_tree;
    g_object_set_data(G_OBJECT(l_tree), "side", GINT_TO_POINTER(0));
    setup_tree_columns(l_tree, COL_SRC_ACTION, COL_SRC_PATH);
    gtk_container_add(GTK_CONTAINER(l_scroll), l_tree);
    gtk_box_pack_start(GTK_BOX(l_vbox), l_scroll, TRUE, TRUE, 0);
    gtk_paned_pack1(GTK_PANED(paned), l_vbox, TRUE, FALSE);

    r_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    r_scroll = gtk_scrolled_window_new(NULL, NULL);
    r_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(cecup.store));
    cecup.r_tree = r_tree;
    g_object_set_data(G_OBJECT(r_tree), "side", GINT_TO_POINTER(1));
    setup_tree_columns(r_tree, COL_DST_ACTION, COL_DST_PATH);
    gtk_container_add(GTK_CONTAINER(r_scroll), r_tree);
    gtk_box_pack_start(GTK_BOX(r_vbox), r_scroll, TRUE, TRUE, 0);
    gtk_paned_pack2(GTK_PANED(paned), r_vbox, TRUE, FALSE);

    l_adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(l_scroll));
    r_adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(r_scroll));
    g_signal_connect(l_adj, "value-changed", G_CALLBACK(on_scroll_sync), r_adj);
    g_signal_connect(r_adj, "value-changed", G_CALLBACK(on_scroll_sync), l_adj);
    g_signal_connect(cecup.store, "sort-column-changed",
                     G_CALLBACK(on_sort_changed), NULL);

    log_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(log_scroll, -1, 150);
    log_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(log_view), FALSE);
    cecup.log_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(log_view));
    gtk_container_add(GTK_CONTAINER(log_scroll), log_view);
    gtk_paned_pack2(GTK_PANED(v_paned), log_scroll, FALSE, FALSE);

    filter_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_set_halign(filter_hbox, GTK_ALIGN_CENTER);
    cecup.filter_new = gtk_toggle_button_new_with_label(EMOJI_NEW);
    cecup.filter_hard = gtk_toggle_button_new_with_label(EMOJI_LINK);
    cecup.filter_update = gtk_toggle_button_new_with_label(EMOJI_UPDATE);
    cecup.filter_equal = gtk_toggle_button_new_with_label(EMOJI_EQUAL);
    cecup.filter_delete = gtk_toggle_button_new_with_label(EMOJI_DELETE);
    cecup.filter_ignore = gtk_toggle_button_new_with_label(EMOJI_IGNORE);

    gtk_widget_set_tooltip_text(cecup.filter_new, "Show New Files");
    gtk_widget_set_tooltip_text(cecup.filter_hard, "Show Hardlinks");
    gtk_widget_set_tooltip_text(cecup.filter_update, "Show Updates");
    gtk_widget_set_tooltip_text(cecup.filter_equal, "Show Identical");
    gtk_widget_set_tooltip_text(cecup.filter_delete, "Show Deletions");
    gtk_widget_set_tooltip_text(cecup.filter_ignore, "Show Ignored");

    // clang-format off
    gtk_box_pack_start(GTK_BOX(filter_hbox), cecup.filter_new,
                       FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(filter_hbox), cecup.filter_hard,
                       FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(filter_hbox), cecup.filter_update,
                       FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(filter_hbox), cecup.filter_equal,
                       FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(filter_hbox), cecup.filter_delete,
                       FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(filter_hbox), cecup.filter_ignore,
                       FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(main_vbox), filter_hbox, FALSE, FALSE, 0);
    // clang-format on

    cecup.stats_label = gtk_label_new("✅ Ready");
    gtk_box_pack_start(GTK_BOX(main_vbox), cecup.stats_label, FALSE, FALSE, 5);

    read_config();

    g_signal_connect(browse_src, "clicked", G_CALLBACK(on_browse_src), NULL);
    g_signal_connect(browse_dst, "clicked", G_CALLBACK(on_browse_dst), NULL);
    g_signal_connect(invert_button, "clicked", G_CALLBACK(on_invert_clicked),
                     NULL);
    g_signal_connect(cecup.preview_button, "clicked",
                     G_CALLBACK(on_preview_clicked), NULL);
    g_signal_connect(cecup.stop_button, "clicked", G_CALLBACK(on_stop_clicked),
                     NULL);
    g_signal_connect(cecup.sync_button, "clicked", G_CALLBACK(on_sync_clicked),
                     NULL);
    g_signal_connect(cecup.ignore_button, "clicked",
                     G_CALLBACK(on_ignore_clicked), NULL);
    g_signal_connect(cecup.fix_button, "clicked", G_CALLBACK(on_fix_clicked),
                     NULL);
    g_signal_connect(reset_button, "clicked", G_CALLBACK(on_reset_clicked),
                     NULL);

    g_signal_connect(cecup.filter_new, "toggled", G_CALLBACK(on_filter_toggled),
                     NULL);
    g_signal_connect(cecup.filter_hard, "toggled",
                     G_CALLBACK(on_filter_toggled), NULL);
    g_signal_connect(cecup.filter_update, "toggled",
                     G_CALLBACK(on_filter_toggled), NULL);
    g_signal_connect(cecup.filter_equal, "toggled",
                     G_CALLBACK(on_filter_toggled), NULL);
    g_signal_connect(cecup.filter_delete, "toggled",
                     G_CALLBACK(on_filter_toggled), NULL);
    g_signal_connect(cecup.filter_ignore, "toggled",
                     G_CALLBACK(on_filter_toggled), NULL);

    g_signal_connect(cecup.diff_entry, "changed", G_CALLBACK(on_config_changed),
                     NULL);
    g_signal_connect(cecup.term_entry, "changed", G_CALLBACK(on_config_changed),
                     NULL);
    g_signal_connect(cecup.src_entry, "changed", G_CALLBACK(on_config_changed),
                     NULL);
    g_signal_connect(cecup.dst_entry, "changed", G_CALLBACK(on_config_changed),
                     NULL);
    g_signal_connect(cecup.check_fs, "toggled", G_CALLBACK(on_config_changed),
                     NULL);
    g_signal_connect(cecup.check_equal, "toggled",
                     G_CALLBACK(on_config_changed), NULL);
    g_signal_connect(cecup.delete_excluded, "toggled",
                     G_CALLBACK(on_config_changed), NULL);

    gtk_widget_show_all(cecup.gtk_window);
    gtk_main();

    arena_destroy(cecup.row_arena);
    arena_destroy(cecup.ui_arena);
    g_mutex_clear(&cecup.ui_arena_mutex);
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

    if ((row_idx < 0) || (row_idx >= cecup.visible_count)) {
        return;
    }
    row = cecup.visible_rows[row_idx];

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
        if (col == gtk_tree_view_get_column(GTK_TREE_VIEW(cecup.l_tree), 3)) {
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
