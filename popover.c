#include <gtk/gtk.h>
#include "util.c"

static void
print_hello(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    g_print("Hello!\n");
    return;
}

static void
show_menu(GtkGestureClick *gesture, int32 n_press, double x, double y, gpointer user_data)
{
    GMenuModel *menu_model;
    GtkWidget *popover;
    GtkWidget *widget;
    GtkWidget *parent;
    GdkRectangle rect;
    double translated_x;
    double translated_y;

    menu_model = G_MENU_MODEL(user_data);
    widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    
    parent = gtk_widget_get_parent(widget);
    
    if (gtk_widget_translate_coordinates(widget, parent, x, y, &translated_x, &translated_y) == FALSE) {
        return;
    }

    popover = gtk_popover_menu_new_from_model(menu_model);
    gtk_widget_set_parent(popover, parent);
    
    rect.x = (int32)translated_x;
    rect.y = (int32)translated_y;
    rect.width = 1;
    rect.height = 1;
    
    gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);
    gtk_popover_set_has_arrow(GTK_POPOVER(popover), FALSE);
    gtk_popover_popup(GTK_POPOVER(popover));
    return;
}

static void
activate(GtkApplication *application, gpointer user_data)
{
    GtkWidget *window;
    GtkWidget *main_box;
    GtkWidget *tree_view;
    GtkWidget *log_view;
    GtkWidget *scrolled_window_tree;
    GtkWidget *scrolled_window_log;
    GtkListStore *store;
    GtkTreeIter iter;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    GMenu *menu;
    GSimpleAction *hello_action;
    GtkGesture *tree_gesture;
    GtkGesture *log_gesture;

    window = gtk_application_window_new(application);
    gtk_window_set_title(GTK_WINDOW(window), "Popover Menu Example");
    gtk_window_set_default_size(GTK_WINDOW(window), 600, 400);

    main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_window_set_child(GTK_WINDOW(window), main_box);

    store = gtk_list_store_new(1, G_TYPE_STRING);
    gtk_list_store_append(store, &iter);
    gtk_list_store_set(store, &iter, 0, "Right click this Tree Item", -1);
    
    tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Items", renderer, "text", 0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), column);
    g_object_unref(store);

    scrolled_window_tree = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scrolled_window_tree, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled_window_tree), tree_view);
    gtk_box_append(GTK_BOX(main_box), scrolled_window_tree);

    log_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(log_view), FALSE);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(log_view)), "Right click here in the log view...", -1);
    
    scrolled_window_log = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scrolled_window_log, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled_window_log), log_view);
    gtk_box_append(GTK_BOX(main_box), scrolled_window_log);

    menu = g_menu_new();
    g_menu_append(menu, "Say Hello", "app.hello");

    tree_gesture = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(tree_gesture), 0);
    g_signal_connect(tree_gesture, "pressed", G_CALLBACK(show_menu), menu);
    gtk_widget_add_controller(tree_view, GTK_EVENT_CONTROLLER(tree_gesture));

    log_gesture = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(log_gesture), 0);
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(log_gesture), GTK_PHASE_CAPTURE);
    g_signal_connect(log_gesture, "pressed", G_CALLBACK(show_menu), menu);
    gtk_widget_add_controller(log_view, GTK_EVENT_CONTROLLER(log_gesture));

    hello_action = g_simple_action_new("hello", NULL);
    g_signal_connect(hello_action, "activate", G_CALLBACK(print_hello), NULL);
    g_action_map_add_action(G_ACTION_MAP(application), G_ACTION(hello_action));

    gtk_window_present(GTK_WINDOW(window));
    return;
}

int32
main(int32 argc, char **argv)
{
    GtkApplication *application;
    int32 status;

    application = gtk_application_new("com.example.popover", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(application, "activate", G_CALLBACK(activate), NULL);
    status = g_application_run(G_APPLICATION(application), argc, argv);
    g_object_unref(application);

    return status;
}
