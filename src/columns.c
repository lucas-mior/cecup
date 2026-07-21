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

#if !defined(COLUMNS_C)
#define COLUMNS_C

#include "gtk_include.h"

#include "cecup.h"
#include "on.c"

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_columns 1
#elif !defined(TESTING_columns)
#define TESTING_columns 0
#endif
#if !defined(TESTING)
#define TESTING 0
#endif

static void
column_setup_checkbox(GtkSignalListItemFactory *factory, GtkListItem *list_item, void *data) {
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
column_bind_checkbox(GtkSignalListItemFactory *factory, GtkListItem *list_item, void *data) {
    GtkWidget *check;
    CecupItemProxy *proxy;
    int32 row_id;
    uint32 position;

    (void)factory;

    check = gtk_list_item_get_child(list_item);
    proxy = CECUP_ITEM_PROXY(gtk_list_item_get_item(list_item));
    row_id = cecup_item_proxy_get_index(proxy);
    position = gtk_list_item_get_position(list_item);

    g_signal_handlers_block_by_func(check, on_cell_toggled, data);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(check), (bool)cecup.rows_selected[row_id]);
    g_signal_handlers_unblock_by_func(check, on_cell_toggled, data);

    // TODO: Set an explicit checkbox column type. With no "cecup-col", the
    // tooltip code converts NULL to COLUMN_ACTION and shows an unrelated tip.
    g_object_set_data(G_OBJECT(check), "cecup-row-id", GINT_TO_POINTER(row_id + 1));
    g_object_set_data(G_OBJECT(check), "cecup-pos", GUINT_TO_POINTER(position + 1));
    return;
}

static void
column_setup_action(GtkSignalListItemFactory *factory, GtkListItem *list_item, void *data) {
    GtkWidget *label;

    (void)factory;
    (void)data;

    label = gtk_label_new(NULL);
    gtk_widget_set_halign(label, GTK_ALIGN_FILL);
    gtk_widget_set_valign(label, GTK_ALIGN_FILL);
    gtk_label_set_xalign(GTK_LABEL(label), 0.5);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_list_item_set_child(list_item, label);

    return;
}

static void
column_bind_action(GtkSignalListItemFactory *factory, GtkListItem *list_item, void *data) {
    GtkWidget *label;
    CecupItemProxy *proxy;
    int32 row_id;
    int32 side;
    enum Action action;
    enum Action actions[2];
    enum Reason reason;
    char class_name[32];
    char *classes[2];
    uint32 position;

    (void)factory;

    label = gtk_list_item_get_child(list_item);
    proxy = CECUP_ITEM_PROXY(gtk_list_item_get_item(list_item));
    row_id = cecup_item_proxy_get_index(proxy);
    side = GPOINTER_TO_INT(data);
    position = gtk_list_item_get_position(list_item);

    item_get_actions_reasons(row_id, &actions[L], &actions[R], &reason);
    action = actions[side];

    gtk_label_set_text(GTK_LABEL(label), action_emojis[action]);

    SNPRINTF(class_name, "cell-color-%u", action);
    classes[0] = class_name;
    classes[1] = NULL;
    gtk_widget_set_css_classes(label, (const char **)classes);

    g_object_set_data(G_OBJECT(label), "cecup-row-id", GINT_TO_POINTER(row_id + 1));
    g_object_set_data(G_OBJECT(label), "cecup-pos", GUINT_TO_POINTER(position + 1));
    g_object_set_data(G_OBJECT(label), "cecup-col", GINT_TO_POINTER(COLUMN_ACTION));

    return;
}

static void
column_setup_path(GtkSignalListItemFactory *factory, GtkListItem *list_item, void *data) {
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

    g_signal_connect(editable, "notify::editing", G_CALLBACK(on_path_editing_notify), tree);

    click = gtk_gesture_click_new();
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(click), GTK_PHASE_CAPTURE);
    g_signal_connect(click, "pressed", G_CALLBACK(on_path_click_pressed), tree);
    gtk_widget_add_controller(editable, GTK_EVENT_CONTROLLER(click));

    gtk_list_item_set_child(list_item, editable);

    return;
}

static void
column_bind_path(GtkSignalListItemFactory *factory, GtkListItem *list_item, void *data) {
    GtkWidget *editable;
    CecupItemProxy *proxy;
    int32 row_id;
    GtkWidget *tree;
    int32 side;
    char *path;
    enum Action action;
    enum Action actions[2];
    enum Reason reason;
    char class_name[32];
    char *classes[2];
    uint32 position;

    (void)factory;
    tree = data;

    editable = gtk_list_item_get_child(list_item);
    proxy = CECUP_ITEM_PROXY(gtk_list_item_get_item(list_item));
    row_id = cecup_item_proxy_get_index(proxy);
    side = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(tree), "side"));
    position = gtk_list_item_get_position(list_item);

    path = item_path_side(row_id, side);

    if (path) {
        gtk_editable_set_text(GTK_EDITABLE(editable), path);
    } else {
        gtk_editable_set_text(GTK_EDITABLE(editable), "");
    }

    item_get_actions_reasons(row_id, &actions[L], &actions[R], &reason);
    action = actions[side];

    SNPRINTF(class_name, "cell-color-%u", action);
    classes[0] = class_name;
    classes[1] = NULL;
    gtk_widget_set_css_classes(editable, (const char **)classes);

    g_object_set_data(G_OBJECT(editable), "cecup-row-id", GINT_TO_POINTER(row_id + 1));
    g_object_set_data(G_OBJECT(editable), "cecup-col", GINT_TO_POINTER(COLUMN_PATH));
    g_object_set_data(G_OBJECT(editable), "cecup-pos", GUINT_TO_POINTER(position + 1));

    return;
}

static void
column_text_setup(GtkSignalListItemFactory *factory, GtkListItem *list_item, void *data) {
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
column_text_bind(GtkSignalListItemFactory *factory, GtkListItem *list_item, void *data) {
    GtkWidget *label;
    CecupItemProxy *proxy;
    int32 row_id;
    enum Action action;
    enum Action actions[2];
    enum Reason reason;
    int64 size;
    int64 mtime;
    char text_buf[64] = "";
    char class_name[32];
    char *classes[2];
    uint32 position;
    TextInfo *text_info;

    (void)data;
    text_info = g_object_get_data(G_OBJECT(factory), "text_info");

    label = gtk_list_item_get_child(list_item);
    proxy = CECUP_ITEM_PROXY(gtk_list_item_get_item(list_item));
    row_id = cecup_item_proxy_get_index(proxy);
    position = gtk_list_item_get_position(list_item);

    item_get_actions_reasons(row_id, &actions[L], &actions[R], &reason);

    size = item_size_side(row_id, text_info->side);
    mtime = item_mtime_side(row_id, text_info->side);

    if (text_info->type == COLUMN_MTIME) {
        // TODO: Timestamp zero and valid pre-epoch values are rendered blank.
        // Distinguish a missing row from a timestamp by its index, not by its
        // sign.
        if (mtime > 0) {
            struct tm time_information;
            time_t unix_timestamp;

            unix_timestamp = (time_t)mtime + timezone_offset;
            // TODO: Use localtime_r for each timestamp and check its result.
            // A startup-time offset is wrong across DST, and gmtime_r failure
            // leaves time_information unusable for strftime.
            gmtime_r(&unix_timestamp, &time_information);
            STRFTIME(text_buf, "%Y-%m-%d %H:%M:%S", &time_information);
        }
    } else if (text_info->type == COLUMN_SIZE) {
        if (size >= 0) {
            bytes_pretty(text_buf, size);
        }
    }

    gtk_label_set_text(GTK_LABEL(label), text_buf);

    action = actions[text_info->side];

    SNPRINTF(class_name, "cell-color-%u", action);
    classes[0] = class_name;
    classes[1] = NULL;
    gtk_widget_set_css_classes(label, (const char **)classes);

    g_object_set_data(G_OBJECT(label), "cecup-row-id", GINT_TO_POINTER(row_id + 1));
    g_object_set_data(G_OBJECT(label), "cecup-pos", GUINT_TO_POINTER(position + 1));
    g_object_set_data(G_OBJECT(label), "cecup-col", GINT_TO_POINTER(text_info->type));

    return;
}

#if (0 == TESTING_columns) && TESTING
static inline void
columns_functions_sink(void) {
    (void)columns_functions_sink;
    (void)column_text_setup;
    (void)column_text_bind;
    (void)column_bind_path;
    (void)column_setup_path;
    (void)column_setup_action;
    (void)column_bind_action;
    (void)column_setup_checkbox;
    (void)column_bind_checkbox;
    return;
}
#endif

#if TESTING_columns

#include "util.c"
#include "aux.c"
#include "list_model.c"
#include "work.c"
#include "assert.c"

int
main(void) {
    GtkWidget *window;
    GtkWidget *tree;
    GtkSelectionModel *sel;
    CecupListModel *model;
    GtkSignalListItemFactory *factory_cb;
    GtkSignalListItemFactory *factory_act;
    GtkSignalListItemFactory *factory_path;
    GtkSignalListItemFactory *factory_size;
    GtkSignalListItemFactory *factory_mtime;
    GtkColumnViewColumn *col;
    TextInfo *ti_size;
    TextInfo *ti_mtime;

    disable_dbus_warning();

    if (!gtk_init_check()) {
        exit(EXIT_SUCCESS);
    }

    cecup.rows_capacity = 10;
    cecup.rows_selected = malloc2(10 * SIZEOF(uint8));
    cecup.rows_selected[0] = true;
    cecup.rows[L] = malloc2(10 * SIZEOF(int32));
    cecup.rows[R] = malloc2(10 * SIZEOF(int32));
    cecup.rows[L][0] = 0;
    cecup.rows[R][0] = 0;

    cecup.rows_visible = malloc2(10 * SIZEOF(int32));
    cecup.rows_visible[0] = 0;
    cecup.rows_visible_len = 1;

    cecup.traversal[L].stats = malloc2(10 * SIZEOF(struct stat));
    cecup.traversal[R].stats = malloc2(10 * SIZEOF(struct stat));
    memset64(cecup.traversal[L].stats, 0, 10 * SIZEOF(struct stat));
    memset64(cecup.traversal[R].stats, 0, 10 * SIZEOF(struct stat));

    cecup.traversal[L].stats[0].st_size = 2048;
    cecup.traversal[R].stats[0].st_size = 2048;
    cecup.traversal[L].stats[0].st_mtime = 1600000000;
    cecup.traversal[R].stats[0].st_mtime = 1600000000;

    cecup.traversal[L].paths = malloc2(10 * SIZEOF(char*));
    cecup.traversal[R].paths = malloc2(10 * SIZEOF(char*));
    cecup.traversal[L].paths[0] = "test_l";
    cecup.traversal[R].paths[0] = "test_r";

    cecup.traversal[L].patterns = malloc2(10 * SIZEOF(char*));
    cecup.traversal[R].patterns = malloc2(10 * SIZEOF(char*));
    memset64(cecup.traversal[L].patterns, 0, 10 * SIZEOF(char*));
    memset64(cecup.traversal[R].patterns, 0, 10 * SIZEOF(char*));

    cecup.traversal[L].symlink_targets = malloc2(10 * SIZEOF(char*));
    cecup.traversal[R].symlink_targets = malloc2(10 * SIZEOF(char*));
    memset64(cecup.traversal[L].symlink_targets, 0, 10 * SIZEOF(char*));
    memset64(cecup.traversal[R].symlink_targets, 0, 10 * SIZEOF(char*));

    cecup.traversal[L].paths_lens = malloc2(10 * SIZEOF(int16));
    cecup.traversal[R].paths_lens = malloc2(10 * SIZEOF(int16));
    memset64(cecup.traversal[L].paths_lens, 0, 10 * SIZEOF(int16));
    memset64(cecup.traversal[R].paths_lens, 0, 10 * SIZEOF(int16));

    cecup.traversal[L].row_ids = malloc2(10 * SIZEOF(int32));
    cecup.traversal[R].row_ids = malloc2(10 * SIZEOF(int32));
    cecup.traversal[L].row_ids[0] = 0;
    cecup.traversal[R].row_ids[0] = 0;

    cecup.traversal[L].file_count = 1;
    cecup.traversal[R].file_count = 1;

    model = cecup_list_model_new();
    model->reported_count = 1;

    sel = GTK_SELECTION_MODEL(gtk_single_selection_new(G_LIST_MODEL(model)));
    tree = gtk_column_view_new(sel);
    g_object_set_data(G_OBJECT(tree), "side", GINT_TO_POINTER(L));

    factory_cb = GTK_SIGNAL_LIST_ITEM_FACTORY(gtk_signal_list_item_factory_new());
    g_signal_connect(factory_cb, "setup", G_CALLBACK(column_setup_checkbox), GINT_TO_POINTER(L));
    g_signal_connect(factory_cb, "bind", G_CALLBACK(column_bind_checkbox), GINT_TO_POINTER(L));
    col = gtk_column_view_column_new(NULL, GTK_LIST_ITEM_FACTORY(factory_cb));
    gtk_column_view_append_column(GTK_COLUMN_VIEW(tree), col);
    g_object_unref(col);

    factory_act = GTK_SIGNAL_LIST_ITEM_FACTORY(gtk_signal_list_item_factory_new());
    g_signal_connect(factory_act, "setup", G_CALLBACK(column_setup_action), NULL);
    g_signal_connect(factory_act, "bind", G_CALLBACK(column_bind_action), GINT_TO_POINTER(L));
    col = gtk_column_view_column_new("Action", GTK_LIST_ITEM_FACTORY(factory_act));
    gtk_column_view_append_column(GTK_COLUMN_VIEW(tree), col);
    g_object_unref(col);

    factory_path = GTK_SIGNAL_LIST_ITEM_FACTORY(gtk_signal_list_item_factory_new());
    g_signal_connect(factory_path, "setup", G_CALLBACK(column_setup_path), tree);
    g_signal_connect(factory_path, "bind", G_CALLBACK(column_bind_path), tree);
    col = gtk_column_view_column_new("Path", GTK_LIST_ITEM_FACTORY(factory_path));
    gtk_column_view_append_column(GTK_COLUMN_VIEW(tree), col);
    g_object_unref(col);

    factory_size = GTK_SIGNAL_LIST_ITEM_FACTORY(gtk_signal_list_item_factory_new());
    ti_size = malloc2(SIZEOF(TextInfo));
    ti_size->type = COLUMN_SIZE;
    ti_size->side = L;
    g_object_set_data(G_OBJECT(factory_size), "text_info", ti_size);
    g_signal_connect(factory_size, "setup", G_CALLBACK(column_text_setup), NULL);
    g_signal_connect(factory_size, "bind", G_CALLBACK(column_text_bind), ti_size);
    col = gtk_column_view_column_new("Size", GTK_LIST_ITEM_FACTORY(factory_size));
    gtk_column_view_append_column(GTK_COLUMN_VIEW(tree), col);
    g_object_unref(col);

    factory_mtime = GTK_SIGNAL_LIST_ITEM_FACTORY(gtk_signal_list_item_factory_new());
    ti_mtime = malloc2(SIZEOF(TextInfo));
    ti_mtime->type = COLUMN_MTIME;
    ti_mtime->side = L;
    g_object_set_data(G_OBJECT(factory_mtime), "text_info", ti_mtime);
    g_signal_connect(factory_mtime, "setup", G_CALLBACK(column_text_setup), NULL);
    g_signal_connect(factory_mtime, "bind", G_CALLBACK(column_text_bind), ti_mtime);
    col = gtk_column_view_column_new("Mtime", GTK_LIST_ITEM_FACTORY(factory_mtime));
    gtk_column_view_append_column(GTK_COLUMN_VIEW(tree), col);
    g_object_unref(col);

    window = gtk_window_new();
    gtk_window_set_child(GTK_WINDOW(window), tree);
    gtk_window_present(GTK_WINDOW(window));

    for (int32 i = 0; i < 100; i += 1) {
        g_main_context_iteration(NULL, FALSE);
    }

    gtk_window_destroy(GTK_WINDOW(window));

    free2(ti_size, SIZEOF(TextInfo));
    free2(ti_mtime, SIZEOF(TextInfo));
    free2(cecup.rows_selected, 10 * SIZEOF(uint8));
    free2(cecup.rows_visible, 10 * SIZEOF(int32));
    free2(cecup.rows[L], 10 * SIZEOF(int32));
    free2(cecup.rows[R], 10 * SIZEOF(int32));
    free2(cecup.traversal[L].stats, 10 * SIZEOF(struct stat));
    free2(cecup.traversal[R].stats, 10 * SIZEOF(struct stat));
    free2(cecup.traversal[L].paths, 10 * SIZEOF(char*));
    free2(cecup.traversal[R].paths, 10 * SIZEOF(char*));
    free2(cecup.traversal[L].patterns, 10 * SIZEOF(char*));
    free2(cecup.traversal[R].patterns, 10 * SIZEOF(char*));
    free2(cecup.traversal[L].symlink_targets, 10 * SIZEOF(char*));
    free2(cecup.traversal[R].symlink_targets, 10 * SIZEOF(char*));
    free2(cecup.traversal[L].paths_lens, 10 * SIZEOF(int16));
    free2(cecup.traversal[R].paths_lens, 10 * SIZEOF(int16));
    free2(cecup.traversal[L].row_ids, 10 * SIZEOF(int32));
    free2(cecup.traversal[R].row_ids, 10 * SIZEOF(int32));

    ASSERT(true);
    exit(EXIT_SUCCESS);
}

#endif

#endif /* COLUMNS_C */
