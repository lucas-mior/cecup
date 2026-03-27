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

#include <gtk/gtk.h>

#include "cecup.h"
#include "on.c"

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_columns 1
#elif !defined(TESTING_columns)
#define TESTING_columns 0
#endif

static void
setup_column_checkbox(GtkSignalListItemFactory *factory,
                      GtkListItem *list_item, void *data) {
    GtkWidget *check = gtk_check_button_new();

    (void)factory;
    (void)data;

    gtk_widget_set_halign(check, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(check, GTK_ALIGN_CENTER);
    g_signal_connect(check, "toggled", G_CALLBACK(on_cell_toggled), data);
    gtk_list_item_set_child(list_item, check);

    return;
}

static void
bind_column_checkbox(GtkSignalListItemFactory *factory,
                     GtkListItem *list_item, void *data) {
    GtkWidget *check;
    CecupItemProxy *proxy;
    CecupItem *item;
    uint32 position;

    (void)factory;
    (void)data;

    check = gtk_list_item_get_child(list_item);
    proxy = CECUP_ITEM_PROXY(gtk_list_item_get_item(list_item));
    item = cecup_item_proxy_get_item(proxy);
    position = gtk_list_item_get_position(list_item);

    g_signal_handlers_block_by_func(check, on_cell_toggled, NULL);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(check), item->selected);
    g_signal_handlers_unblock_by_func(check, on_cell_toggled, NULL);

    g_object_set_data(G_OBJECT(check), "cecup-item", item);
    g_object_set_data(G_OBJECT(check), "cecup-pos", GUINT_TO_POINTER(position + 1));
    return;
}

static void
setup_column_action(GtkSignalListItemFactory *factory,
                    GtkListItem *list_item, void *data) {
    GtkWidget *label = gtk_label_new(NULL);

    (void)factory;
    (void)data;

    gtk_widget_set_halign(label, GTK_ALIGN_FILL);
    gtk_widget_set_valign(label, GTK_ALIGN_FILL);
    gtk_label_set_xalign(GTK_LABEL(label), 0.5);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_list_item_set_child(list_item, label);

    return;
}

static void
bind_column_action(GtkSignalListItemFactory *factory,
                   GtkListItem *list_item, void *data) {
    GtkWidget *label;
    CecupItemProxy *proxy;
    CecupItem *item;
    int32 side;
    enum CecupAction src_act;
    enum CecupAction dst_act;
    enum CecupReason reason;
    enum CecupAction action;
    char class_name[32];
    char *classes[2];
    uint32 position;

    (void)factory;

    label = gtk_list_item_get_child(list_item);
    proxy = CECUP_ITEM_PROXY(gtk_list_item_get_item(list_item));
    item = cecup_item_proxy_get_item(proxy);
    side = GPOINTER_TO_INT(data);
    position = gtk_list_item_get_position(list_item);

    aux_item_status_get(item, &src_act, &dst_act, &reason);

    if (side == L) {
        action = src_act;
    } else {
        action = dst_act;
    }

    gtk_label_set_text(GTK_LABEL(label), action_emojis[action]);

    SNPRINTF(class_name, "cell-color-%u", action);
    classes[0] = class_name;
    classes[1] = NULL;
    gtk_widget_set_css_classes(label, (const char **)classes);

    g_object_set_data(G_OBJECT(label), "cecup-item", item);
    g_object_set_data(G_OBJECT(label), "cecup-pos", GUINT_TO_POINTER(position + 1));
    g_object_set_data(G_OBJECT(label), "cecup-col", GINT_TO_POINTER(COLUMN_ACTION));

    return;
}

static void
setup_column_path(GtkSignalListItemFactory *factory,
                  GtkListItem *list_item, void *data) {
    GtkWidget *editable = gtk_editable_label_new("");
    GtkWidget *tree;
    GtkGesture *click;

    (void)factory;
    tree = data;

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
bind_column_path(GtkSignalListItemFactory *factory,
                 GtkListItem *list_item, void *data) {
    GtkWidget *editable;
    CecupItemProxy *proxy;
    CecupItem *item;
    GtkWidget *tree;
    int32 side;
    char *path;
    enum CecupAction src_act;
    enum CecupAction dst_act;
    enum CecupReason reason;
    enum CecupAction action;
    char class_name[32];
    char *classes[2];
    uint32 position;

    (void)factory;
    tree = data;

    editable = gtk_list_item_get_child(list_item);
    proxy = CECUP_ITEM_PROXY(gtk_list_item_get_item(list_item));
    item = cecup_item_proxy_get_item(proxy);
    side = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(tree), "side"));
    position = gtk_list_item_get_position(list_item);

    path = item_path_side(item, side);

    if (path) {
        gtk_editable_set_text(GTK_EDITABLE(editable), path);
    } else {
        gtk_editable_set_text(GTK_EDITABLE(editable), "");
    }

    aux_item_status_get(item, &src_act, &dst_act, &reason);

    if (side == L) {
        action = src_act;
    } else {
        action = dst_act;
    }

    SNPRINTF(class_name, "cell-color-%u", action);
    classes[0] = class_name;
    classes[1] = NULL;
    gtk_widget_set_css_classes(editable, (const char **)classes);

    g_object_set_data(G_OBJECT(editable), "cecup-item", item);
    g_object_set_data(G_OBJECT(editable), "cecup-col", GINT_TO_POINTER(COLUMN_PATH));
    g_object_set_data(G_OBJECT(editable), "cecup-pos",
                      GUINT_TO_POINTER(position + 1));

    return;
}

static void
bind_text_cb(GtkSignalListItemFactory *factory,
             GtkListItem *list_item, void *data) {
    GtkWidget *label;
    CecupItemProxy *proxy;
    CecupItem *item;
    enum CecupAction src_act;
    enum CecupAction dst_act;
    enum CecupReason reason;
    enum CecupAction action;
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
    item = cecup_item_proxy_get_item(proxy);
    position = gtk_list_item_get_position(list_item);

    aux_item_status_get(item, &src_act, &dst_act, &reason);

    size = item_size_side(item, text_info->side);
    mtime = item_mtime_side(item, text_info->side);

    if (text_info->type == COLUMN_MTIME) {
        if (mtime > 0) {
            struct tm time_information;
            time_t unix_timestamp;

            unix_timestamp = (time_t)mtime + timezone_offset;
            gmtime_r(&unix_timestamp, &time_information);
            STRFTIME(text_buf, "%Y-%m-%d %H:%M:%S", &time_information);
        }
    } else if (text_info->type == COLUMN_SIZE) {
        if (size >= 0) {
            bytes_pretty(text_buf, size);
        }
    }

    gtk_label_set_text(GTK_LABEL(label), text_buf);

    if (text_info->side == L) {
        action = src_act;
    } else {
        action = dst_act;
    }

    SNPRINTF(class_name, "cell-color-%u", action);
    classes[0] = class_name;
    classes[1] = NULL;
    gtk_widget_set_css_classes(label, (const char **)classes);

    g_object_set_data(G_OBJECT(label), "cecup-item", item);
    g_object_set_data(G_OBJECT(label), "cecup-pos", GUINT_TO_POINTER(position + 1));
    g_object_set_data(G_OBJECT(label), "cecup-col", GINT_TO_POINTER(text_info->type));

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

#if TESTING_columns
static inline void
columns_functions_sink(void) {
    (void)setup_text_cb;
    (void)bind_text_cb;
    (void)bind_column_path;
    (void)setup_column_path;
    (void)setup_column_action;
    (void)bind_column_action;
    (void)setup_column_checkbox;
    (void)bind_column_checkbox;
    return;
}

int
main(void) {
    exit(EXIT_SUCCESS);
}

#endif

#endif /* COLUMNS_C */
