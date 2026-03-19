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

#include "profiler.c"

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

#define CECUP_TYPE_CELL_RENDERER_TEXT (cecup_cell_renderer_text_get_type())
G_DECLARE_FINAL_TYPE(CecupCellRendererText, cecup_cell_renderer_text, CECUP,
                     CELL_RENDERER_TEXT, GtkCellRendererText)

struct _CecupCellRendererText {
    GtkCellRendererText parent_instance;
    char *raw_text;
    int32 text_len;
    char *raw_color;
};

G_DEFINE_TYPE(CecupCellRendererText, cecup_cell_renderer_text,
              GTK_TYPE_CELL_RENDERER_TEXT)

static void
cecup_cell_renderer_text_init(CecupCellRendererText *self) {
    self->raw_text = NULL;
    self->text_len = 0;
    self->raw_color = NULL;
    return;
}

static void
cecup_cell_renderer_text_snapshot(GtkCellRenderer *cell, GtkSnapshot *snapshot,
                                  GtkWidget *widget,
                                  const GdkRectangle *background_area,
                                  const GdkRectangle *cell_area,
                                  GtkCellRendererState flags) {
    CecupCellRendererText *self = CECUP_CELL_RENDERER_TEXT(cell);
    PangoLayout *layout;
    PangoContext *context;
    GdkRGBA color;
    int32 x_pad;
    int32 y_pad;
    GtkStyleContext *style_context = gtk_widget_get_style_context(widget);
    bool is_selected = (flags & GTK_CELL_RENDERER_SELECTED) != 0;
    cairo_t *cr;
    graphene_rect_t bounds;

    graphene_rect_init(&bounds,
                       (float)background_area->x,
                       (float)background_area->y,
                       (float)background_area->width,
                       (float)background_area->height);

    if ((cr = gtk_snapshot_append_cairo(snapshot, &bounds))) {
        if (!is_selected && self->raw_color) {
            if (gdk_rgba_parse(&color, self->raw_color)) {
                gdk_cairo_set_source_rgba(cr, &color);
                cairo_rectangle(cr, (double)background_area->x, (double)background_area->y,
                                (double)background_area->width, (double)background_area->height);
                cairo_fill(cr);
            }
        }

        if ((context = gtk_widget_get_pango_context(widget)) == NULL) {
            cairo_destroy(cr);
            return;
        }

        if ((layout = pango_layout_new(context)) == NULL) {
            cairo_destroy(cr);
            return;
        }

        pango_layout_set_text(layout, self->raw_text, self->text_len);
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
        pango_layout_set_width(layout, (cell_area->width) * PANGO_SCALE);

        gtk_cell_renderer_get_padding(cell, &x_pad, &y_pad);

        gtk_style_context_save(style_context);
        if (is_selected) {
            gtk_style_context_set_state(style_context, GTK_STATE_FLAG_SELECTED);
        }

        gtk_style_context_get_color(style_context, &color);
        gdk_cairo_set_source_rgba(cr, &color);

        cairo_move_to(cr, (double)cell_area->x + x_pad, (double)cell_area->y + y_pad);
        pango_cairo_show_layout(cr, layout);

        gtk_style_context_restore(style_context);
        g_object_unref(layout);
        cairo_destroy(cr);
    }

    return;
}

static void
cecup_cell_renderer_text_class_init(CecupCellRendererTextClass *klass) {
    GtkCellRendererClass *cell_class = GTK_CELL_RENDERER_CLASS(klass);
    cell_class->snapshot = cecup_cell_renderer_text_snapshot;
    return;
}

static GtkCellRenderer *
cecup_cell_renderer_text_new(void) {
    return g_object_new(CECUP_TYPE_CELL_RENDERER_TEXT, NULL);
}

static void setup_tree_columns(GtkWidget *tree, int32 col_act, int32 col_path);

static void
cell_data_func(GtkTreeViewColumn *col, GtkCellRenderer *renderer,
                GtkTreeModel *model, GtkTreeIter *iter, void *data) {
    GtkTreePath *tree_path = gtk_tree_model_get_path(model, iter);
    int32 col_id = GPOINTER_TO_INT(data);
    CecupRow *row;
    int32 row_idx;
    CecupCellRendererText *cecup_renderer = (CecupCellRendererText *)renderer;

    row_idx = gtk_tree_path_get_indices(tree_path)[0];
    gtk_tree_path_free(tree_path);

    if ((row_idx < 0) || (row_idx >= cecup.rows_visible_len)) {
        return;
    }
    row = cecup.rows_visible[row_idx];

    switch (col_id) {
    case COL_SELECTED:
        g_object_set(renderer, "active", row->selected, NULL);
        break;
    case COL_SRC_ACTION:
        cecup_renderer->raw_text = action_emojis[row->src_action];
        cecup_renderer->text_len = strlen32(cecup_renderer->raw_text);
        cecup_renderer->raw_color = colors[row->src_action];
        break;
    case COL_DST_ACTION:
        cecup_renderer->raw_text = action_emojis[row->dst_action];
        cecup_renderer->text_len = strlen32(cecup_renderer->raw_text);
        cecup_renderer->raw_color = colors[row->dst_action];
        break;
    case COL_SRC_PATH:
        cecup_renderer->raw_text = row->src_path;
        cecup_renderer->text_len = row->src_path ? row->path_len : 0;
        cecup_renderer->raw_color = colors[row->src_action];
        break;
    case COL_DST_PATH:
        cecup_renderer->raw_text = row->dst_path;
        cecup_renderer->text_len = row->dst_path ? row->path_len : 0;
        cecup_renderer->raw_color = colors[row->dst_action];
        break;
    case COL_SIZE_TEXT: {
        char *background;
        char *text;
        int32 text_len;

        if (col == gtk_tree_view_get_column(GTK_TREE_VIEW(cecup.l_tree), 3)) {
            background = colors[row->src_action];
            text = row->src_size_text;
            text_len = strlen32(row->src_size_text);
        } else {
            background = colors[row->dst_action];
            text = row->dst_size_text;
            text_len = strlen32(row->dst_size_text);
        }

        cecup_renderer->raw_text = text;
        cecup_renderer->text_len = text_len;
        cecup_renderer->raw_color = background;
        break;
    }
    case COL_MTIME_TEXT: {
        char *background;
        char *text;
        int32 text_len;

        if (col == gtk_tree_view_get_column(GTK_TREE_VIEW(cecup.l_tree), 4)) {
            background = colors[row->src_action];
            text = row->src_mtime_text;
            text_len = strlen32(row->src_mtime_text);
        } else {
            background = colors[row->dst_action];
            text = row->dst_mtime_text;
            text_len = strlen32(row->dst_mtime_text);
        }

        cecup_renderer->raw_text = text;
        cecup_renderer->text_len = text_len;
        cecup_renderer->raw_color = background;
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
    GtkCellRenderer *renderer_toggle = gtk_cell_renderer_toggle_new();
    GtkCellRenderer *renderer_text = cecup_cell_renderer_text_new();
    GtkCellRenderer *renderer_path = cecup_cell_renderer_text_new();
    GtkTreeViewColumn *column;
    GtkEventController *key;

    for (uint32 i = 0; i < LENGTH(context_menu_items); i += 1) {
        CecupMenuItem *menu_item = &context_menu_items[i];
        GSimpleAction *action = g_simple_action_new(menu_item->action, G_VARIANT_TYPE_INT32);
        g_signal_connect(action, "activate", G_CALLBACK(menu_item->callback), menu_item->variant);
        g_action_map_add_action(G_ACTION_MAP(cecup.application), G_ACTION(action));
    }

    gtk_tree_view_set_fixed_height_mode(GTK_TREE_VIEW(tree), TRUE);

    column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_pack_start(column, renderer_toggle, TRUE);
    gtk_tree_view_column_set_cell_data_func(
        column, renderer_toggle, cell_data_func, GINT_TO_POINTER(COL_SELECTED),
        NULL);
    g_signal_connect(renderer_toggle, "toggled", G_CALLBACK(on_cell_toggled),
                     NULL);
    gtk_tree_view_column_set_resizable(column, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);

    column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_title(column, _("Task"));
    gtk_tree_view_column_pack_start(column, renderer_text, TRUE);
    gtk_tree_view_column_set_cell_data_func(
        column, renderer_text, cell_data_func, GINT_TO_POINTER(col_act), NULL);
    gtk_tree_view_column_set_sort_column_id(column, col_act);
    gtk_tree_view_column_set_resizable(column, TRUE);
    gtk_tree_view_column_set_min_width(column, 80);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);

    g_signal_connect(renderer_path, "edited",
                     G_CALLBACK(on_path_edited), tree);
    g_signal_connect(renderer_path, "editing-started",
                     G_CALLBACK(on_path_editing_started), tree);
    g_object_set(renderer_path, "editable", TRUE, NULL);

    column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_title(column, _("Name"));
    gtk_tree_view_column_pack_start(column, renderer_path, TRUE);
    gtk_tree_view_column_set_cell_data_func(
        column, renderer_path, cell_data_func, GINT_TO_POINTER(col_path), NULL);
    gtk_tree_view_column_set_sort_column_id(column, col_path);
    gtk_tree_view_column_set_resizable(column, TRUE);
    gtk_tree_view_column_set_expand(column, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);

    column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_title(column, _("Size"));
    gtk_tree_view_column_pack_start(column, renderer_text, TRUE);
    gtk_tree_view_column_set_cell_data_func(
        column, renderer_text, cell_data_func, GINT_TO_POINTER(COL_SIZE_TEXT),
        NULL);
    gtk_tree_view_column_set_sort_column_id(column, COL_SIZE_RAW);
    gtk_tree_view_column_set_resizable(column, TRUE);
    gtk_tree_view_column_set_min_width(column, 100);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);

    column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_title(column, _("Modification Time"));
    gtk_tree_view_column_pack_start(column, renderer_text, TRUE);
    gtk_tree_view_column_set_cell_data_func(
        column, renderer_text, cell_data_func, GINT_TO_POINTER(COL_MTIME_TEXT),
        NULL);
    gtk_tree_view_column_set_sort_column_id(column, COL_MTIME_RAW);
    gtk_tree_view_column_set_resizable(column, TRUE);
    gtk_tree_view_column_set_min_width(column, 150);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);

    gtk_widget_set_has_tooltip(tree, TRUE);

    g_signal_connect(tree, "query-tooltip",
                     G_CALLBACK(on_tree_tooltip), NULL);

    {
        GtkGesture *click = gtk_gesture_click_new();
        gtk_widget_add_controller(tree, GTK_EVENT_CONTROLLER(click));
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 0);
        g_signal_connect(click, "pressed", G_CALLBACK(on_tree_button_press), NULL);
    }

    key = gtk_event_controller_key_new();
    gtk_widget_add_controller(tree, GTK_EVENT_CONTROLLER(key));
    g_signal_connect(key, "key-pressed", G_CALLBACK(on_tree_key_press), NULL);

    return;
}

static void
activate(GtkApplication *application, gpointer user_data) {
    GtkWidget *main_vbox;
    GtkWidget *header_vbox;
    GtkWidget *button_hbox;
    GtkWidget *filter_hbox;
    GtkWidget *search_hbox;
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
    GtkWidget *progress_vbox;
    GtkWidget *paths_hbox;
    GtkAdjustment *l_adj;
    GtkAdjustment *r_adj;
    GType column_types[NUM_COLS];
    char cwd[MAX_PATH_LENGTH];
    char *default_src;
    char *default_dst;
    char src_path_buffer[MAX_PATH_LENGTH];
    char dst_path_buffer[MAX_PATH_LENGTH];
    int32 dst_path_len;
    int32 src_path_len;

    (void)user_data;

    cecup.gtk_window = gtk_application_window_new(application);
    gtk_window_set_title(GTK_WINDOW(cecup.gtk_window), "cecup");
    gtk_window_set_default_size(GTK_WINDOW(cecup.gtk_window), 1100, 800);

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

    l_entry_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    cecup.src_entry = gtk_entry_new();
    gtk_widget_set_tooltip_text(cecup.src_entry,
                                _("Folder containing your original files"));
    gtk_editable_set_text(GTK_EDITABLE(cecup.src_entry), default_src);
    browse_src = gtk_button_new_with_label(_("Select Folder"));

    gtk_widget_set_hexpand(cecup.src_entry, TRUE);
    gtk_box_append(GTK_BOX(l_entry_hbox), cecup.src_entry);
    gtk_box_append(GTK_BOX(l_entry_hbox), browse_src);
    gtk_widget_set_hexpand(l_entry_hbox, TRUE);
    gtk_box_append(GTK_BOX(paths_hbox), l_entry_hbox);

    cecup.invert_button = gtk_button_new_with_label("<--->");
    gtk_widget_set_tooltip_text(cecup.invert_button, _("Invert Original and Backup"));
    gtk_box_append(GTK_BOX(paths_hbox), cecup.invert_button);

    r_entry_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    cecup.dst_entry = gtk_entry_new();
    gtk_widget_set_tooltip_text(cecup.dst_entry,
                                _("Folder where the backup will be stored"));
    gtk_editable_set_text(GTK_EDITABLE(cecup.dst_entry), default_dst);
    browse_dst = gtk_button_new_with_label(_("Select Folder"));

    gtk_widget_set_hexpand(cecup.dst_entry, TRUE);
    gtk_box_append(GTK_BOX(r_entry_hbox), cecup.dst_entry);
    gtk_box_append(GTK_BOX(r_entry_hbox), browse_dst);
    gtk_widget_set_hexpand(r_entry_hbox, TRUE);
    gtk_box_append(GTK_BOX(paths_hbox), r_entry_hbox);

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

    for (int32 i = 0; i < NUM_COLS; i += 1) {
        column_types[i] = G_TYPE_INT;
    }
    column_types[COL_ROW_PTR] = G_TYPE_POINTER;
    cecup.store = gtk_list_store_newv(NUM_COLS, column_types);

    v_paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    gtk_widget_set_vexpand(v_paned, TRUE);
    gtk_box_append(GTK_BOX(main_vbox), v_paned);
    paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_start_child(GTK_PANED(v_paned), paned);

    l_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    l_scroll = gtk_scrolled_window_new();
    l_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(cecup.store));
    cecup.l_tree = l_tree;
    g_object_set_data(G_OBJECT(l_tree), "side", GINT_TO_POINTER(0));
    setup_tree_columns(l_tree, COL_SRC_ACTION, COL_SRC_PATH);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(l_scroll), l_tree);
    gtk_box_append(GTK_BOX(l_vbox), l_scroll);
    gtk_widget_set_vexpand(l_scroll, TRUE);
    gtk_paned_set_start_child(GTK_PANED(paned), l_vbox);

    r_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    r_scroll = gtk_scrolled_window_new();
    r_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(cecup.store));
    cecup.r_tree = r_tree;
    g_object_set_data(G_OBJECT(r_tree), "side", GINT_TO_POINTER(1));
    setup_tree_columns(r_tree, COL_DST_ACTION, COL_DST_PATH);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(r_scroll), r_tree);
    gtk_box_append(GTK_BOX(r_vbox), r_scroll);
    gtk_widget_set_vexpand(r_scroll, TRUE);
    gtk_paned_set_end_child(GTK_PANED(paned), r_vbox);

    l_adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(l_scroll));
    r_adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(r_scroll));
    g_signal_connect(l_adj, "value-changed", G_CALLBACK(on_scroll_sync), r_adj);
    g_signal_connect(r_adj, "value-changed", G_CALLBACK(on_scroll_sync), l_adj);
    g_signal_connect(cecup.store, "sort-column-changed",
                     G_CALLBACK(on_sort_changed), NULL);

    log_scroll = gtk_scrolled_window_new();
    gtk_widget_set_size_request(log_scroll, -1, 150);
    cecup.log_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(cecup.log_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(cecup.log_view),
                                GTK_WRAP_WORD_CHAR);
    cecup.log_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(cecup.log_view));
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(log_scroll), cecup.log_view);
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

        action_copy_all = g_simple_action_new("copy_all", NULL);
        g_signal_connect(action_copy_all, "activate", G_CALLBACK(on_log_copy), "all");
        g_action_map_add_action(G_ACTION_MAP(application), G_ACTION(action_copy_all));

        action_copy_line = g_simple_action_new("copy_line", G_VARIANT_TYPE_INT32);
        g_signal_connect(action_copy_line, "activate", G_CALLBACK(on_log_copy), "line");
        g_action_map_add_action(G_ACTION_MAP(application), G_ACTION(action_copy_line));
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

    {
        GKeyFile *key = g_key_file_new();
        char *value;

        if (g_key_file_load_from_file(key, cecup.config_path, G_KEY_FILE_NONE,
                                      NULL)) {
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
        }
        g_free(key);
    }

    g_signal_connect(browse_src, "clicked",
                     G_CALLBACK(on_browse_src), NULL);
    g_signal_connect(browse_dst, "clicked",
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

    gtk_window_present(GTK_WINDOW(cecup.gtk_window));
    return;
}

int32
main(int32 argc, char **argv) {
    int32 status;

    program = argv[0];
    (void)program_len;

    begin_profile();

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
    g_cond_init(&cecup.ui_ready_cond);
    cecup.ui_waiting = false;

    cecup.rows_len = 0;
    cecup.rows_capacity = 4096;
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

    cecup.application = gtk_application_new("com.cecup.app", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(cecup.application, "activate", G_CALLBACK(activate), NULL);
    status = g_application_run(G_APPLICATION(cecup.application), argc, argv);
    g_object_unref(cecup.application);

    end_and_print_profile();

    XFREE(cecup.rows);
    XFREE(cecup.rows_visible);

    return status;
}

profiler_end_of_compilation_unit;
