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

#if !defined(ON_LOG_C)
#define ON_LOG_C

#include <gtk/gtk.h>
#include <string.h>
#include "util.c"
#include "cecup.h"
#include "on.h"

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_on_log 1
#elif !defined(TESTING_on_log)
#define TESTING_on_log 0
#endif
#if !defined(TESTING)
#define TESTING 0
#endif

static void
on_log_copy(GSimpleAction *action, GVariant *parameter, void *data) {
    char *which = data;
    char *text;
    int32 line_num;

    GtkTextIter text_start;
    GtkTextIter text_end;
    GdkClipboard *clipboard;

    (void)action;

    if (strcmp(which, "all") == 0) {
        gtk_text_buffer_get_bounds(cecup.log_buffer, &text_start, &text_end);
    } else if (strcmp(which, "line") == 0) {
        if (parameter == NULL) {
            error("Error in %s: GVariant *parameter is NULL.\n", __func__);
            return;
        }
        line_num = g_variant_get_int32(parameter);
        gtk_text_buffer_get_iter_at_line(cecup.log_buffer, &text_start, line_num);
        text_end = text_start;

        if (!gtk_text_iter_ends_line(&text_end)) {
            gtk_text_iter_forward_to_line_end(&text_end);
        }
    } else {
        error("%s called with wrong argument (which = %s)\n", __func__, which);
        fatal(EXIT_FAILURE);
    }

    clipboard = gdk_display_get_clipboard(gdk_display_get_default());
    if ((text = gtk_text_buffer_get_text(cecup.log_buffer, &text_start, &text_end, FALSE))) {
        gdk_clipboard_set_text(clipboard, text);
        g_free(text);
    }

    return;
}

static void
on_log_button_press(GtkGestureClick *gesture, int32 npress, double x, double y, void *data) {
    GtkWidget *widget;
    GtkWidget *popover;
    GtkTextIter iter;
    int32 buffer_x;
    int32 buffer_y;
    int32 line_num;
    uint32 button;

    (void)data;
    button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));

    if (npress != 1) {
        return;
    }

    if (button != GDK_BUTTON_SECONDARY) {
        return;
    }

    widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);

    gtk_text_view_window_to_buffer_coords(GTK_TEXT_VIEW(widget), GTK_TEXT_WINDOW_WIDGET,
                                          (int32)x, (int32)y,
                                          &buffer_x, &buffer_y);
    gtk_text_view_get_iter_at_location(GTK_TEXT_VIEW(widget), &iter, buffer_x, buffer_y);

    line_num = gtk_text_iter_get_line(&iter);

    {
        GMenu *menu = g_menu_new();
        GMenuItem *item = g_menu_item_new(_("📝 Copy Line"), NULL);
        g_menu_append(menu, _("📋 Copy Whole Log"), "app.copy_all");

        g_menu_item_set_action_and_target(item, "app.copy_line", "i", line_num);
        g_menu_append_item(menu, item);
        g_object_unref(item);

        popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(menu));
        g_object_unref(menu);
    }

    gtk_widget_set_parent(popover, widget);

    {
        GdkRectangle rect;
        rect.x = (int32)x;
        rect.y = (int32)y;
        rect.width = 1;
        rect.height = 1;

        gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);
    }

    gtk_popover_set_has_arrow(GTK_POPOVER(popover), FALSE);
    g_signal_connect(popover, "closed", G_CALLBACK(on_popover_closed), NULL);

    gtk_popover_popup(GTK_POPOVER(popover));

    return;
}

#if (0 == TESTING_on_log) && TESTING
static inline void
on_log_functions_sink(void) {
    (void)on_log_functions_sink;
}
#endif

#if TESTING_on_log
#include "on.c"

int
main(void) {
    return 0;
}

#endif

#endif /* ON_LOG_C */
