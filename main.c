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

#include "cecup.h"
#include "util.c"
#include "on.c"
#include "i18n.h"

#define SPACING_BOX 5
#define PADDING_BUTTON 5
#define PADDING_FILTER_BUTTON 2
#define PADDING_LABEL 0
#define PADDING_ZERO 0
#define FILL_FALSE false
#define EXPAND_FALSE false
#define FILL_TRUE true
#define EXPAND_TRUE true

CecupListModel *cecup_list_model_new(void);
CecupRow *cecup_row_proxy_get_row(CecupRowProxy *proxy);

static void
setup_selected_cb(GtkSignalListItemFactory *factory,
                  GtkListItem *list_item, void *data) {
    GtkWidget *check;

    (void)factory;
    (void)data;

    check = gtk_check_button_new();
    gtk_widget_set_halign(check, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(check, GTK_ALIGN_CENTER);
    g_signal_connect(check, "toggled", G_CALLBACK(on_cell_toggled), data);
    gtk_list_item_set_child(list_item, check);

    return;
}

static void
bind_selected_cb(GtkSignalListItemFactory *factory,
                 GtkListItem *list_item, void *data) {
    GtkWidget *check;
    CecupRowProxy *proxy;
    CecupRow *row;
    uint32 position;

    (void)factory;
    (void)data;

    check = gtk_list_item_get_child(list_item);
    proxy = CECUP_ROW_PROXY(gtk_list_item_get_item(list_item));
    row = cecup_row_proxy_get_row(proxy);
    position = gtk_list_item_get_position(list_item);

    g_signal_handlers_block_by_func(check, on_cell_toggled, NULL);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(check), row->selected);
    g_signal_handlers_unblock_by_func(check, on_cell_toggled, NULL);

    g_object_set_data(G_OBJECT(check), "cecup-row", row);
    g_object_set_data(G_OBJECT(check), "cecup-pos", GUINT_TO_POINTER(position));
    return;
}

static void
setup_text_cb(GtkSignalListItemFactory *factory,
              GtkListItem *list_item, void *data) {
    GtkWidget *label;

    (void)factory;
    (void)data;

    label = gtk_label_new(NULL);
    gtk_widget_set_halign(label, GTK_ALIGN_FILL);
    gtk_widget_set_valign(label, GTK_ALIGN_FILL);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_list_item_set_child(list_item, label);

    return;
}

static void
bind_action_cb(GtkSignalListItemFactory *factory,
               GtkListItem *list_item, void *data) {
    GtkWidget *label;
    CecupRowProxy *proxy;
    CecupRow *row;
    int32 side;
    enum CecupAction action;
    char class_name[32];
    char *classes[2];
    uint32 position;

    (void)factory;

    label = gtk_list_item_get_child(list_item);
    proxy = CECUP_ROW_PROXY(gtk_list_item_get_item(list_item));
    row = cecup_row_proxy_get_row(proxy);
    side = GPOINTER_TO_INT(data);
    position = gtk_list_item_get_position(list_item);

    if (side == L) {
        action = row->src_action;
    } else {
        action = row->dst_action;
    }

    gtk_label_set_text(GTK_LABEL(label), action_emojis[action]);

    SNPRINTF(class_name, "cell-color-%u", action);
    classes[0] = class_name;
    classes[1] = NULL;
    gtk_widget_set_css_classes(label, (const char **)classes);

    g_object_set_data(G_OBJECT(label), "cecup-row", row);
    g_object_set_data(G_OBJECT(label), "cecup-pos", GUINT_TO_POINTER(position));
    g_object_set_data(G_OBJECT(label), "cecup-col", GINT_TO_POINTER(1));

    return;
}

static void
setup_path_cb(GtkSignalListItemFactory *factory,
              GtkListItem *list_item, void *data) {
    GtkWidget *editable;
    GtkWidget *tree;
    GtkGesture *click;

    (void)factory;
    tree = data;

    editable = gtk_editable_label_new("");
    gtk_widget_set_halign(editable, GTK_ALIGN_FILL);
    gtk_widget_set_valign(editable, GTK_ALIGN_FILL);
    gtk_editable_set_alignment(GTK_EDITABLE(editable), 0.0);
    gtk_editable_set_width_chars(GTK_EDITABLE(editable), 1);

    g_signal_connect(editable, "notify::editing",
                     G_CALLBACK(on_path_editing_notify), tree);

    click = gtk_gesture_click_new();
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(click),
                                               GTK_PHASE_CAPTURE);
    g_signal_connect(click, "pressed", G_CALLBACK(on_path_click_pressed), tree);
    gtk_widget_add_controller(editable, GTK_EVENT_CONTROLLER(click));

    gtk_list_item_set_child(list_item, editable);

    return;
}

static void
bind_path_cb(GtkSignalListItemFactory *factory,
             GtkListItem *list_item, void *data) {
    GtkWidget *editable;
    CecupRowProxy *proxy;
    CecupRow *row;
    GtkWidget *tree;
    int32 side;
    enum CecupAction action;
    char class_name[32];
    char *classes[2];
    uint32 position;

    (void)factory;
    tree = data;

    editable = gtk_list_item_get_child(list_item);
    proxy = CECUP_ROW_PROXY(gtk_list_item_get_item(list_item));
    row = cecup_row_proxy_get_row(proxy);
    side = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(tree), "side"));
    position = gtk_list_item_get_position(list_item);

    if (side == L) {
        if (row->src_path) {
            gtk_editable_set_text(GTK_EDITABLE(editable), row->src_path);
        } else {
            gtk_editable_set_text(GTK_EDITABLE(editable), "");
        }
        action = row->src_action;
    } else {
        if (row->dst_path) {
            gtk_editable_set_text(GTK_EDITABLE(editable), row->dst_path);
        } else {
            gtk_editable_set_text(GTK_EDITABLE(editable), "");
        }
        action = row->dst_action;
    }

    SNPRINTF(class_name, "cell-color-%u", action);
    classes[0] = class_name;
    classes[1] = NULL;
    gtk_widget_set_css_classes(editable, (const char **)classes);

    g_object_set_data(G_OBJECT(editable), "cecup-row", row);
    g_object_set_data(G_OBJECT(editable), "cecup-col", GINT_TO_POINTER(2));
    g_object_set_data(G_OBJECT(editable), "cecup-pos",
                      GUINT_TO_POINTER(position));

    return;
}

static void
bind_size_cb(GtkSignalListItemFactory *factory,
             GtkListItem *list_item, void *data) {
    GtkWidget *label;
    CecupRowProxy *proxy;
    CecupRow *row;
    int32 side;
    enum CecupAction action;
    char class_name[32];
    char *classes[2];
    uint32 position;

    (void)factory;

    label = gtk_list_item_get_child(list_item);
    proxy = CECUP_ROW_PROXY(gtk_list_item_get_item(list_item));
    row = cecup_row_proxy_get_row(proxy);
    side = GPOINTER_TO_INT(data);
    position = gtk_list_item_get_position(list_item);

    if (side == L) {
        gtk_label_set_text(GTK_LABEL(label), row->src_size_text);
        action = row->src_action;
    } else {
        gtk_label_set_text(GTK_LABEL(label), row->dst_size_text);
        action = row->dst_action;
    }

    SNPRINTF(class_name, "cell-color-%u", action);
    classes[0] = class_name;
    classes[1] = NULL;
    gtk_widget_set_css_classes(label, (const char **)classes);

    g_object_set_data(G_OBJECT(label), "cecup-row", row);
    g_object_set_data(G_OBJECT(label), "cecup-pos", GUINT_TO_POINTER(position));
    g_object_set_data(G_OBJECT(label), "cecup-col", GINT_TO_POINTER(3));
    return;
}

static void
bind_mtime_cb(GtkSignalListItemFactory *factory,
              GtkListItem *list_item, void *data) {
    GtkWidget *label;
    CecupRowProxy *proxy;
    CecupRow *row;
    int32 side;
    enum CecupAction action;
    char class_name[32];
    char *classes[2];
    uint32 position;

    (void)factory;

    label = gtk_list_item_get_child(list_item);
    proxy = CECUP_ROW_PROXY(gtk_list_item_get_item(list_item));
    row = cecup_row_proxy_get_row(proxy);
    side = GPOINTER_TO_INT(data);
    position = gtk_list_item_get_position(list_item);

    if (side == L) {
        gtk_label_set_text(GTK_LABEL(label), row->src_mtime_text);
        action = row->src_action;
    } else {
        gtk_label_set_text(GTK_LABEL(label), row->dst_mtime_text);
        action = row->dst_action;
    }

    SNPRINTF(class_name, "cell-color-%u", action);
    classes[0] = class_name;
    classes[1] = NULL;
    gtk_widget_set_css_classes(label, (const char **)classes);

    g_object_set_data(G_OBJECT(label), "cecup-row", row);
    g_object_set_data(G_OBJECT(label), "cecup-pos", GUINT_TO_POINTER(position));
    g_object_set_data(G_OBJECT(label), "cecup-col", GINT_TO_POINTER(4));
    return;
}

static void
main_setup_tree_columns(GtkWidget *tree,
                        enum CecupColumn col_act, enum CecupColumn col_path) {
    GtkColumnViewColumn *column;
    GtkEventController *key;
    GActionMap *action_map;
    int32 side;

    side = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(tree), "side"));
    action_map = G_ACTION_MAP(cecup.application);

    if (g_action_map_lookup_action(action_map, "tree_dispatch") == NULL) {
        GSimpleAction *dispatch;
        GSimpleAction *ignore;

        dispatch = g_simple_action_new("tree_dispatch", G_VARIANT_TYPE_INT32);
        g_signal_connect(dispatch, "activate",
                         G_CALLBACK(on_menu_dispatch), NULL);
        g_action_map_add_action(action_map, G_ACTION(dispatch));

        ignore = g_simple_action_new("ignore", G_VARIANT_TYPE_STRING);
        g_signal_connect(ignore, "activate",
                         G_CALLBACK(on_menu_ignore_action), NULL);
        g_action_map_add_action(action_map, G_ACTION(ignore));
    }

    {
        GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
        g_signal_connect(factory, "setup",
                         G_CALLBACK(setup_selected_cb), GINT_TO_POINTER(side));
        g_signal_connect(factory, "bind",
                         G_CALLBACK(bind_selected_cb), GINT_TO_POINTER(side));
        column = gtk_column_view_column_new(NULL, factory);
        gtk_column_view_append_column(GTK_COLUMN_VIEW(tree), column);
    }

    {
        GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
        GtkSorter *sorter = GTK_SORTER(gtk_string_sorter_new(NULL));

        g_signal_connect(factory, "setup",
                         G_CALLBACK(setup_text_cb), NULL);
        g_signal_connect(factory, "bind",
                         G_CALLBACK(bind_action_cb), GINT_TO_POINTER(side));

        column = gtk_column_view_column_new(_("Task"), factory);

        gtk_column_view_column_set_resizable(column, TRUE);
        gtk_column_view_column_set_sorter(column, sorter);

        g_object_set_data(G_OBJECT(column), "col_id", GINT_TO_POINTER(col_act));
        gtk_column_view_append_column(GTK_COLUMN_VIEW(tree), column);
    }

    {
        GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
        GtkSorter *sorter = GTK_SORTER(gtk_string_sorter_new(NULL));

        g_signal_connect(factory, "setup",
                         G_CALLBACK(setup_path_cb), tree);
        g_signal_connect(factory, "bind",
                         G_CALLBACK(bind_path_cb), tree);

        column = gtk_column_view_column_new(_("Name"), factory);

        gtk_column_view_column_set_resizable(column, TRUE);
        gtk_column_view_column_set_fixed_width(column, 500);
        gtk_column_view_column_set_sorter(column, sorter);

        g_object_set_data(G_OBJECT(column),
                          "col_id", GINT_TO_POINTER(col_path));
        gtk_column_view_append_column(GTK_COLUMN_VIEW(tree), column);
    }

    {
        GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
        GtkSorter *sorter = GTK_SORTER(gtk_string_sorter_new(NULL));

        g_signal_connect(factory, "setup",
                         G_CALLBACK(setup_text_cb), NULL);
        g_signal_connect(factory, "bind",
                         G_CALLBACK(bind_size_cb), GINT_TO_POINTER(side));

        column = gtk_column_view_column_new(_("Size"), factory);

        gtk_column_view_column_set_resizable(column, TRUE);
        gtk_column_view_column_set_sorter(column, sorter);

        g_object_set_data(G_OBJECT(column),
                          "col_id", GINT_TO_POINTER(COL_SIZE_RAW));
        gtk_column_view_append_column(GTK_COLUMN_VIEW(tree), column);
    }

    {
        GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
        GtkSorter *sorter = GTK_SORTER(gtk_string_sorter_new(NULL));

        g_signal_connect(factory, "setup",
                         G_CALLBACK(setup_text_cb), NULL);
        g_signal_connect(factory, "bind",
                         G_CALLBACK(bind_mtime_cb), GINT_TO_POINTER(side));

        column = gtk_column_view_column_new(_("Modification Time"), factory);

        gtk_column_view_column_set_expand(column, TRUE);
        gtk_column_view_column_set_resizable(column, TRUE);
        gtk_column_view_column_set_sorter(column, sorter);

        g_object_set_data(G_OBJECT(column),
                          "col_id", GINT_TO_POINTER(COL_MTIME_RAW));
        gtk_column_view_append_column(GTK_COLUMN_VIEW(tree), column);
    }

    gtk_widget_set_has_tooltip(tree, TRUE);
    gtk_widget_set_focusable(tree, TRUE);

    g_signal_connect(tree, "query-tooltip", G_CALLBACK(on_tree_tooltip), NULL);
    {
        GtkSorter *sorter = gtk_column_view_get_sorter(GTK_COLUMN_VIEW(tree));
        g_signal_connect(sorter, "changed", G_CALLBACK(on_sort_changed), tree);
    }

    {
        GtkGesture *click;

        click = gtk_gesture_click_new();
        gtk_widget_add_controller(tree, GTK_EVENT_CONTROLLER(click));
        gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(click),
                                                   GTK_PHASE_CAPTURE);
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 0);
        g_signal_connect(click, "pressed",
                         G_CALLBACK(on_tree_button_press), NULL);
    }

    key = gtk_event_controller_key_new();
    gtk_widget_add_controller(tree, GTK_EVENT_CONTROLLER(key));
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(key),
                                               GTK_PHASE_BUBBLE);
    g_signal_connect(key, "key-pressed", G_CALLBACK(on_tree_key_press), NULL);

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
    GtkWidget *paned;

    GtkWidget *vbox[2];
    GtkWidget *entry_hbox[2];
    GtkWidget *browse[2];
    GtkWidget *scroll[2];
    GtkWidget *tree[2];
    GtkWidget *log_scroll;
    GtkWidget *progress_vbox;
    GtkWidget *paths_hbox;

    GtkAdjustment *l_adj;
    GtkAdjustment *r_adj;
    GtkSelectionModel *selection_model;

    char cwd[MAX_PATH_LENGTH];
    char *default_src;
    char *default_dst;
    char src_path_buffer[MAX_PATH_LENGTH];
    char dst_path_buffer[MAX_PATH_LENGTH];
    int32 dst_path_len;
    int32 src_path_len;

    (void)user_data;

    {
        GtkCssProvider *provider;
        char css[4096];
        int32 offset;
        int32 n;

        provider = gtk_css_provider_new();
        offset = 0;

        n = snprintf2(css + offset, SIZEOF(css) - offset,
                      "columnview row { min-height: 0px; }\n"
                      "columnview cell { padding: 0px; }\n");
        offset += n;

        for (int32 i = 0; i < LENGTH(colors); i += 1) {
            int32 m;

            if (colors[i] == NULL) {
                continue;
            }

            m = snprintf2(css + offset, SIZEOF(css) - offset,
                          "row:not(:selected)"
                          " .cell-color-%d { background-color: %s; }\n",
                          i, colors[i]);
            offset += m;
        }

        gtk_css_provider_load_from_string(provider, css);
        gtk_style_context_add_provider_for_display(
            gdk_display_get_default(),
            GTK_STYLE_PROVIDER(provider),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(provider);
    }

    cecup.gtk_window = gtk_application_window_new(application);
    gtk_window_set_title(GTK_WINDOW(cecup.gtk_window), "cecup");
    gtk_window_set_default_size(GTK_WINDOW(cecup.gtk_window), 1100, 800);

    g_signal_connect(cecup.gtk_window, "destroy",
                     G_CALLBACK(on_window_destroy), NULL);

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
    cecup.fix_button
        = gtk_button_new_with_label(_("🛠️ Rename problematic files"));

    gtk_widget_set_tooltip_text(
        cecup.preview_button,
        _("Check which files need to be copied or updated"));
    gtk_widget_set_tooltip_text(
        cecup.ignore_button, _("Edit the list of filename patterns to ignore"));

    {
        char tooltip[1024];
        int32 offset;

        offset = SNPRINTF(
            tooltip, "%s",
            _("Rename problematic filenames, the ones containing:\n"));
        for (int32 i = 0; i < LENGTH(replacements); i += 1) {
            int32 n;

            n = snprintf2(tooltip + offset, SIZEOF(tooltip) - offset,
                          " \"%s\"\n", replacements[i].problem);
            offset += n;
        }
        gtk_widget_set_tooltip_text(cecup.fix_button, tooltip);
    }

    cecup.stop_button = gtk_button_new_with_label(_("⏹️ Stop"));
    gtk_widget_set_tooltip_text(cecup.stop_button,
                                _("Cancel the current task"));
    cecup.sync_button = gtk_button_new_with_label(_("⏩ Apply Changes"));
    gtk_widget_set_tooltip_text(cecup.sync_button,
                                _("Start copying and updating all files"));
    gtk_widget_set_sensitive(cecup.stop_button, FALSE);

    gtk_box_append(GTK_BOX(button_hbox), cecup.ignore_button);
    gtk_widget_set_margin_start(cecup.ignore_button, PADDING_BUTTON);
    gtk_widget_set_margin_end(cecup.ignore_button, PADDING_BUTTON);

    gtk_box_append(GTK_BOX(button_hbox), cecup.fix_button);
    gtk_widget_set_margin_start(cecup.fix_button, PADDING_BUTTON);
    gtk_widget_set_margin_end(cecup.fix_button, PADDING_BUTTON);

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
    cecup.check_fs
        = gtk_check_button_new_with_label(_("Protect same-drive sync"));
    cecup.delete_excluded
        = gtk_check_button_new_with_label(_("Remove ignored items"));
    cecup.delete_after = gtk_check_button_new_with_label(_("Sync 100%"));

    gtk_widget_set_tooltip_text(
        cecup.check_fs,
        _("Prevent copying if original and backup are on the same disk"));
    gtk_widget_set_tooltip_text(
        cecup.delete_excluded,
        _("Remove files from backup if they were added to the ignore list"));
    gtk_widget_set_tooltip_text(
        cecup.delete_after,
        _("Delete files in backup that do not exist in the original"));

    cecup.diff_entry = gtk_entry_new();
    gtk_widget_set_tooltip_text(cecup.diff_entry,
                                _("Executable used for comparing files"));
    cecup.term_entry = gtk_entry_new();
    gtk_widget_set_tooltip_text(
        cecup.term_entry, _("Terminal emulator used to launch the diff tool"));

    reset_button = gtk_button_new_with_label(_("Defaults"));
    gtk_widget_set_tooltip_text(reset_button, _("Restore original settings"));

    gtk_box_append(GTK_BOX(options_hbox), cecup.check_fs);
    gtk_widget_set_margin_start(cecup.check_fs, PADDING_BUTTON);
    gtk_widget_set_margin_end(cecup.check_fs, PADDING_BUTTON);

    gtk_box_append(GTK_BOX(options_hbox), cecup.delete_excluded);
    gtk_widget_set_margin_start(cecup.delete_excluded, PADDING_BUTTON);
    gtk_widget_set_margin_end(cecup.delete_excluded, PADDING_BUTTON);

    gtk_box_append(GTK_BOX(options_hbox), cecup.delete_after);
    gtk_widget_set_margin_start(cecup.delete_after, PADDING_BUTTON);
    gtk_widget_set_margin_end(cecup.delete_after, PADDING_BUTTON);

    gtk_box_append(GTK_BOX(options_hbox), gtk_label_new(_("Diff Tool:")));
    gtk_box_append(GTK_BOX(options_hbox), cecup.diff_entry);

    gtk_editable_set_text(GTK_EDITABLE(cecup.diff_entry), "unidiff.bash");
    gtk_box_append(GTK_BOX(options_hbox), gtk_label_new(_("Terminal:")));
    gtk_box_append(GTK_BOX(options_hbox), cecup.term_entry);
    gtk_editable_set_text(GTK_EDITABLE(cecup.term_entry), "xterm");

    gtk_box_append(GTK_BOX(options_hbox), reset_button);
    gtk_box_append(GTK_BOX(header_vbox), options_hbox);

    progress_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    cecup.progress_preview = gtk_progress_bar_new();
    gtk_widget_set_tooltip_text(cecup.progress_preview,
                                _("Preview analysis progress"));

    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(cecup.progress_preview),
                                   TRUE);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(cecup.progress_preview),
                              _("Analyzing changes"));

    gtk_box_append(GTK_BOX(progress_vbox), cecup.progress_preview);
    gtk_box_append(GTK_BOX(header_vbox), progress_vbox);
    gtk_widget_set_margin_bottom(progress_vbox, 5);

    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        error("Error getting current working directory: %s.\n",
              strerror(errno));
        exit(EXIT_FAILURE);
    }

    src_path_len = SNPRINTF(src_path_buffer, "%s/a/", cwd);
    default_src = xmalloc(src_path_len + 1);
    memcpy64(default_src, src_path_buffer, src_path_len + 1);

    dst_path_len = SNPRINTF(dst_path_buffer, "%s/b/", cwd);
    default_dst = xmalloc(dst_path_len + 1);
    memcpy64(default_dst, dst_path_buffer, dst_path_len + 1);

    paths_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_start(paths_hbox, 10);
    gtk_widget_set_margin_end(paths_hbox, 10);
    gtk_widget_set_margin_top(paths_hbox, 10);
    gtk_widget_set_margin_bottom(paths_hbox, 10);
    gtk_box_append(GTK_BOX(main_vbox), paths_hbox);

    entry_hbox[L] = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    cecup.src_entry = gtk_entry_new();
    gtk_widget_set_tooltip_text(cecup.src_entry,
                                _("Folder containing your original files"));
    gtk_editable_set_text(GTK_EDITABLE(cecup.src_entry), default_src);
    browse[L] = gtk_button_new_with_label(_("Select Folder"));

    gtk_widget_set_hexpand(cecup.src_entry, TRUE);
    gtk_box_append(GTK_BOX(entry_hbox[L]), cecup.src_entry);
    gtk_box_append(GTK_BOX(entry_hbox[L]), browse[L]);
    gtk_widget_set_hexpand(entry_hbox[L], TRUE);
    gtk_box_append(GTK_BOX(paths_hbox), entry_hbox[L]);

    cecup.invert_button = gtk_button_new_with_label("<--->");
    gtk_widget_set_tooltip_text(cecup.invert_button,
                                _("Invert Original and Backup"));
    gtk_box_append(GTK_BOX(paths_hbox), cecup.invert_button);

    entry_hbox[R] = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    cecup.dst_entry = gtk_entry_new();
    gtk_widget_set_tooltip_text(cecup.dst_entry,
                                _("Folder where the backup will be stored"));
    gtk_editable_set_text(GTK_EDITABLE(cecup.dst_entry), default_dst);
    browse[R] = gtk_button_new_with_label(_("Select Folder"));

    gtk_widget_set_hexpand(cecup.dst_entry, TRUE);
    gtk_box_append(GTK_BOX(entry_hbox[R]), cecup.dst_entry);
    gtk_box_append(GTK_BOX(entry_hbox[R]), browse[R]);
    gtk_widget_set_hexpand(entry_hbox[R], TRUE);
    gtk_box_append(GTK_BOX(paths_hbox), entry_hbox[R]);

    search_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, SPACING_BOX);
    gtk_widget_set_margin_start(search_hbox, 10);
    gtk_widget_set_margin_end(search_hbox, 10);
    cecup.search_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(cecup.search_entry),
                                   _("Search files..."));
    gtk_entry_set_icon_from_icon_name(GTK_ENTRY(cecup.search_entry),
                                      GTK_ENTRY_ICON_PRIMARY,
                                      "system-search-symbolic");
    {
        GtkWidget *search_label = gtk_label_new(_("🔍"));
        gtk_box_append(GTK_BOX(search_hbox), search_label);
        gtk_widget_set_margin_start(search_label, 5);
        gtk_widget_set_margin_end(search_label, 5);
    }
    gtk_widget_set_hexpand(cecup.search_entry, TRUE);
    gtk_box_append(GTK_BOX(search_hbox), cecup.search_entry);
    gtk_box_append(GTK_BOX(main_vbox), search_hbox);

    v_paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    gtk_widget_set_vexpand(v_paned, TRUE);
    gtk_box_append(GTK_BOX(main_vbox), v_paned);
    paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_start_child(GTK_PANED(v_paned), paned);

    vbox[L] = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    scroll[L] = gtk_scrolled_window_new();
    selection_model = GTK_SELECTION_MODEL(gtk_single_selection_new(cecup.store));
    tree[L] = gtk_column_view_new(selection_model);
    cecup.tree[L] = tree[L];
    g_object_set_data(G_OBJECT(tree[L]), "side", GINT_TO_POINTER(L));
    main_setup_tree_columns(tree[L], COL_SRC_ACTION, COL_SRC_PATH);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll[L]), tree[L]);
    gtk_box_append(GTK_BOX(vbox[L]), scroll[L]);
    gtk_widget_set_vexpand(scroll[L], TRUE);
    gtk_paned_set_start_child(GTK_PANED(paned), vbox[L]);

    vbox[R] = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    scroll[R] = gtk_scrolled_window_new();
    selection_model = GTK_SELECTION_MODEL(gtk_single_selection_new(cecup.store));
    tree[R] = gtk_column_view_new(selection_model);
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

    log_scroll = gtk_scrolled_window_new();
    gtk_widget_set_size_request(log_scroll, -1, 150);
    cecup.log_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(cecup.log_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(cecup.log_view),
                                GTK_WRAP_WORD_CHAR);
    cecup.log_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(cecup.log_view));
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(log_scroll),
                                  cecup.log_view);
    gtk_paned_set_end_child(GTK_PANED(v_paned), log_scroll);

    {
        GtkGesture *log_gesture = gtk_gesture_click_new();

        gtk_widget_add_controller(cecup.log_view,
                                  GTK_EVENT_CONTROLLER(log_gesture));
        gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(log_gesture),
                                                   GTK_PHASE_CAPTURE);
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(log_gesture), 0);
        g_signal_connect(log_gesture, "pressed",
                         G_CALLBACK(on_log_button_press), NULL);
    }

    {
        GSimpleAction *action_copy_all;
        GSimpleAction *action_copy_line;
        GActionMap *action_map = G_ACTION_MAP(cecup.application);

        action_copy_all = g_simple_action_new("copy_all", NULL);
        g_signal_connect(action_copy_all, "activate",
                         G_CALLBACK(on_log_copy), "all");
        g_action_map_add_action(action_map, G_ACTION(action_copy_all));

        action_copy_line = g_simple_action_new("copy_line",
                                               G_VARIANT_TYPE_INT32);
        g_signal_connect(action_copy_line, "activate",
                         G_CALLBACK(on_log_copy), "line");
        g_action_map_add_action(action_map, G_ACTION(action_copy_line));
    }

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

    gtk_box_append(GTK_BOX(main_vbox), filter_hbox);

    cecup.stats_label = gtk_label_new(_("✅ Everything ready"));
    gtk_box_append(GTK_BOX(main_vbox), cecup.stats_label);
    gtk_widget_set_margin_top(cecup.stats_label, 5);
    gtk_widget_set_margin_bottom(cecup.stats_label, 5);

    cecup.src_entry_id = g_signal_connect(cecup.src_entry, "activate",
                                          G_CALLBACK(on_config_changed), NULL);
    cecup.dst_entry_id = g_signal_connect(cecup.dst_entry, "activate",
                                          G_CALLBACK(on_config_changed), NULL);

    do {
        GKeyFile *key = g_key_file_new();
        char *value;

        if (!g_key_file_load_from_file(key, cecup.config_path,
                                       G_KEY_FILE_NONE, NULL)) {
            g_free(key);
            break;
        }

        if ((value = g_key_file_get_string(key, "Paths", "src", NULL))) {
            gtk_editable_set_text(GTK_EDITABLE(cecup.src_entry), value);
            g_free(value);
        }
        if ((value = g_key_file_get_string(key, "Paths", "dst", NULL))) {
            gtk_editable_set_text(GTK_EDITABLE(cecup.dst_entry), value);
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
            gtk_toggle_button_set_active(
                GTK_TOGGLE_BUTTON(cecup.filter_new),
                g_key_file_get_boolean(key, "Filters", "new", NULL));
        }
        if (g_key_file_has_key(key, "Filters", "hard", NULL)) {
            gtk_toggle_button_set_active(
                GTK_TOGGLE_BUTTON(cecup.filter_hard),
                g_key_file_get_boolean(key, "Filters", "hard", NULL));
        }
        if (g_key_file_has_key(key, "Filters", "update", NULL)) {
            gtk_toggle_button_set_active(
                GTK_TOGGLE_BUTTON(cecup.filter_update),
                g_key_file_get_boolean(key, "Filters", "update", NULL));
        }
        if (g_key_file_has_key(key, "Filters", "equal", NULL)) {
            gtk_toggle_button_set_active(
                GTK_TOGGLE_BUTTON(cecup.filter_equal),
                g_key_file_get_boolean(key, "Filters", "equal", NULL));
        }
        if (g_key_file_has_key(key, "Filters", "delete", NULL)) {
            gtk_toggle_button_set_active(
                GTK_TOGGLE_BUTTON(cecup.filter_delete),
                g_key_file_get_boolean(key, "Filters", "delete", NULL));
        }
        if (g_key_file_has_key(key, "Filters", "ignore", NULL)) {
            gtk_toggle_button_set_active(
                GTK_TOGGLE_BUTTON(cecup.filter_ignore),
                g_key_file_get_boolean(key, "Filters", "ignore", NULL));
        }
        if (g_key_file_has_key(key, "Options", "check_fs", NULL)) {
            gtk_check_button_set_active(
                GTK_CHECK_BUTTON(cecup.check_fs),
                g_key_file_get_boolean(key, "Options", "check_fs", NULL));
        }
        if (g_key_file_has_key(key, "Options", "delete_excluded", NULL)) {
            gtk_check_button_set_active(
                GTK_CHECK_BUTTON(cecup.delete_excluded),
                g_key_file_get_boolean(key, "Options", "delete_excluded",
                                       NULL));
        }
        if (g_key_file_has_key(key, "Options", "delete_after", NULL)) {
            gtk_check_button_set_active(
                GTK_CHECK_BUTTON(cecup.delete_after),
                g_key_file_get_boolean(key, "Options", "delete_after", NULL));
        }

        g_free(key);
    } while (0);

    g_signal_connect(browse[L], "clicked",
                     G_CALLBACK(on_browse_src), NULL);
    g_signal_connect(browse[R], "clicked",
                     G_CALLBACK(on_browse_dst), NULL);
    g_signal_connect(cecup.invert_button, "clicked",
                     G_CALLBACK(on_invert_clicked), NULL);
    g_signal_connect(cecup.preview_button, "clicked",
                     G_CALLBACK(on_preview_clicked), NULL);
    g_signal_connect(cecup.stop_button, "clicked",
                     G_CALLBACK(on_stop_clicked), NULL);
    g_signal_connect(cecup.sync_button, "clicked",
                     G_CALLBACK(on_sync_clicked), NULL);
    g_signal_connect(cecup.ignore_button, "clicked",
                     G_CALLBACK(on_ignore_clicked), NULL);
    g_signal_connect(cecup.fix_button, "clicked",
                     G_CALLBACK(on_fix_clicked), NULL);
    g_signal_connect(reset_button, "clicked",
                     G_CALLBACK(on_reset_clicked), NULL);

    g_signal_connect(cecup.search_entry, "changed",
                     G_CALLBACK(on_search_changed), NULL);

    g_signal_connect(cecup.filter_new, "toggled",
                     G_CALLBACK(on_filter_toggled), NULL);
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

    g_signal_connect(cecup.diff_entry, "changed",
                     G_CALLBACK(on_config_changed), NULL);
    g_signal_connect(cecup.term_entry, "changed",
                     G_CALLBACK(on_config_changed), NULL);

    g_signal_connect(cecup.check_fs, "toggled",
                     G_CALLBACK(on_preview_setting_toggled), NULL);
    g_signal_connect(cecup.delete_excluded, "toggled",
                     G_CALLBACK(on_delete_excluded_toggled), NULL);
    g_signal_connect(cecup.delete_after, "toggled",
                     G_CALLBACK(on_delete_after_toggled), NULL);

    cecup_get_dirs();

    gtk_window_present(GTK_WINDOW(cecup.gtk_window));

    return;
}

int32
main(int32 argc, char **argv) {
    int32 status;

    program = argv[0];
    (void)program_len;

    {
        char *locale_devel = "./po";
        char *locale_system = "/usr/share/locale/";
        char *locale_local_system = "/usr/local/share/locale/";

        if (setlocale(LC_ALL, "") == NULL) {
            error("Error setting locale: %s.\n", strerror(errno));
            exit(EXIT_FAILURE);
        }

        if (access(locale_devel, F_OK) == 0) {
            bindtextdomain("cecup", locale_devel);
        } else if (access(locale_system, F_OK) == 0) {
            bindtextdomain("cecup", locale_system);
        } else if (access(locale_local_system, F_OK) == 0) {
            bindtextdomain("cecup", locale_system);
        } else {
            error("Can't find any locale directory available.\n");
        }

        bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
        textdomain(GETTEXT_PACKAGE);
    }

    memset64(&cecup, 0, SIZEOF(cecup));

    cecup.changed_dirs = true;
    cecup.arena = arena_create(SIZEMB(64));
    g_mutex_init(&cecup.arena_mutex);

    cecup.rows_len = 0;
    cecup.rows_capacity = 256;
    cecup.rows = xmalloc(cecup.rows_capacity*SIZEOF(CecupRow *));
    cecup.rows_visible = xmalloc(cecup.rows_capacity*SIZEOF(CecupRow *));

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
            char cmd[4096];
            g_mkdir_with_parents(config_base, 0755);

            SNPRINTF(cmd, "cp -r /etc/cecup/* '%s/'", config_base);
            system(cmd);
        }

        SNPRINTF(cecup.ignore_path, "%s/ignore.conf", config_base);
        SNPRINTF(cecup.config_path, "%s/cecup.conf", config_base);
    }

    cecup.store = G_LIST_MODEL(cecup_list_model_new());

    cecup.application = gtk_application_new("com.cecup.app",
                                            G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(cecup.application, "activate",
                     G_CALLBACK(main_application_run), NULL);
    status = g_application_run(G_APPLICATION(cecup.application), argc, argv);

    g_object_unref(cecup.store);
    g_object_unref(cecup.application);

    XFREE(cecup.rows);
    XFREE(cecup.rows_visible);

    exit(status);
}
