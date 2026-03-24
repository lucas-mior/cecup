#include <gtk/gtk.h>

#include "cecup.h"
#include "on.c"

static void
setup_selected_cb(GtkSignalListItemFactory *factory,
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
setup_action_cb(GtkSignalListItemFactory *factory,
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

