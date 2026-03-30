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
#include <sys/stat.h>
#include <unistd.h>

#include "i18n.h"

#include "cecup.h"
#include "util.c"
#include "on.c"
#include "on_menu.c"
#include "columns.c"

#define SPACING_BOX 5
#define PADDING_BUTTON 5
#define PADDING_FILTER_BUTTON 2
#define PADDING_LABEL 0
#define PADDING_ZERO 0
#define FILL_FALSE false
#define EXPAND_FALSE false
#define FILL_TRUE true
#define EXPAND_TRUE true

#define INITIAL_CAPACITY 1024

static void
free_text_info(void *data) {
    free(data, SIZEOF(TextInfo));
    return;
}

static void
main_setup_tree_columns(GtkWidget *tree, enum CecupColumn col_act, enum CecupColumn col_path) {
    GActionMap *action_map = G_ACTION_MAP(cecup.application);
    int8 side = (int8)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(tree), "side"));

    if (g_action_map_lookup_action(action_map, "tree_dispatch") == NULL) {
        GSimpleAction *dispatch;
        GSimpleAction *ignore;

        dispatch = g_simple_action_new("tree_dispatch", G_VARIANT_TYPE_INT32);
        g_signal_connect(dispatch, "activate", G_CALLBACK(on_menu_dispatch), NULL);
        g_action_map_add_action(action_map, G_ACTION(dispatch));

        ignore = g_simple_action_new("ignore", G_VARIANT_TYPE_STRING);
        g_signal_connect(ignore, "activate", G_CALLBACK(on_menu_ignore_action), NULL);
        g_action_map_add_action(action_map, G_ACTION(ignore));
    }

    {
        GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
        GtkColumnViewColumn *column = gtk_column_view_column_new(NULL, factory);

        g_signal_connect(factory, "setup", G_CALLBACK(column_setup_checkbox), GINT_TO_POINTER(side));
        g_signal_connect(factory, "bind", G_CALLBACK(column_bind_checkbox), GINT_TO_POINTER(side));
        gtk_column_view_append_column(GTK_COLUMN_VIEW(tree), column);
        g_object_unref(column);
    }

    {
        GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
        GtkColumnViewColumn *column = gtk_column_view_column_new(_("Task"), factory);
        GtkSorter *sorter = GTK_SORTER(gtk_string_sorter_new(NULL));

        g_signal_connect(factory, "setup", G_CALLBACK(column_setup_action), NULL);
        g_signal_connect(factory, "bind", G_CALLBACK(column_bind_action), GINT_TO_POINTER(side));

        gtk_column_view_column_set_resizable(column, TRUE);
        gtk_column_view_column_set_sorter(column, sorter);

        g_object_set_data(G_OBJECT(column), "col_id", GINT_TO_POINTER(col_act));
        gtk_column_view_append_column(GTK_COLUMN_VIEW(tree), column);
        g_object_unref(column);
    }

    {
        GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
        GtkSorter *sorter = GTK_SORTER(gtk_string_sorter_new(NULL));
        GtkColumnViewColumn *column = gtk_column_view_column_new(_("Name"), factory);

        g_signal_connect(factory, "setup", G_CALLBACK(column_setup_path), tree);
        g_signal_connect(factory, "bind", G_CALLBACK(column_bind_path), tree);

        gtk_column_view_column_set_resizable(column, TRUE);
        gtk_column_view_column_set_fixed_width(column, 500);
        gtk_column_view_column_set_sorter(column, sorter);

        g_object_set_data(G_OBJECT(column), "col_id", GINT_TO_POINTER(col_path));
        gtk_column_view_append_column(GTK_COLUMN_VIEW(tree), column);
        g_object_unref(column);
    }

    {
        GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
        GtkSorter *sorter = GTK_SORTER(gtk_string_sorter_new(NULL));
        TextInfo *text_info = xmalloc(SIZEOF(*text_info));
        GtkColumnViewColumn *column = gtk_column_view_column_new(_("Size"), factory);

        text_info->side = side;
        text_info->type = COLUMN_SIZE;
        g_object_set_data_full(G_OBJECT(factory), "text_info", text_info, free_text_info);

        g_signal_connect(factory, "setup", G_CALLBACK(column_text_setup), NULL);
        g_signal_connect(factory, "bind", G_CALLBACK(column_text_bind), text_info);

        gtk_column_view_column_set_resizable(column, TRUE);
        gtk_column_view_column_set_sorter(column, sorter);

        g_object_set_data(G_OBJECT(column), "col_id", GINT_TO_POINTER(COL_SIZE_RAW));
        gtk_column_view_append_column(GTK_COLUMN_VIEW(tree), column);
        g_object_unref(column);
    }

    {
        GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
        GtkSorter *sorter = GTK_SORTER(gtk_string_sorter_new(NULL));
        TextInfo *text_info = xmalloc(SIZEOF(*text_info));
        GtkColumnViewColumn *column = gtk_column_view_column_new(_("Modification Time"), factory);

        text_info->side = side;
        text_info->type = COLUMN_MTIME;
        g_object_set_data_full(G_OBJECT(factory), "text_info", text_info, free_text_info);

        g_signal_connect(factory, "setup", G_CALLBACK(column_text_setup), NULL);
        g_signal_connect(factory, "bind", G_CALLBACK(column_text_bind), text_info);

        gtk_column_view_column_set_expand(column, TRUE);
        gtk_column_view_column_set_resizable(column, TRUE);
        gtk_column_view_column_set_sorter(column, sorter);

        g_object_set_data(G_OBJECT(column), "col_id", GINT_TO_POINTER(COL_MTIME_RAW));
        gtk_column_view_append_column(GTK_COLUMN_VIEW(tree), column);
        g_object_unref(column);
    }

    gtk_widget_set_has_tooltip(tree, TRUE);
    gtk_widget_set_focusable(tree, TRUE);

    g_signal_connect(tree, "query-tooltip", G_CALLBACK(on_tree_tooltip), NULL);

    {
        GtkSorter *sorter = gtk_column_view_get_sorter(GTK_COLUMN_VIEW(tree));
        g_signal_connect(sorter, "changed", G_CALLBACK(on_sort_changed), tree);
    }

    {
        GtkGesture *click = gtk_gesture_click_new();

        gtk_widget_add_controller(tree, GTK_EVENT_CONTROLLER(click));
        gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(click), GTK_PHASE_CAPTURE);
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 0);
        g_signal_connect(click, "pressed", G_CALLBACK(on_tree_button_press), NULL);
    }

    {
        GtkEventController *key = gtk_event_controller_key_new();

        gtk_widget_add_controller(tree, GTK_EVENT_CONTROLLER(key));
        gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(key), GTK_PHASE_BUBBLE);
        g_signal_connect(key, "key-pressed", G_CALLBACK(on_tree_key_press), NULL);
    }

    return;
}

static void
main_application_run(GtkApplication *application, gpointer user_data) {
    GtkWidget *main_vbox;
    GtkWidget *header_vbox;
    GtkWidget *button_hbox;
    GtkWidget *filter_hbox;
    GtkWidget *search_hbox;
    GtkWidget *options_hbox;
    GtkWidget *reset_button;
    GtkWidget *v_paned_outer;
    GtkWidget *v_paned_inner;
    GtkWidget *footer_vbox;
    GtkWidget *paned;

    GtkWidget *vbox[2];
    GtkWidget *entry_hbox[2];
    GtkWidget *scroll[2];
    GtkWidget *tree[2];
    GtkWidget *progress_vbox;
    GtkWidget *paths_hbox;

    GtkAdjustment *l_adj;
    GtkAdjustment *r_adj;

    char src_path_buffer[MAX_PATH_LENGTH];
    char dst_path_buffer[MAX_PATH_LENGTH];
    bool is_first_run = false;

    (void)user_data;

    {
        static char *base_css[] = {
            "columnview row { min-height: 0px; }",
            "columnview cell { padding: 0px; }",
            "paned > separator { min-width: 10px; min-height: 10px; }",
            "scrollbar.vertical slider { min-width: 12px; }",
            "scrollbar.horizontal slider { min-height: 12px; }",
            "progressbar text { font-size: 11pt; font-weight: bold; }"
        };
        GtkCssProvider *css_provider;
        char css_buffer[BUFSIZ];
        int32 offset;

        css_provider = gtk_css_provider_new();
        offset = 0;

        for (int32 i = 0; i < LENGTH(base_css); i += 1) {
            int32 n;
            n = snprintf2(css_buffer + offset, SIZEOF(css_buffer) - offset, "%s\n", base_css[i]);
            offset += n;
        }

        for (int32 i = 0; i < LENGTH(colors); i += 1) {
            int32 m;

            if (colors[i] == NULL) {
                continue;
            }

            m = snprintf2(css_buffer + offset, SIZEOF(css_buffer) - offset,
                          "row:not(:selected)"
                          " .cell-color-%d { background-color: %s; }\n",
                          i, colors[i]);
            offset += m;
        }

        gtk_css_provider_load_from_string(css_provider, css_buffer);
        gtk_style_context_add_provider_for_display(gdk_display_get_default(),
                                                   GTK_STYLE_PROVIDER(css_provider),
                                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(css_provider);
    }

    cecup.gtk_window = gtk_application_window_new(application);
    gtk_window_set_title(GTK_WINDOW(cecup.gtk_window), "cecup");
    gtk_window_set_default_size(GTK_WINDOW(cecup.gtk_window), 1100, 800);

    g_signal_connect(cecup.gtk_window, "destroy", G_CALLBACK(on_window_destroy), NULL);

    main_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, SPACING_BOX);
    gtk_window_set_child(GTK_WINDOW(cecup.gtk_window), main_vbox);

    header_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, SPACING_BOX);
    gtk_widget_set_margin_start(header_vbox, SPACING_BOX);
    gtk_widget_set_margin_end(header_vbox, SPACING_BOX);
    gtk_widget_set_margin_top(header_vbox, SPACING_BOX);
    gtk_widget_set_margin_bottom(header_vbox, SPACING_BOX);
    gtk_box_append(GTK_BOX(main_vbox), header_vbox);

    button_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, SPACING_BOX);

    cecup.preview_button = gtk_button_new_with_label(_("🔎 Analyze"));
    cecup.ignore_button = gtk_button_new_with_label(_("Edit Ignore Rules"));

    gtk_widget_set_tooltip_text(cecup.preview_button,
                                _("Check which files need to be copied or updated"));
    gtk_widget_set_tooltip_text(cecup.ignore_button,
                                _("Edit the list of filename patterns to ignore"));

    cecup.stop_button = gtk_button_new_with_label(_("⏹️ Stop"));
    gtk_widget_set_tooltip_text(cecup.stop_button, _("Cancel the current task"));
    cecup.preview_dirty = true;
    cecup.sync_button = gtk_button_new_with_label(_("⏩ Apply Changes"));
    gtk_widget_set_tooltip_text(cecup.sync_button, _("Start copying and updating all files"));
    gtk_widget_set_sensitive(cecup.stop_button, FALSE);

    gtk_box_append(GTK_BOX(button_hbox), cecup.ignore_button);
    gtk_widget_set_margin_start(cecup.ignore_button, PADDING_BUTTON);
    gtk_widget_set_margin_end(cecup.ignore_button, PADDING_BUTTON);

    {
        GtkWidget *spacer = gtk_label_new("");

        gtk_widget_set_hexpand(spacer, TRUE);
        gtk_box_append(GTK_BOX(button_hbox), spacer);
    }

    gtk_box_append(GTK_BOX(button_hbox), cecup.preview_button);
    gtk_widget_set_margin_start(cecup.preview_button, PADDING_BUTTON);
    gtk_widget_set_margin_end(cecup.preview_button, PADDING_BUTTON);

    gtk_box_append(GTK_BOX(button_hbox), cecup.stop_button);
    gtk_widget_set_margin_start(cecup.stop_button, PADDING_BUTTON);
    gtk_widget_set_margin_end(cecup.stop_button, PADDING_BUTTON);

    gtk_box_append(GTK_BOX(button_hbox), cecup.sync_button);
    gtk_widget_set_margin_start(cecup.sync_button, PADDING_BUTTON);
    gtk_widget_set_margin_end(cecup.sync_button, PADDING_BUTTON);

    gtk_box_append(GTK_BOX(header_vbox), button_hbox);

    options_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, SPACING_BOX);
    cecup.check_fs = gtk_check_button_new_with_label(_("Protect same-drive sync"));
    cecup.delete_ignored_button = gtk_check_button_new_with_label(_("Remove ignored items"));
    cecup.delete_after_button = gtk_check_button_new_with_label(_("Sync 100%"));

    gtk_widget_set_tooltip_text(cecup.check_fs,
                                _("Prevent copying if original and backup are on the same disk"));
    gtk_widget_set_tooltip_text(cecup.delete_ignored_button,
                                _("Remove files from backup if they were added to the ignore list"));
    gtk_widget_set_tooltip_text(cecup.delete_after_button,
                                _("Delete files in backup that do not exist in the original"));

    cecup.diff_entry = gtk_entry_new();
    gtk_widget_set_tooltip_text(cecup.diff_entry, _("Executable used for comparing files"));
    cecup.term_entry = gtk_entry_new();
    gtk_widget_set_tooltip_text(cecup.term_entry,
                                _("Terminal emulator used to launch the diff tool"));

    reset_button = gtk_button_new_with_label(_("Defaults"));
    gtk_widget_set_tooltip_text(reset_button, _("Restore original settings"));

    gtk_box_append(GTK_BOX(options_hbox), cecup.check_fs);
    gtk_widget_set_margin_start(cecup.check_fs, PADDING_BUTTON);
    gtk_widget_set_margin_end(cecup.check_fs, PADDING_BUTTON);

    gtk_box_append(GTK_BOX(options_hbox), cecup.delete_ignored_button);
    gtk_widget_set_margin_start(cecup.delete_ignored_button, PADDING_BUTTON);
    gtk_widget_set_margin_end(cecup.delete_ignored_button, PADDING_BUTTON);

    gtk_box_append(GTK_BOX(options_hbox), cecup.delete_after_button);
    gtk_widget_set_margin_start(cecup.delete_after_button, PADDING_BUTTON);
    gtk_widget_set_margin_end(cecup.delete_after_button, PADDING_BUTTON);

    gtk_box_append(GTK_BOX(options_hbox), gtk_label_new(_("Diff Tool:")));
    gtk_box_append(GTK_BOX(options_hbox), cecup.diff_entry);

    gtk_editable_set_text(GTK_EDITABLE(cecup.diff_entry), "unidiff.bash");
    gtk_box_append(GTK_BOX(options_hbox), gtk_label_new(_("Terminal:")));
    gtk_box_append(GTK_BOX(options_hbox), cecup.term_entry);
    gtk_editable_set_text(GTK_EDITABLE(cecup.term_entry), "xterm");

    gtk_box_append(GTK_BOX(options_hbox), reset_button);
    gtk_box_append(GTK_BOX(header_vbox), options_hbox);

    progress_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    cecup.progress_bar = gtk_progress_bar_new();

    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(cecup.progress_bar), TRUE);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(cecup.progress_bar), _("Analyzing changes"));

    gtk_box_append(GTK_BOX(progress_vbox), cecup.progress_bar);
    gtk_box_append(GTK_BOX(header_vbox), progress_vbox);
    gtk_widget_set_margin_bottom(progress_vbox, 5);

    {
        char cwd[MAX_PATH_LENGTH];
        if (getcwd(cwd, sizeof(cwd)) == NULL) {
            error("Error getting current working directory: %s.\n", strerror(errno));
            fatal(EXIT_FAILURE);
        }

        if (strlen32(cwd) > (SIZEOF(src_path_buffer) / 2)) {
            error("Error: current working directory path is too long.\n");
            fatal(EXIT_FAILURE);
        }

        SNPRINTF(src_path_buffer, "%s/a/", cwd);
        SNPRINTF(dst_path_buffer, "%s/b/", cwd);
    }

    paths_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_start(paths_hbox, 10);
    gtk_widget_set_margin_end(paths_hbox, 10);
    gtk_widget_set_margin_top(paths_hbox, 10);
    gtk_widget_set_margin_bottom(paths_hbox, 10);
    gtk_box_append(GTK_BOX(main_vbox), paths_hbox);

    entry_hbox[L] = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    cecup.dir_entry[L] = gtk_entry_new();
    gtk_widget_set_tooltip_text(cecup.dir_entry[L], _("Folder containing your original files"));
    gtk_editable_set_text(GTK_EDITABLE(cecup.dir_entry[L]), src_path_buffer);
    cecup.browse_button[L] = gtk_button_new_with_label(_("Select Folder"));

    gtk_widget_set_hexpand(cecup.dir_entry[L], TRUE);
    gtk_box_append(GTK_BOX(entry_hbox[L]), cecup.dir_entry[L]);
    gtk_box_append(GTK_BOX(entry_hbox[L]), cecup.browse_button[L]);
    gtk_widget_set_hexpand(entry_hbox[L], TRUE);
    gtk_box_append(GTK_BOX(paths_hbox), entry_hbox[L]);

    cecup.invert_button = gtk_button_new_with_label("<--->");
    gtk_widget_set_tooltip_text(cecup.invert_button, _("Invert Original and Backup"));
    gtk_box_append(GTK_BOX(paths_hbox), cecup.invert_button);

    entry_hbox[R] = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    cecup.dir_entry[R] = gtk_entry_new();
    gtk_widget_set_tooltip_text(cecup.dir_entry[R], _("Folder where the backup will be stored"));
    gtk_editable_set_text(GTK_EDITABLE(cecup.dir_entry[R]), dst_path_buffer);
    cecup.browse_button[R] = gtk_button_new_with_label(_("Select Folder"));

    gtk_widget_set_hexpand(cecup.dir_entry[R], TRUE);
    gtk_box_append(GTK_BOX(entry_hbox[R]), cecup.dir_entry[R]);
    gtk_box_append(GTK_BOX(entry_hbox[R]), cecup.browse_button[R]);
    gtk_widget_set_hexpand(entry_hbox[R], TRUE);
    gtk_box_append(GTK_BOX(paths_hbox), entry_hbox[R]);

    search_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, SPACING_BOX);
    gtk_widget_set_margin_start(search_hbox, 10);
    gtk_widget_set_margin_end(search_hbox, 10);
    cecup.search_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(cecup.search_entry), _("Search files..."));
    gtk_entry_set_icon_from_icon_name(GTK_ENTRY(cecup.search_entry),
                                      GTK_ENTRY_ICON_PRIMARY, "system-search-symbolic");
    {
        GtkWidget *search_label;

        search_label = gtk_label_new(_("🔍"));
        gtk_box_append(GTK_BOX(search_hbox), search_label);
        gtk_widget_set_margin_start(search_label, 5);
        gtk_widget_set_margin_end(search_label, 5);
    }
    gtk_widget_set_hexpand(cecup.search_entry, TRUE);
    gtk_box_append(GTK_BOX(search_hbox), cecup.search_entry);

    cecup.select_visible_button = gtk_button_new_with_label(_("Select all visible"));
    gtk_box_append(GTK_BOX(search_hbox), cecup.select_visible_button);

    cecup.unselect_button = gtk_button_new_with_label(_("Unselect all"));
    gtk_box_append(GTK_BOX(search_hbox), cecup.unselect_button);

    gtk_box_append(GTK_BOX(main_vbox), search_hbox);

    v_paned_outer = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    gtk_widget_set_vexpand(v_paned_outer, TRUE);
    gtk_box_append(GTK_BOX(main_vbox), v_paned_outer);

    v_paned_inner = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    gtk_paned_set_start_child(GTK_PANED(v_paned_outer), v_paned_inner);

    paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_vexpand(paned, TRUE);
    gtk_paned_set_start_child(GTK_PANED(v_paned_inner), paned);

    vbox[L] = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    scroll[L] = gtk_scrolled_window_new();
    {
        GtkSelectionModel *selection_model;
        GListModel *list_model = g_object_ref(cecup.store);
        selection_model = GTK_SELECTION_MODEL(gtk_single_selection_new(list_model));
        tree[L] = gtk_column_view_new(selection_model);
    }
    cecup.tree[L] = tree[L];
    g_object_set_data(G_OBJECT(tree[L]), "side", GINT_TO_POINTER(L));
    main_setup_tree_columns(tree[L], COL_SRC_ACTION, COL_SRC_PATH);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll[L]), tree[L]);
    gtk_box_append(GTK_BOX(vbox[L]), scroll[L]);
    gtk_widget_set_vexpand(scroll[L], TRUE);
    gtk_paned_set_start_child(GTK_PANED(paned), vbox[L]);

    vbox[R] = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    scroll[R] = gtk_scrolled_window_new();
    {
        GtkSelectionModel *selection_model;
        GListModel *list_model = g_object_ref(cecup.store);
        selection_model = GTK_SELECTION_MODEL(gtk_single_selection_new(list_model));
        tree[R] = gtk_column_view_new(selection_model);
    }
    cecup.tree[R] = tree[R];
    g_object_set_data(G_OBJECT(tree[R]), "side", GINT_TO_POINTER(R));
    main_setup_tree_columns(tree[R], COL_DST_ACTION, COL_DST_PATH);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll[R]), tree[R]);
    gtk_box_append(GTK_BOX(vbox[R]), scroll[R]);
    gtk_widget_set_vexpand(scroll[R], TRUE);
    gtk_paned_set_end_child(GTK_PANED(paned), vbox[R]);

    l_adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scroll[L]));
    r_adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scroll[R]));
    g_signal_connect(l_adj, "value-changed", G_CALLBACK(on_scroll_sync), r_adj);
    g_signal_connect(r_adj, "value-changed", G_CALLBACK(on_scroll_sync), l_adj);

    footer_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, SPACING_BOX);
    gtk_paned_set_end_child(GTK_PANED(v_paned_inner), footer_vbox);

    filter_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, SPACING_BOX);
    gtk_widget_set_halign(filter_hbox, GTK_ALIGN_CENTER);
    cecup.filter_new = gtk_toggle_button_new_with_label(EMOJI_NEW);
    cecup.filter_hard = gtk_toggle_button_new_with_label(EMOJI_LINK);
    cecup.filter_update = gtk_toggle_button_new_with_label(EMOJI_UPDATE);
    cecup.filter_equal = gtk_toggle_button_new_with_label(EMOJI_EQUAL);
    cecup.filter_delete = gtk_toggle_button_new_with_label(EMOJI_DELETE);
    cecup.filter_ignore = gtk_toggle_button_new_with_label(EMOJI_IGNORE);

    gtk_widget_set_tooltip_text(cecup.filter_new, _("Show New Files"));
    gtk_widget_set_tooltip_text(cecup.filter_hard, _("Show links"));
    gtk_widget_set_tooltip_text(cecup.filter_update, _("Show Updates"));
    gtk_widget_set_tooltip_text(cecup.filter_equal, _("Show equals"));
    gtk_widget_set_tooltip_text(cecup.filter_delete, _("Show Deletions"));
    gtk_widget_set_tooltip_text(cecup.filter_ignore, _("Show Ignored"));

    gtk_box_append(GTK_BOX(filter_hbox), cecup.filter_new);
    gtk_widget_set_margin_start(cecup.filter_new, PADDING_FILTER_BUTTON);
    gtk_widget_set_margin_end(cecup.filter_new, PADDING_FILTER_BUTTON);

    gtk_box_append(GTK_BOX(filter_hbox), cecup.filter_hard);
    gtk_widget_set_margin_start(cecup.filter_hard, PADDING_FILTER_BUTTON);
    gtk_widget_set_margin_end(cecup.filter_hard, PADDING_FILTER_BUTTON);

    gtk_box_append(GTK_BOX(filter_hbox), cecup.filter_update);
    gtk_widget_set_margin_start(cecup.filter_update, PADDING_FILTER_BUTTON);
    gtk_widget_set_margin_end(cecup.filter_update, PADDING_FILTER_BUTTON);

    gtk_box_append(GTK_BOX(filter_hbox), cecup.filter_equal);
    gtk_widget_set_margin_start(cecup.filter_equal, PADDING_FILTER_BUTTON);
    gtk_widget_set_margin_end(cecup.filter_equal, PADDING_FILTER_BUTTON);

    gtk_box_append(GTK_BOX(filter_hbox), cecup.filter_delete);
    gtk_widget_set_margin_start(cecup.filter_delete, PADDING_FILTER_BUTTON);
    gtk_widget_set_margin_end(cecup.filter_delete, PADDING_FILTER_BUTTON);

    gtk_box_append(GTK_BOX(filter_hbox), cecup.filter_ignore);
    gtk_widget_set_margin_start(cecup.filter_ignore, PADDING_FILTER_BUTTON);
    gtk_widget_set_margin_end(cecup.filter_ignore, PADDING_FILTER_BUTTON);

    gtk_box_append(GTK_BOX(footer_vbox), filter_hbox);

    cecup.stats_label = gtk_label_new(_("✅ Everything ready"));
    gtk_box_append(GTK_BOX(footer_vbox), cecup.stats_label);
    gtk_widget_set_margin_top(cecup.stats_label, 5);
    gtk_widget_set_margin_bottom(cecup.stats_label, 5);

    gtk_paned_set_resize_end_child(GTK_PANED(v_paned_inner), FALSE);
    gtk_paned_set_shrink_end_child(GTK_PANED(v_paned_inner), FALSE);

    {
        GtkWidget *log_scroll = gtk_scrolled_window_new();

        gtk_widget_set_size_request(log_scroll, -1, 100);

        cecup.log_view = gtk_text_view_new();
        gtk_text_view_set_editable(GTK_TEXT_VIEW(cecup.log_view), FALSE);
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(cecup.log_view), GTK_WRAP_WORD_CHAR);
        cecup.log_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(cecup.log_view));
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(log_scroll), cecup.log_view);

        gtk_paned_set_end_child(GTK_PANED(v_paned_outer), log_scroll);
    }

    {
        GtkGesture *log_gesture = gtk_gesture_click_new();

        gtk_widget_add_controller(cecup.log_view, GTK_EVENT_CONTROLLER(log_gesture));
        gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(log_gesture),
                                                   GTK_PHASE_CAPTURE);
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(log_gesture), 0);
        g_signal_connect(log_gesture, "pressed", G_CALLBACK(on_log_button_press), NULL);
    }

    {
        GActionMap *action_map = G_ACTION_MAP(cecup.application);
        GSimpleAction *action_copy_all = g_simple_action_new("copy_all", NULL);
        GSimpleAction *action_copy_line = g_simple_action_new("copy_line", G_VARIANT_TYPE_INT32);

        g_action_map_add_action(action_map, G_ACTION(action_copy_all));
        g_action_map_add_action(action_map, G_ACTION(action_copy_line));

        g_signal_connect(action_copy_all, "activate", G_CALLBACK(on_log_copy), "all");
        g_signal_connect(action_copy_line, "activate", G_CALLBACK(on_log_copy), "line");
    }

    cecup.src_entry_id = g_signal_connect(cecup.dir_entry[L], "activate",
                                          G_CALLBACK(on_config_changed), NULL);
    cecup.dst_entry_id = g_signal_connect(cecup.dir_entry[R], "activate",
                                          G_CALLBACK(on_config_changed), NULL);

    g_signal_connect(cecup.dir_entry[L], "changed",
                     G_CALLBACK(invalidate_preview), NULL);
    g_signal_connect(cecup.dir_entry[R], "changed",
                     G_CALLBACK(invalidate_preview), NULL);

    do {
        GKeyFile *key;
        char *value;

        key = g_key_file_new();
        if (!g_key_file_load_from_file(key, cecup.config_path, G_KEY_FILE_NONE, NULL)) {
            is_first_run = true;
            g_key_file_free(key);
            break;
        }

        if ((value = g_key_file_get_string(key, "Paths", "src", NULL))) {
            gtk_editable_set_text(GTK_EDITABLE(cecup.dir_entry[L]), value);
            g_free(value);
        }
        if ((value = g_key_file_get_string(key, "Paths", "dst", NULL))) {
            gtk_editable_set_text(GTK_EDITABLE(cecup.dir_entry[R]), value);
            g_free(value);
        }
        if ((value = g_key_file_get_string(key, "Tools", "diff", NULL))) {
            gtk_editable_set_text(GTK_EDITABLE(cecup.diff_entry), value);
            g_free(value);
        }
        if ((value = g_key_file_get_string(key, "Tools", "term", NULL))) {
            gtk_editable_set_text(GTK_EDITABLE(cecup.term_entry), value);
            g_free(value);
        }

        if (g_key_file_has_key(key, "Filters", "new", NULL)) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup.filter_new),
                                         g_key_file_get_boolean(key, "Filters", "new", NULL));
        }
        if (g_key_file_has_key(key, "Filters", "hard", NULL)) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup.filter_hard),
                                         g_key_file_get_boolean(key, "Filters", "hard", NULL));
        }
        if (g_key_file_has_key(key, "Filters", "update", NULL)) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup.filter_update),
                                         g_key_file_get_boolean(key, "Filters", "update", NULL));
        }
        if (g_key_file_has_key(key, "Filters", "equal", NULL)) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup.filter_equal),
                                         g_key_file_get_boolean(key, "Filters", "equal", NULL));
        }
        if (g_key_file_has_key(key, "Filters", "delete", NULL)) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup.filter_delete),
                                         g_key_file_get_boolean(key, "Filters", "delete", NULL));
        }
        if (g_key_file_has_key(key, "Filters", "ignore", NULL)) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup.filter_ignore),
                                         g_key_file_get_boolean(key, "Filters", "ignore", NULL));
        }
        if (g_key_file_has_key(key, "Options", "check_fs", NULL)) {
            gtk_check_button_set_active(GTK_CHECK_BUTTON(cecup.check_fs),
                                        g_key_file_get_boolean(key, "Options", "check_fs", NULL));
        }
        if (g_key_file_has_key(key, "Options", "delete_ignored", NULL)) {
            bool val;

            val = g_key_file_get_boolean(key, "Options", "delete_ignored", NULL);
            cecup.delete_ignored = val;
            gtk_check_button_set_active(GTK_CHECK_BUTTON(cecup.delete_ignored_button), val);
        }
        if (g_key_file_has_key(key, "Options", "delete_after", NULL)) {
            bool val;

            val = g_key_file_get_boolean(key, "Options", "delete_after", NULL);
            cecup.delete_after = val;
            gtk_check_button_set_active(GTK_CHECK_BUTTON(cecup.delete_after_button), val);
        }

        g_key_file_free(key);
    } while (0);

    g_signal_connect(cecup.browse_button[L], "clicked", G_CALLBACK(on_browse_src),      NULL);
    g_signal_connect(cecup.browse_button[R], "clicked", G_CALLBACK(on_browse_dst),      NULL);
    g_signal_connect(cecup.invert_button,    "clicked", G_CALLBACK(on_invert_clicked),  NULL);
    g_signal_connect(cecup.preview_button,   "clicked", G_CALLBACK(on_preview_clicked), NULL);
    g_signal_connect(cecup.stop_button,      "clicked", G_CALLBACK(on_stop_clicked),    NULL);
    g_signal_connect(cecup.sync_button,      "clicked", G_CALLBACK(on_sync_clicked),    NULL);
    g_signal_connect(cecup.ignore_button,    "clicked", G_CALLBACK(on_ignore_clicked),  NULL);
    g_signal_connect(cecup.unselect_button,  "clicked", G_CALLBACK(on_unselect_all_clicked), NULL);
    g_signal_connect(cecup.select_visible_button, "clicked", G_CALLBACK(on_select_all_visible_clicked), NULL);
    g_signal_connect(reset_button,           "clicked", G_CALLBACK(on_reset_clicked),   NULL);

    g_signal_connect(cecup.search_entry, "changed", G_CALLBACK(on_search_changed), NULL);

    g_signal_connect(cecup.filter_new,    "toggled", G_CALLBACK(on_filter_toggled), NULL);
    g_signal_connect(cecup.filter_hard,   "toggled", G_CALLBACK(on_filter_toggled), NULL);
    g_signal_connect(cecup.filter_update, "toggled", G_CALLBACK(on_filter_toggled), NULL);
    g_signal_connect(cecup.filter_equal,  "toggled", G_CALLBACK(on_filter_toggled), NULL);
    g_signal_connect(cecup.filter_delete, "toggled", G_CALLBACK(on_filter_toggled), NULL);
    g_signal_connect(cecup.filter_ignore, "toggled", G_CALLBACK(on_filter_toggled), NULL);

    g_signal_connect(cecup.diff_entry, "changed", G_CALLBACK(on_config_changed), NULL);
    g_signal_connect(cecup.term_entry, "changed", G_CALLBACK(on_config_changed), NULL);

    g_signal_connect(cecup.check_fs, "toggled", G_CALLBACK(on_preview_setting_toggled), NULL);
    g_signal_connect(cecup.delete_ignored_button, "toggled", G_CALLBACK(on_delete_ignored_toggled), NULL);
    g_signal_connect(cecup.delete_after_button, "toggled", G_CALLBACK(on_delete_after_toggled), NULL);

    cecup_get_dirs();

    gtk_window_present(GTK_WINDOW(cecup.gtk_window));

    if (is_first_run) {
        GtkWidget *dialog;

        dialog = gtk_message_dialog_new(GTK_WINDOW(cecup.gtk_window),
                                        GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
                                        "%s", _(cecup_welcome_text));

        g_signal_connect(dialog, "response", G_CALLBACK(gtk_window_destroy), NULL);
        gtk_widget_show(dialog);
    }

    return;
}

int32
main(int32 argc, char **argv) {
    int32 status;

    program = argv[0];

    if (setenv("GTK_IM_MODULE", "gtk-im-context-simple", true) < 0) {
        error("Error in setenv: %s.\n", strerror(errno));
        fatal(EXIT_FAILURE);
    }

    {
        char *locale_devel;
        char *locale_system;
        char *locale_local_system;

        // TODO: Hardcoded locale paths might fail if the application is installed in non-standard
        //       prefixes (like /opt) or sandboxed environments (like Flatpak/Snap).
        locale_devel = "./po";
        locale_system = "/usr/share/locale/";
        locale_local_system = "/usr/local/share/locale/";

        if (setlocale(LC_ALL, "") == NULL) {
            error("Error setting locale: %s.\n", strerror(errno));
            exit(EXIT_FAILURE);
        }

        if (access(locale_devel, F_OK) == 0) {
            bindtextdomain("cecup", locale_devel);
        } else if (access(locale_system, F_OK) == 0) {
            bindtextdomain("cecup", locale_system);
        } else if (access(locale_local_system, F_OK) == 0) {
            bindtextdomain("cecup", locale_local_system);
        } else {
            error("Can't find any locale directory available.\n");
        }

        bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
        textdomain(GETTEXT_PACKAGE);
    }

    timezone_init();
    memset64(&cecup, 0, SIZEOF(cecup));

    cecup.arena = arena_create(SIZEMB(64));
    cecup.traversal_src.arena = arena_create(SIZEMB(64));
    cecup.traversal_dst.arena = arena_create(SIZEMB(64));
    g_mutex_init(&cecup.arena_mutex);

    cecup.rows_len = 0;
    cecup.rows_capacity = 256;

    cecup.rows_src = xmalloc(cecup.rows_capacity*SIZEOF(int32));
    cecup.rows_dst = xmalloc(cecup.rows_capacity*SIZEOF(int32));
    cecup.rows_visible = xmalloc(cecup.rows_capacity*SIZEOF(int32));
    cecup.rows_selected = xmalloc(cecup.rows_capacity*SIZEOF(uint8));

    cecup.transfer_set = hash_create_transfer_set(INITIAL_CAPACITY);

    cecup.traversal_src.map = hash_create_fs_map(INITIAL_CAPACITY);
    cecup.traversal_dst.map = hash_create_fs_map(INITIAL_CAPACITY);
    cecup.traversal_src.inode_map = hash_create_inode_map(INITIAL_CAPACITY);
    cecup.traversal_dst.inode_map = hash_create_inode_map(INITIAL_CAPACITY);

    cecup.ignore_patterns = NULL;
    cecup.ignore_capacity = 0;
    cecup.ignore_count = 0;

    cecup.sort_col = COL_SRC_PATH;
    cecup.sort_order = GTK_SORT_ASCENDING;
    cecup.refresh_id = 0;

    {
        char xdg_buffer[MAX_PATH_LENGTH];
        char config_base[MAX_PATH_LENGTH];
        char *XDG_CONFIG_HOME;

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
            pid_t child_cp;

            g_mkdir_with_parents(config_base, 0755);

            switch (child_cp = fork()) {
            case -1:
                error("Error forking: %s.\n", strerror(errno));
                exit(EXIT_FAILURE);
            case 0:
            {
                char *args_cp[] = {
                    "cp",
                    "-r",
                    "/etc/cecup/.",
                    config_base,
                    NULL,
                };

                execvp(args_cp[0], args_cp);
                error("Error executing cp: %s.\n", strerror(errno));
                _exit(EXIT_FAILURE);
            }
            default:
            {
                if (waitpid(child_cp, NULL, 0) < 0) {
                    error("Error waiting for cp: %s.\n", strerror(errno));
                }
                break;
            }
            }
        }

        SNPRINTF(cecup.ignore_path, "%s/ignore.conf", config_base);
        SNPRINTF(cecup.config_path, "%s/cecup.conf", config_base);
    }

    cecup.store = G_LIST_MODEL(cecup_list_model_new());
    cecup.stop_working = false;

    cecup.application = gtk_application_new("com.cecup.app", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(cecup.application, "activate", G_CALLBACK(main_application_run), NULL);
    status = g_application_run(G_APPLICATION(cecup.application), argc, argv);

    g_object_unref(cecup.application);
    g_object_unref(cecup.store);

    free(cecup.rows_src, cecup.rows_capacity*SIZEOF(*(cecup.rows_src)));
    free(cecup.rows_dst, cecup.rows_capacity*SIZEOF(*(cecup.rows_dst)));
    free(cecup.rows_visible, cecup.rows_capacity*SIZEOF(*(cecup.rows_visible)));
    free(cecup.rows_selected, cecup.rows_capacity*SIZEOF(uint8));

    free(cecup.src_base, cecup.src_base_len + 1);
    free(cecup.dst_base, cecup.dst_base_len + 1);

    hash_destroy_fs_map(cecup.traversal_src.map);
    hash_destroy_fs_map(cecup.traversal_dst.map);

    hash_destroy_inode_map(cecup.traversal_src.inode_map);
    hash_destroy_inode_map(cecup.traversal_dst.inode_map);

    arena_destroy(cecup.traversal_src.arena);
    arena_destroy(cecup.traversal_dst.arena);
    arena_destroy(cecup.arena);

    g_mutex_clear(&cecup.arena_mutex);

    exit(status);
}
