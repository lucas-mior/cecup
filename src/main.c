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
#include "columns.c"

#include ".welcome.h"

#define SPACING_BOX 5
#define PADDING_BUTTON 5
#define PADDING_FILTER_BUTTON 2
#define PADDING_LABEL 0
#define PADDING_ZERO 0
#define FILL_FALSE false
#define EXPAND_FALSE false
#define FILL_TRUE true
#define EXPAND_TRUE true

#define NEW_WITH_NAME(VARIABLE, FUNCTION, ...) do { \
    VARIABLE = FUNCTION(__VA_ARGS__); \
    gtk_widget_set_name(VARIABLE, #VARIABLE); \
} while (0)

static bool is_first_run = false;

static void
free_text_info(void *data) {
    free2(data, SIZEOF(TextInfo));
    return;
}

static void
main_setup_tree_columns(GtkWidget *tree) {
    GActionMap *action_map = G_ACTION_MAP(cecup.application);
    int8 side = (int8)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(tree), "side"));

    if (g_action_map_lookup_action(action_map, "tree_dispatch") == NULL) {
        GSimpleAction *dispatch = g_simple_action_new("tree_dispatch", G_VARIANT_TYPE_INT32);
        GSimpleAction *ignore = g_simple_action_new("ignore", G_VARIANT_TYPE_STRING);

        g_action_map_add_action(action_map, G_ACTION(dispatch));
        g_action_map_add_action(action_map, G_ACTION(ignore));

        g_signal_connect(dispatch, "activate", G_CALLBACK(on_menu_dispatch),      NULL);
        g_signal_connect(ignore,   "activate", G_CALLBACK(on_menu_ignore_action), NULL);
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

        if (side == L) {
            g_object_set_data(G_OBJECT(column), "col_id", GINT_TO_POINTER(COL_SRC_ACTION));
        } else {
            g_object_set_data(G_OBJECT(column), "col_id", GINT_TO_POINTER(COL_DST_ACTION));
        }
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

        if (side == L) {
            g_object_set_data(G_OBJECT(column), "col_id", GINT_TO_POINTER(COL_SRC_PATH));
        } else {
            g_object_set_data(G_OBJECT(column), "col_id", GINT_TO_POINTER(COL_DST_PATH));
        }
        gtk_column_view_append_column(GTK_COLUMN_VIEW(tree), column);
        g_object_unref(column);
    }

    {
        GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
        GtkSorter *sorter = GTK_SORTER(gtk_string_sorter_new(NULL));
        TextInfo *text_info = malloc2(SIZEOF(*text_info));
        GtkColumnViewColumn *column = gtk_column_view_column_new(_("Size"), factory);

        text_info->side = side;
        text_info->type = COLUMN_SIZE;
        g_object_set_data_full(G_OBJECT(factory), "text_info", text_info, free_text_info);

        g_signal_connect(factory, "setup", G_CALLBACK(column_text_setup), NULL);
        g_signal_connect(factory, "bind", G_CALLBACK(column_text_bind), text_info);

        gtk_column_view_column_set_resizable(column, TRUE);
        gtk_column_view_column_set_sorter(column, sorter);

        if (side == L) {
            g_object_set_data(G_OBJECT(column), "col_id", GINT_TO_POINTER(COL_SRC_SIZE));
        } else {
            g_object_set_data(G_OBJECT(column), "col_id", GINT_TO_POINTER(COL_DST_SIZE));
        }
        gtk_column_view_append_column(GTK_COLUMN_VIEW(tree), column);
        g_object_unref(column);
    }

    {
        GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
        GtkSorter *sorter = GTK_SORTER(gtk_string_sorter_new(NULL));
        TextInfo *text_info = malloc2(SIZEOF(*text_info));
        GtkColumnViewColumn *column = gtk_column_view_column_new(_("Modification Time"), factory);

        text_info->side = side;
        text_info->type = COLUMN_MTIME;
        g_object_set_data_full(G_OBJECT(factory), "text_info", text_info, free_text_info);

        g_signal_connect(factory, "setup", G_CALLBACK(column_text_setup), NULL);
        g_signal_connect(factory, "bind", G_CALLBACK(column_text_bind), text_info);

        gtk_column_view_column_set_expand(column, TRUE);
        gtk_column_view_column_set_resizable(column, TRUE);
        gtk_column_view_column_set_sorter(column, sorter);

        if (side == L) {
            g_object_set_data(G_OBJECT(column), "col_id", GINT_TO_POINTER(COL_SRC_MTIME));
        } else {
            g_object_set_data(G_OBJECT(column), "col_id", GINT_TO_POINTER(COL_DST_MTIME));
        }
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
    GtkWidget *v_paned;
    GtkWidget *top_vbox;
    GtkWidget *footer_vbox;
    GtkWidget *paned_trees;

    GtkWidget *vbox[2];
    GtkWidget *entry_hbox[2];
    GtkWidget *scroll[2];
    GtkWidget *progress_vbox;
    GtkWidget *paths_hbox;

    char src_path_buffer[MAX_PATH_LENGTH];
    char dst_path_buffer[MAX_PATH_LENGTH];

    (void)user_data;

    {
        static char *base_css[] = {
            "columnview row { min-height: 0px; }",
            "columnview cell { padding: 0px; }",
            "paned > separator { min-width: 10px; min-height: 10px; }",
            "scrollbar.vertical slider { min-width: 12px; }",
            "scrollbar.horizontal slider { min-height: 8px; }",
            "progressbar text { font-size: 11pt; font-weight: bold; }"
        };
        GtkCssProvider *css_provider;
        char css_buffer[BUFSIZ];
        int32 offset;

        css_provider = gtk_css_provider_new();
        offset = 0;

        for (int32 i = 0; i < LENGTH(base_css); i += 1) {
            int32 n;
            n = snprintf2(css_buffer + offset, SIZEOF(css_buffer) - offset,
                          "%s\n", base_css[i]);
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

    NEW_WITH_NAME(cecup.gtk_window, gtk_application_window_new, application);
    gtk_window_set_title(GTK_WINDOW(cecup.gtk_window), "cecup");
    gtk_window_set_default_size(GTK_WINDOW(cecup.gtk_window), 1100, 800);

    g_signal_connect(cecup.gtk_window, "destroy", G_CALLBACK(on_window_destroy), NULL);

    NEW_WITH_NAME(main_vbox, gtk_box_new, GTK_ORIENTATION_VERTICAL, SPACING_BOX);
    gtk_window_set_child(GTK_WINDOW(cecup.gtk_window), main_vbox);

    NEW_WITH_NAME(header_vbox, gtk_box_new, GTK_ORIENTATION_VERTICAL, SPACING_BOX);
    gtk_widget_set_margin_start(header_vbox, SPACING_BOX);
    gtk_widget_set_margin_end(header_vbox, SPACING_BOX);
    gtk_widget_set_margin_top(header_vbox, SPACING_BOX);
    gtk_widget_set_margin_bottom(header_vbox, SPACING_BOX);
    gtk_box_append(GTK_BOX(main_vbox), header_vbox);

    NEW_WITH_NAME(options_hbox, gtk_box_new, GTK_ORIENTATION_HORIZONTAL, SPACING_BOX);
    gtk_widget_set_hexpand(options_hbox, TRUE);

    NEW_WITH_NAME(cecup.ignore_button, gtk_button_new);
    NEW_WITH_NAME(cecup.check_fs_button, gtk_check_button_new);
    NEW_WITH_NAME(cecup.delete_ignored_button, gtk_check_button_new);
    NEW_WITH_NAME(cecup.delete_after_button, gtk_check_button_new);

    gtk_button_set_label(GTK_BUTTON(cecup.ignore_button),  _("Edit Ignore Rules"));
    gtk_check_button_set_label(GTK_CHECK_BUTTON(cecup.check_fs_button),
                               _("Protect same-drive sync"));
    gtk_check_button_set_label(GTK_CHECK_BUTTON(cecup.delete_ignored_button),
                               _("Remove ignored items"));
    gtk_check_button_set_label(GTK_CHECK_BUTTON(cecup.delete_after_button),
                               _("Sync 100%"));

    gtk_widget_set_tooltip_text(cecup.ignore_button,
                                _("Edit the list of filename patterns to ignore"));
    gtk_widget_set_tooltip_text(cecup.check_fs_button,
                                _("Prevent copying if original and backup are on the same disk"));
    gtk_widget_set_tooltip_text(cecup.delete_ignored_button,
                                _("Remove files from backup if they were added to the ignore list"));
    gtk_widget_set_tooltip_text(cecup.delete_after_button,
                                _("Delete files in backup that do not exist in the original"));

    NEW_WITH_NAME(cecup.diff_entry, gtk_entry_new);
    NEW_WITH_NAME(cecup.term_entry, gtk_entry_new);

    gtk_widget_set_tooltip_text(cecup.diff_entry,
                                _("Executable used for comparing files"));
    gtk_widget_set_tooltip_text(cecup.term_entry,
                                _("Terminal emulator used to launch the diff tool"));

    NEW_WITH_NAME(reset_button, gtk_button_new);
    gtk_button_set_label(GTK_BUTTON(reset_button), _("Defaults"));
    gtk_widget_set_tooltip_text(reset_button, _("Restore original settings"));

    gtk_box_append(GTK_BOX(options_hbox), cecup.ignore_button);
    gtk_widget_set_margin_start(cecup.ignore_button, PADDING_BUTTON);
    gtk_widget_set_margin_end(cecup.ignore_button, PADDING_BUTTON);

    gtk_box_append(GTK_BOX(options_hbox), cecup.check_fs_button);
    gtk_widget_set_margin_start(cecup.check_fs_button, PADDING_BUTTON);
    gtk_widget_set_margin_end(cecup.check_fs_button, PADDING_BUTTON);

    gtk_box_append(GTK_BOX(options_hbox), cecup.delete_ignored_button);
    gtk_widget_set_margin_start(cecup.delete_ignored_button, PADDING_BUTTON);
    gtk_widget_set_margin_end(cecup.delete_ignored_button, PADDING_BUTTON);

    gtk_box_append(GTK_BOX(options_hbox), cecup.delete_after_button);
    gtk_widget_set_margin_start(cecup.delete_after_button, PADDING_BUTTON);
    gtk_widget_set_margin_end(cecup.delete_after_button, PADDING_BUTTON);

    gtk_box_append(GTK_BOX(options_hbox), gtk_label_new(_("Diff Tool:")));
    gtk_box_append(GTK_BOX(options_hbox), cecup.diff_entry);

    gtk_editable_set_text(GTK_EDITABLE(cecup.diff_entry), "diff");
    gtk_box_append(GTK_BOX(options_hbox), gtk_label_new(_("Terminal:")));
    gtk_box_append(GTK_BOX(options_hbox), cecup.term_entry);
    gtk_editable_set_text(GTK_EDITABLE(cecup.term_entry), "xterm");

    gtk_box_append(GTK_BOX(options_hbox), reset_button);
    gtk_box_append(GTK_BOX(header_vbox), options_hbox);

    NEW_WITH_NAME(progress_vbox, gtk_box_new, GTK_ORIENTATION_VERTICAL, 2);
    NEW_WITH_NAME(cecup.progress_bar, gtk_progress_bar_new);

    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(cecup.progress_bar), TRUE);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(cecup.progress_bar), _("Analyzing changes"));

    gtk_box_append(GTK_BOX(progress_vbox), cecup.progress_bar);
    gtk_box_append(GTK_BOX(header_vbox), progress_vbox);
    gtk_widget_set_margin_bottom(progress_vbox, 5);

    NEW_WITH_NAME(search_hbox, gtk_box_new, GTK_ORIENTATION_HORIZONTAL, SPACING_BOX);
    NEW_WITH_NAME(cecup.search_entry, gtk_entry_new);

    gtk_widget_set_margin_start(search_hbox, 10);
    gtk_widget_set_margin_end(search_hbox, 10);
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

    NEW_WITH_NAME(cecup.stats_label, gtk_label_new, "");

    gtk_label_set_label(GTK_LABEL(cecup.stats_label), _("✅ Everything ready"));
    gtk_box_append(GTK_BOX(search_hbox), cecup.stats_label);
    gtk_widget_set_margin_start(cecup.stats_label, 10);
    gtk_widget_set_margin_end(cecup.stats_label, 10);

    NEW_WITH_NAME(cecup.select_visible_button, gtk_button_new);
    NEW_WITH_NAME(cecup.unselect_button, gtk_button_new);

    gtk_button_set_label(GTK_BUTTON(cecup.select_visible_button), _("Select all visible"));
    gtk_button_set_label(GTK_BUTTON(cecup.unselect_button),       _("Unselect all"));

    gtk_widget_set_tooltip_text(cecup.select_visible_button,
                                _("Select all files which currently are on the list below"));
    gtk_widget_set_tooltip_text(cecup.unselect_button,
                                _("Unselect all files which are currently selected"));

    gtk_box_append(GTK_BOX(search_hbox), cecup.select_visible_button);
    gtk_box_append(GTK_BOX(search_hbox), cecup.unselect_button);

    gtk_box_append(GTK_BOX(main_vbox), search_hbox);

    NEW_WITH_NAME(filter_hbox, gtk_box_new, GTK_ORIENTATION_HORIZONTAL, SPACING_BOX);
    gtk_widget_set_halign(filter_hbox, GTK_ALIGN_CENTER);

    NEW_WITH_NAME(cecup.filter_new, gtk_toggle_button_new);
    NEW_WITH_NAME(cecup.filter_link, gtk_toggle_button_new);
    NEW_WITH_NAME(cecup.filter_update, gtk_toggle_button_new);
    NEW_WITH_NAME(cecup.filter_equal, gtk_toggle_button_new);
    NEW_WITH_NAME(cecup.filter_delete, gtk_toggle_button_new);
    NEW_WITH_NAME(cecup.filter_ignore, gtk_toggle_button_new);

    gtk_button_set_label(GTK_BUTTON(cecup.filter_new),    EMOJI_NEW);
    gtk_button_set_label(GTK_BUTTON(cecup.filter_link),   EMOJI_LINK);
    gtk_button_set_label(GTK_BUTTON(cecup.filter_update), EMOJI_UPDATE);
    gtk_button_set_label(GTK_BUTTON(cecup.filter_equal),  EMOJI_EQUAL);
    gtk_button_set_label(GTK_BUTTON(cecup.filter_delete), EMOJI_DELETE);
    gtk_button_set_label(GTK_BUTTON(cecup.filter_ignore), EMOJI_IGNORE);

    gtk_widget_set_tooltip_text(cecup.filter_new,    _("Show new files"));
    gtk_widget_set_tooltip_text(cecup.filter_link,   _("Show links"));
    gtk_widget_set_tooltip_text(cecup.filter_update, _("Show updates"));
    gtk_widget_set_tooltip_text(cecup.filter_equal,  _("Show equals"));
    gtk_widget_set_tooltip_text(cecup.filter_delete, _("Show files to be deleted"));
    gtk_widget_set_tooltip_text(cecup.filter_ignore, _("Show ignored files"));

    gtk_box_append(GTK_BOX(filter_hbox), cecup.filter_new);
    gtk_widget_set_margin_start(cecup.filter_new, PADDING_FILTER_BUTTON);
    gtk_widget_set_margin_end(cecup.filter_new, PADDING_FILTER_BUTTON);

    gtk_box_append(GTK_BOX(filter_hbox), cecup.filter_link);
    gtk_widget_set_margin_start(cecup.filter_link, PADDING_FILTER_BUTTON);
    gtk_widget_set_margin_end(cecup.filter_link, PADDING_FILTER_BUTTON);

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

    gtk_box_append(GTK_BOX(main_vbox), filter_hbox);

    NEW_WITH_NAME(v_paned, gtk_paned_new, GTK_ORIENTATION_VERTICAL);
    gtk_widget_set_vexpand(v_paned, TRUE);
    gtk_box_append(GTK_BOX(main_vbox), v_paned);

    NEW_WITH_NAME(top_vbox, gtk_box_new, GTK_ORIENTATION_VERTICAL, 0);
    gtk_paned_set_start_child(GTK_PANED(v_paned), top_vbox);

    NEW_WITH_NAME(paned_trees, gtk_paned_new, GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_vexpand(paned_trees, TRUE);
    gtk_box_append(GTK_BOX(top_vbox), paned_trees);

    NEW_WITH_NAME(vbox[L], gtk_box_new, GTK_ORIENTATION_VERTICAL, 5);
    NEW_WITH_NAME(scroll[L], gtk_scrolled_window_new);
    gtk_scrolled_window_set_overlay_scrolling(GTK_SCROLLED_WINDOW(scroll[L]), FALSE);

    {
        GtkSelectionModel *selection_model;
        GListModel *list_model = g_object_ref(cecup.store);
        selection_model = GTK_SELECTION_MODEL(gtk_single_selection_new(list_model));
        NEW_WITH_NAME(cecup.tree[L], gtk_column_view_new, selection_model);
    }

    g_object_set_data(G_OBJECT(cecup.tree[L]), "side", GINT_TO_POINTER(L));
    main_setup_tree_columns(cecup.tree[L]);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll[L]), cecup.tree[L]);
    gtk_box_append(GTK_BOX(vbox[L]), scroll[L]);
    gtk_widget_set_vexpand(scroll[L], TRUE);
    gtk_paned_set_start_child(GTK_PANED(paned_trees), vbox[L]);

    NEW_WITH_NAME(vbox[R], gtk_box_new, GTK_ORIENTATION_VERTICAL, 5);
    NEW_WITH_NAME(scroll[R], gtk_scrolled_window_new);
    gtk_scrolled_window_set_overlay_scrolling(GTK_SCROLLED_WINDOW(scroll[R]), FALSE);

    {
        GtkSelectionModel *selection_model;
        GListModel *list_model = g_object_ref(cecup.store);
        selection_model = GTK_SELECTION_MODEL(gtk_single_selection_new(list_model));
        NEW_WITH_NAME(cecup.tree[R], gtk_column_view_new, selection_model);
    }

    g_object_set_data(G_OBJECT(cecup.tree[R]), "side", GINT_TO_POINTER(R));
    main_setup_tree_columns(cecup.tree[R]);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll[R]), cecup.tree[R]);
    gtk_box_append(GTK_BOX(vbox[R]), scroll[R]);
    gtk_widget_set_vexpand(scroll[R], TRUE);
    gtk_paned_set_end_child(GTK_PANED(paned_trees), vbox[R]);

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

    NEW_WITH_NAME(paths_hbox, gtk_box_new, GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_start(paths_hbox, 10);
    gtk_widget_set_margin_end(paths_hbox, 10);
    gtk_widget_set_margin_top(paths_hbox, 10);
    gtk_widget_set_margin_bottom(paths_hbox, 10);

    NEW_WITH_NAME(entry_hbox[L], gtk_box_new, GTK_ORIENTATION_HORIZONTAL, 5);
    NEW_WITH_NAME(cecup.dir_entry[L], gtk_entry_new);
    NEW_WITH_NAME(cecup.browse_button[L], gtk_button_new);

    gtk_editable_set_text(GTK_EDITABLE(cecup.dir_entry[L]), src_path_buffer);

    gtk_button_set_label(GTK_BUTTON(cecup.browse_button[L]), _("Select Folder"));
    gtk_widget_set_tooltip_text(cecup.dir_entry[L], _("Folder containing original files"));
    gtk_widget_set_tooltip_text(cecup.browse_button[L], _("Open browser to select"
                                                          " the folder containing original files"));

    gtk_widget_set_hexpand(cecup.dir_entry[L], TRUE);
    gtk_box_append(GTK_BOX(entry_hbox[L]), cecup.dir_entry[L]);
    gtk_box_append(GTK_BOX(entry_hbox[L]), cecup.browse_button[L]);
    gtk_widget_set_hexpand(entry_hbox[L], TRUE);
    gtk_box_append(GTK_BOX(paths_hbox), entry_hbox[L]);

    NEW_WITH_NAME(cecup.invert_button, gtk_button_new);
    gtk_button_set_label(GTK_BUTTON(cecup.invert_button), "<--->");
    gtk_widget_set_tooltip_text(cecup.invert_button, _("Invert Original and Backup"));
    gtk_box_append(GTK_BOX(paths_hbox), cecup.invert_button);

    NEW_WITH_NAME(entry_hbox[R], gtk_box_new, GTK_ORIENTATION_HORIZONTAL, 5);
    NEW_WITH_NAME(cecup.dir_entry[R], gtk_entry_new);
    NEW_WITH_NAME(cecup.browse_button[R], gtk_button_new);

    gtk_editable_set_text(GTK_EDITABLE(cecup.dir_entry[R]), dst_path_buffer);

    gtk_button_set_label(GTK_BUTTON(cecup.browse_button[R]), _("Select Folder"));
    gtk_widget_set_tooltip_text(cecup.dir_entry[R], _("Folder where the backup will be stored"));
    gtk_widget_set_tooltip_text(cecup.browse_button[R], _("Open browser to select"
                                                          " the folder where the backup will be stored"));

    gtk_widget_set_hexpand(cecup.dir_entry[R], TRUE);
    gtk_box_append(GTK_BOX(entry_hbox[R]), cecup.dir_entry[R]);
    gtk_box_append(GTK_BOX(entry_hbox[R]), cecup.browse_button[R]);
    gtk_widget_set_hexpand(entry_hbox[R], TRUE);
    gtk_box_append(GTK_BOX(paths_hbox), entry_hbox[R]);

    gtk_box_append(GTK_BOX(top_vbox), paths_hbox);

    NEW_WITH_NAME(button_hbox, gtk_box_new, GTK_ORIENTATION_HORIZONTAL, SPACING_BOX);
    gtk_widget_set_halign(button_hbox, GTK_ALIGN_CENTER);

    NEW_WITH_NAME(cecup.preview_button, gtk_button_new);
    NEW_WITH_NAME(cecup.stop_button, gtk_button_new);
    NEW_WITH_NAME(cecup.sync_button, gtk_button_new);

    gtk_button_set_label(GTK_BUTTON(cecup.preview_button), _("🔎 Analyze"));
    gtk_button_set_label(GTK_BUTTON(cecup.stop_button),    _("⏹️ Stop"));
    gtk_button_set_label(GTK_BUTTON(cecup.sync_button),    _("⏩ Apply Changes"));

    gtk_widget_set_tooltip_text(cecup.preview_button,
                                _("Check which files need to be copied or updated"));
    gtk_widget_set_tooltip_text(cecup.stop_button,
                                _("Cancel the current task"));
    gtk_widget_set_tooltip_text(cecup.sync_button,
                                _("Start copying and updating all files"));

    cecup.preview_dirty = true;
    gtk_widget_set_sensitive(cecup.stop_button, FALSE);

    gtk_box_append(GTK_BOX(button_hbox), cecup.preview_button);
    gtk_widget_set_margin_start(cecup.preview_button, PADDING_BUTTON);
    gtk_widget_set_margin_end(cecup.preview_button, PADDING_BUTTON);

    gtk_box_append(GTK_BOX(button_hbox), cecup.stop_button);
    gtk_widget_set_margin_start(cecup.stop_button, PADDING_BUTTON);
    gtk_widget_set_margin_end(cecup.stop_button, PADDING_BUTTON);

    gtk_box_append(GTK_BOX(button_hbox), cecup.sync_button);
    gtk_widget_set_margin_start(cecup.sync_button, PADDING_BUTTON);
    gtk_widget_set_margin_end(cecup.sync_button, PADDING_BUTTON);

    gtk_box_append(GTK_BOX(top_vbox), button_hbox);

    NEW_WITH_NAME(footer_vbox, gtk_box_new, GTK_ORIENTATION_VERTICAL, SPACING_BOX);
    gtk_widget_set_margin_top(footer_vbox, SPACING_BOX);
    gtk_widget_set_margin_bottom(footer_vbox, SPACING_BOX);
    gtk_box_append(GTK_BOX(top_vbox), footer_vbox);

    {
        GtkWidget *log_scroll;

        NEW_WITH_NAME(log_scroll, gtk_scrolled_window_new);

        gtk_scrolled_window_set_overlay_scrolling(GTK_SCROLLED_WINDOW(log_scroll), FALSE);
        gtk_widget_set_size_request(log_scroll, -1, 100);

        NEW_WITH_NAME(cecup.log_view, gtk_text_view_new);
        gtk_text_view_set_editable(GTK_TEXT_VIEW(cecup.log_view), FALSE);
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(cecup.log_view), GTK_WRAP_WORD_CHAR);
        cecup.log_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(cecup.log_view));
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(log_scroll), cecup.log_view);

        gtk_paned_set_end_child(GTK_PANED(v_paned), log_scroll);
    }

    {
        GtkAdjustment *l_adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scroll[L]));
        GtkAdjustment *r_adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scroll[R]));

        g_signal_connect(l_adj, "value-changed", G_CALLBACK(on_scroll_sync), r_adj);
        g_signal_connect(r_adj, "value-changed", G_CALLBACK(on_scroll_sync), l_adj);
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

    do {
        GKeyFile *key;
        char *value;

        key = g_key_file_new();
        if (!g_key_file_load_from_file(key, cecup.config_path, G_KEY_FILE_NONE, NULL)) {
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
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup.filter_link),
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
            gtk_check_button_set_active(GTK_CHECK_BUTTON(cecup.check_fs_button),
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

    cecup.entry_id[L] = g_signal_connect(cecup.dir_entry[L], "activate",
                                         G_CALLBACK(on_config_changed), NULL);
    cecup.entry_id[R] = g_signal_connect(cecup.dir_entry[R], "activate",
                                         G_CALLBACK(on_config_changed), NULL);

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
    g_signal_connect(cecup.filter_link,   "toggled", G_CALLBACK(on_filter_toggled), NULL);
    g_signal_connect(cecup.filter_update, "toggled", G_CALLBACK(on_filter_toggled), NULL);
    g_signal_connect(cecup.filter_equal,  "toggled", G_CALLBACK(on_filter_toggled), NULL);
    g_signal_connect(cecup.filter_delete, "toggled", G_CALLBACK(on_filter_toggled), NULL);
    g_signal_connect(cecup.filter_ignore, "toggled", G_CALLBACK(on_filter_toggled), NULL);

    g_signal_connect(cecup.diff_entry, "changed", G_CALLBACK(on_config_changed), NULL);
    g_signal_connect(cecup.term_entry, "changed", G_CALLBACK(on_config_changed), NULL);

    g_signal_connect(cecup.check_fs_button, "toggled", G_CALLBACK(on_preview_setting_toggled), NULL);
    g_signal_connect(cecup.delete_ignored_button, "toggled", G_CALLBACK(on_delete_ignored_toggled), NULL);
    g_signal_connect(cecup.delete_after_button, "toggled", G_CALLBACK(on_delete_after_toggled), NULL);

    gtk_window_present(GTK_WINDOW(cecup.gtk_window));

    if (is_first_run || DEBUGGING) {
        GtkWidget *dialog = gtk_dialog_new_with_buttons(_("Welcome to cecup"), GTK_WINDOW(cecup.gtk_window),
                                                        GTK_DIALOG_MODAL,
                                                        _("_OK"), GTK_RESPONSE_OK,
                                                        NULL);
        GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
        GtkWidget *scroll_welcome = gtk_scrolled_window_new();
        GtkWidget *label = gtk_label_new(NULL);

        gtk_window_set_default_size(GTK_WINDOW(dialog), 640, 480);

        gtk_widget_set_vexpand(scroll_welcome, TRUE);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll_welcome), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

        gtk_label_set_markup(GTK_LABEL(label), _(cecup_welcome_text));
        gtk_label_set_wrap(GTK_LABEL(label), TRUE);
        gtk_label_set_xalign(GTK_LABEL(label), 0.0);
        gtk_label_set_yalign(GTK_LABEL(label), 0.0);
        gtk_widget_set_margin_start(label, 15);
        gtk_widget_set_margin_end(label, 15);
        gtk_widget_set_margin_top(label, 15);
        gtk_widget_set_margin_bottom(label, 15);

        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll_welcome), label);
        gtk_box_append(GTK_BOX(content_area), scroll_welcome);

        g_signal_connect(dialog, "response", G_CALLBACK(gtk_window_destroy), NULL);
        gtk_widget_show(dialog);
    }

    return;
}

int32
main(int32 argc, char **argv) {
    int32 status;

    program = argv[0];
    timezone_init();

    ASSERT_EQUAL(!L, R);
    ASSERT_EQUAL(!R, L);

    disable_dbus_warning();

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

        bind_textdomain_codeset(QUOTE(GETTEXT_PACKAGE), "UTF-8");
        textdomain(QUOTE(GETTEXT_PACKAGE));
    }

    memset64(&cecup, 0, SIZEOF(cecup));

    cecup.arena = arena_create(SIZEMB(64), "cecup.arena");
    pthread_mutex_init(&cecup.arena_mutex, NULL);
    pthread_mutex_init(&cecup.stop_lock, NULL);

    cecup.rows_len = 0;
    cecup.rows_capacity = INITIAL_CAPACITY;

    cecup.rows[L] = malloc2(cecup.rows_capacity*SIZEOF(*(cecup.rows[L])));
    cecup.rows[R] = malloc2(cecup.rows_capacity*SIZEOF(*(cecup.rows[R])));
    cecup.rows_visible = malloc2(cecup.rows_capacity*SIZEOF(*(cecup.rows_visible)));
    cecup.rows_selected = malloc2(cecup.rows_capacity*SIZEOF(*(cecup.rows_selected)));

    cecup.ntransfers = 0;
    cecup.transfers_capacity = INITIAL_CAPACITY;
    cecup.transfers = malloc2(cecup.transfers_capacity*SIZEOF(*(cecup.transfers)));
    cecup.transfers_lens = malloc2(cecup.transfers_capacity*SIZEOF(*(cecup.transfers_lens)));
    cecup.transfer_set = hash_create_transfer_set(INITIAL_CAPACITY, "cecup.transfer_set");

    cecup.ndeletions = 0;
    cecup.deletions_capacity = INITIAL_CAPACITY;
    cecup.deletions = malloc2(cecup.deletions_capacity*SIZEOF(*(cecup.deletions)));
    cecup.deletions_lens = malloc2(cecup.deletions_capacity*SIZEOF(*(cecup.deletions_lens)));
    cecup.deletion_set = hash_create_deletion_set(INITIAL_CAPACITY, "cecup.deletion_set");

    traversal_allocate(&cecup.traversal[L], L);
    traversal_allocate(&cecup.traversal[R], R);

    cecup.ignore_patterns = NULL;
    cecup.ignore_capacity = 0;
    cecup.ignore_count = 0;

    cecup.sort_col = COL_SRC_PATH;
    cecup.sort_order = GTK_SORT_ASCENDING;

    {
        char xdg_buffer[MAX_PATH_LENGTH];
        char config_base[MAX_PATH_LENGTH];
        char *XDG_CONFIG_HOME;

        if ((XDG_CONFIG_HOME = getenv("XDG_CONFIG_HOME")) == NULL) {
            char *HOME;

            if ((HOME = getenv("HOME")) == NULL) {
                error("HOME is not defined. Fix your system.\n");
                fatal(EXIT_FAILURE);
            }
            SNPRINTF(xdg_buffer, "%s/.config", HOME);
            XDG_CONFIG_HOME = xdg_buffer;
        }
        SNPRINTF(config_base, "%s/cecup", XDG_CONFIG_HOME);

        if (access(config_base, F_OK) < 0) {
            pid_t child_cp;

            g_mkdir_with_parents(config_base, 0755);
            is_first_run = true;

            switch (child_cp = fork()) {
            case -1:
                error("Error forking: %s.\n", strerror(errno));
                fatal(EXIT_FAILURE);
            case 0:
            {
                char cmd[MAX_PATH_LENGTH];
                char *args_cp[] = {
                    "cp",
                    "-r",
                    "/etc/cecup/.",
                    config_base,
                    NULL,
                };

                execvp(args_cp[0], args_cp);
                STRING_FROM_ARRAY(cmd, " ", args_cp, LENGTH(args_cp) - 1);
                error("Error executing\n"
                      "%s\n"
                      "to copy configuration files: %s.\n", cmd, strerror(errno));
                _exit(EXIT_FAILURE);
            }
            default:
                while (waitpid(child_cp, NULL, 0) < 0) {
                    if (errno == EINTR) {
                        continue;
                    }
                    error("Error waiting for cp: %s.\n", strerror(errno));
                    break;
                }
                break;
            }
        }

        SNPRINTF(cecup.ignore_path, "%s/ignore.conf", config_base);
        SNPRINTF(cecup.config_path, "%s/cecup.conf", config_base);
    }

    cecup.store = G_LIST_MODEL(cecup_list_model_new());
    stop_working(false);

    cecup.application = gtk_application_new("com.cecup.app", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(cecup.application, "activate", G_CALLBACK(main_application_run), NULL);
    status = g_application_run(G_APPLICATION(cecup.application), argc, argv);

    g_object_unref(cecup.application);
    g_object_unref(cecup.store);

    free2(cecup.rows[L], cecup.rows_capacity*SIZEOF(*(cecup.rows[L])));
    free2(cecup.rows[R], cecup.rows_capacity*SIZEOF(*(cecup.rows[R])));
    free2(cecup.rows_visible, cecup.rows_capacity*SIZEOF(*(cecup.rows_visible)));
    free2(cecup.rows_selected, cecup.rows_capacity*SIZEOF(uint8));

    free2(cecup.transfers, cecup.transfers_capacity*SIZEOF(*(cecup.transfers)));
    free2(cecup.transfers_lens, cecup.transfers_capacity*SIZEOF(*(cecup.transfers_lens)));
    hash_destroy_transfer_set(cecup.transfer_set);

    free2(cecup.deletions, cecup.deletions_capacity*SIZEOF(*(cecup.deletions)));
    free2(cecup.deletions_lens, cecup.deletions_capacity*SIZEOF(*(cecup.deletions_lens)));
    hash_destroy_deletion_set(cecup.deletion_set);

    free2(cecup.src_base, cecup.src_base_len + 1);
    free2(cecup.dst_base, cecup.dst_base_len + 1);

    arena_destroy(cecup.arena);

    traversal_free(&cecup.traversal[L]);
    traversal_free(&cecup.traversal[R]);

    xpthread_mutex_destroy(&cecup.arena_mutex);
    xpthread_mutex_destroy(&cecup.stop_lock);

    exit(status);
}
