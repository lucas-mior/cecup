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

#if !defined(ON_TREE_C)
#define ON_TREE_C

#include <gtk/gtk.h>
#include "util.c"
#include "item.c"
#include "aux.c"
#include "cecup.h"
#include "on.h"

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_on_tree 1
#elif !defined(TESTING_on_tree)
#define TESTING_on_tree 0
#endif
#if !defined(TESTING)
#define TESTING 0
#endif

static void
on_tree_button_press(GtkGestureClick *gesture, int32 npress, double x, double y, void *data) {
    GtkWidget *widget;
    GtkWidget *parent;
    double translated_x;
    double translated_y;
    int8 side;
    int32 button;

    (void)data;
    widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));

    gtk_widget_grab_focus(widget);

    side = (int8)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "side"));
    button = (int32)gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));

    if (button == GDK_BUTTON_SECONDARY && npress == 1) {
        GtkWidget *child;
        void *position_pointer = NULL;

        if ((child = gtk_widget_pick(widget, x, y, GTK_PICK_DEFAULT))) {
            while (child
                   && (position_pointer = g_object_get_data(G_OBJECT(child), "cecup-pos"))
                       == NULL) {
                child = gtk_widget_get_parent(child);
            }

            if (child) {
                uint32 position;
                GtkSelectionModel *model;

                position = GPOINTER_TO_UINT(position_pointer) - 1;
                model = gtk_column_view_get_model(GTK_COLUMN_VIEW(widget));
                gtk_selection_model_select_item(model, position, TRUE);
            }
        }
    }

    switch (button) {
    case GDK_BUTTON_PRIMARY:
        break;
    case GDK_BUTTON_SECONDARY: {
        GMenu *menu;
        GtkWidget *popover;
        int32 row_id;
        char *filepath = NULL;
        char *other_path = NULL;
        GdkRectangle rect;
        bool is_busy;
        Message *message;
        GtkSelectionModel *selection;
        uint32 pos;
        enum Action action_src;
        enum Action action_dst;
        enum Reason reason;

        if (npress != 1) {
            break;
        }

        selection = gtk_column_view_get_model(GTK_COLUMN_VIEW(widget));
        pos = gtk_single_selection_get_selected(GTK_SINGLE_SELECTION(selection));

        if (pos == GTK_INVALID_LIST_POSITION) {
            break;
        }

        gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
        is_busy = gtk_widget_get_sensitive(cecup.stop_button);
        row_id = cecup.rows_visible[pos];

        filepath = item_path_side(row_id, side);
        other_path = item_path_side(row_id, !side);

        message = xmalloc(SIZEOF(*message));
        memset64(message, 0, SIZEOF(*message));
        if (filepath) {
            message->src_path_len = item_path_len_side(row_id, side);
            message->src_path = xmalloc(message->src_path_len + 1);
            memcpy64(message->src_path, filepath, message->src_path_len + 1);
        }
        message->side = side;

        item_get_actions_reasons(row_id, &action_src, &action_dst, &reason);

        if (side == L) {
            message->action = action_src;
        } else {
            message->action = action_dst;
        }

        g_object_set_data(G_OBJECT(cecup.application), "active_tree", widget);
        g_object_set_data_full(G_OBJECT(cecup.application), "active_message", message, free_message);

        menu = g_menu_new();
        for (int32 i = 0; i < LENGTH(tree_menu_items); i += 1) {
            CecupMenuItem *menu_item = &tree_menu_items[i];

            if (menu_item->callback == NULL) {
                GMenu *submenu = g_menu_new();
                GMenuItem *m_item;
                char *name;
                int32 path_len;
                int32 length;

                if (filepath) {
                    char *extension;
                    char label[MAX_PATH_LENGTH];
                    char directory[MAX_PATH_LENGTH];
                    char pattern[MAX_PATH_LENGTH];
                    char path_copy[MAX_PATH_LENGTH] = {0};
                    bool is_dir = false;

                    path_len = item_path_len_side(row_id, side);
                    memcpy64(path_copy, filepath, path_len + 1);

                    if (path_len > 0 && path_copy[path_len - 1] == '/') {
                        is_dir = true;
                    }

                    name = basename2(path_copy, &path_len, &length);
                    extension = memrchr64(name, '.', length);
                    dirname2(directory, path_copy, &path_len);

                    if (extension && (extension != name)) {
                        SNPRINTF(label, _("by extension (*%s)"), extension);
                        SNPRINTF(pattern, "*%s", extension);
                        m_item = g_menu_item_new(label, NULL);
                        g_menu_item_set_action_and_target(m_item, "app.ignore", "s", pattern);
                        g_menu_append_item(submenu, m_item);
                        g_object_unref(m_item);
                    }

                    if (!aux_is_root(directory)) {
                        SNPRINTF(label, _("📁 Dir (/%s/)"), directory);
                        SNPRINTF(pattern, "/%s/", directory);
                        m_item = g_menu_item_new(label, NULL);
                        g_menu_item_set_action_and_target(m_item, "app.ignore", "s", pattern);
                        g_menu_append_item(submenu, m_item);
                        g_object_unref(m_item);
                    }

                    if (is_dir) {
                        SNPRINTF(label, _("This folder only (/%s)"), filepath);
                    } else {
                        SNPRINTF(label, _("This file only (/%s)"), filepath);
                    }
                    SNPRINTF(pattern, "/%s", filepath);
                    m_item = g_menu_item_new(label, NULL);
                    g_menu_item_set_action_and_target(m_item, "app.ignore", "s", pattern);
                    g_menu_append_item(submenu, m_item);
                    g_object_unref(m_item);

                    SNPRINTF(label, _("This filename on any folder (*/%s)"), name);
                    SNPRINTF(pattern, "*/%s", name);
                    m_item = g_menu_item_new(label, NULL);
                    g_menu_item_set_action_and_target(m_item, "app.ignore", "s", pattern);
                    g_menu_append_item(submenu, m_item);
                    g_object_unref(m_item);
                }

                m_item = g_menu_item_new_submenu(_(menu_item->label), G_MENU_MODEL(submenu));

                if (is_busy || (filepath == NULL)) {
                    g_menu_item_set_action_and_target(m_item, "none.none", NULL);
                }

                g_menu_append_item(menu, m_item);
                g_object_unref(m_item);
                g_object_unref(submenu);
            } else {
                GMenuItem *m_item;
                bool disabled;

                disabled = false;
                m_item = g_menu_item_new(_(menu_item->label), NULL);
                g_menu_item_set_action_and_target(m_item, "app.tree_dispatch", "i", i);

                if (is_busy) {
                    if ((menu_item->callback == on_menu_apply) ||
                        (menu_item->callback == on_menu_rename) ||
                        (menu_item->callback == on_menu_delete)) {
                        disabled = true;
                    }
                }

                if (filepath == NULL) {
                    disabled = true;
                }

                if (menu_item->callback == on_menu_diff) {
                    if (other_path == NULL) {
                        disabled = true;
                    }
                }

                if (disabled) {
                    g_menu_item_set_action_and_target(m_item, "none.none", NULL);
                }

                g_menu_append_item(menu, m_item);
                g_object_unref(m_item);
            }
        }

        parent = gtk_widget_get_parent(widget);

        if (gtk_widget_translate_coordinates(widget, parent,
                                             x, y,
                                             &translated_x, &translated_y)) {
            popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(menu));
            gtk_widget_set_parent(popover, parent);

            rect.x = (int32)translated_x;
            rect.y = (int32)translated_y;
            rect.width = 1;
            rect.height = 1;

            gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);
            gtk_popover_set_has_arrow(GTK_POPOVER(popover), FALSE);
            g_signal_connect(popover, "closed", G_CALLBACK(on_popover_closed), NULL);
            gtk_popover_popup(GTK_POPOVER(popover));
        }

        g_object_unref(menu);
        break;
    }
    default:
        break;
    }

    return;
}

static gboolean
on_tree_tooltip(GtkWidget *w, int32 x, int32 y, gboolean k, GtkTooltip *t, void *d) {
    GtkWidget *child;
    int32 row_id = 0;
    void *row_id_ptr;
    enum ColumnType column_type;
    int32 side;
    char *tip_text;
    char tip_buffer[MAX_PATH_LENGTH*2];
    char text_buf[64] = "";

    (void)k;
    (void)d;
    tip_text = NULL;
    row_id_ptr = NULL;
    column_type = COLUMN_LAST;

    if ((child = gtk_widget_pick(w, (double)x, (double)y, GTK_PICK_DEFAULT)) == NULL) {
        return FALSE;
    }

    while (child) {
        if ((row_id_ptr = g_object_get_data(G_OBJECT(child), "cecup-row-id"))) {
            void *col_data;

            row_id = GPOINTER_TO_INT(row_id_ptr) - 1;
            col_data = g_object_get_data(G_OBJECT(child), "cecup-col");
            column_type = (enum ColumnType)GPOINTER_TO_INT(col_data);
            break;
        }
        child = gtk_widget_get_parent(child);
    }

    if (child == NULL) {
        return FALSE;
    }

    side = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "side"));

    if (row_id_ptr) {
        char *filepath;
        enum Action action;
        enum Action actions[2];
        enum Reason reason;
        bool is_dir = false;
        int32 path_len;

        item_get_actions_reasons(row_id, &actions[L], &actions[R], &reason);

        action = actions[side];

        filepath = item_path_get(row_id);

        path_len = strlen32(filepath);
        if (path_len > 0 && filepath[path_len - 1] == '/') {
            is_dir = true;
        }

        switch (column_type) {
        case COLUMN_ACTION:
        {
            char *msg;
            if (is_dir) {
                msg = _(action_strings_dir[side][action]);
            } else {
                msg = _(action_strings_file[side][action]);
            }

            tip_text = msg;
            break;
        }
        case COLUMN_PATH:
        {
            int32 pos = 0;
            char reason_buf[1024];
            char *link_target;
            char *ignore_pattern;

            reason_buf[0] = '\0';
            for (uint32 i = 0; i < REASON_BIT_COUNT; i += 1) {
                char *base_msg;

                if (!(reason & (1u << i))) {
                    continue;
                }

                if ((i >= LENGTH(reason_strings_file)) || (reason_strings_file[i] == NULL)) {
                    continue;
                }

                if (pos > 0) {
                    pos += snprintf2(reason_buf + pos, SIZEOF(reason_buf) - pos, "\n");
                }

                if (is_dir) {
                    base_msg = _(reason_strings_dir[i]);
                } else {
                    base_msg = _(reason_strings_file[i]);
                }

                if (base_msg) {
                    pos += snprintf2(reason_buf + pos, SIZEOF(reason_buf) - pos, "%s", base_msg);
                }
            }

            link_target = item_link_target_side(row_id, side);
            ignore_pattern = item_ignore_pattern_side(row_id, side);

            if (link_target) {
                char *notation;

                if ((reason & REASON_HARDLINK)) {
                    notation = RSYNC_HARDLINK;
                } else {
                    notation = RSYNC_SYMLINK;
                }
                SNPRINTF(tip_buffer,
                         "%s%s%s:\n%s", filepath, notation, link_target, reason_buf);
            } else if (ignore_pattern) {
                SNPRINTF(tip_buffer,
                         "%s:\n%s (" N_("pattern") ": %s)", filepath, reason_buf, ignore_pattern);
            } else {
                SNPRINTF(tip_buffer,
                         "%s:\n%s", filepath, reason_buf);
            }
            tip_text = tip_buffer;
            break;
        }
        case COLUMN_SIZE:
        {
            int64 size_raw = item_size_side(row_id, side);

            if (size_raw < 0) {
                size_raw = 0;
            }
            SNPRINTF(tip_buffer, "%s: %lld bytes", filepath, (llong)size_raw);
            tip_text = tip_buffer;
            break;
        }
        case COLUMN_MTIME:
        {
            int64 mtime_raw = item_mtime_side(row_id, side);

            if (mtime_raw > 0) {
                struct tm time_information;
                time_t unix_timestamp;

                unix_timestamp = (time_t)mtime_raw + timezone_offset;
                gmtime_r(&unix_timestamp, &time_information);
                STRFTIME(text_buf, "%Y-%m-%d %H:%M:%S", &time_information);
            }
            SNPRINTF(tip_buffer, "%s: %s", filepath, text_buf);
            tip_text = tip_buffer;
            break;
        }
        case COLUMN_LAST:
        default:
            break;
        }
    }

    if (tip_text) {
        gtk_tooltip_set_text(t, tip_text);
        return TRUE;
    }
    return FALSE;
}

static gboolean
on_tree_key_press(GtkEventControllerKey *controller,
                  uint32 keyval, uint32 keycode, GdkModifierType state,
                  void *data) {
    GtkWidget *widget;
    GtkSelectionModel *selection;
    uint32 pos;
    gboolean handled;
    GdkModifierType modifiers;

    (void)data;
    (void)keycode;
    handled = FALSE;
    widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller));
    selection = gtk_column_view_get_model(GTK_COLUMN_VIEW(widget));

    pos = gtk_single_selection_get_selected(GTK_SINGLE_SELECTION(selection));
    if (pos == GTK_INVALID_LIST_POSITION) {
        return FALSE;
    }

    modifiers = state & gtk_accelerator_get_default_mod_mask();

    for (int32 i = 0; i < LENGTH(tree_menu_items); i += 1) {
        CecupMenuItem *menu_item = &tree_menu_items[i];
        uint32 target;
        uint32 pressed;

        if (menu_item->keyval == 0) {
            continue;
        }

        target = gdk_keyval_to_lower(menu_item->keyval);
        pressed = gdk_keyval_to_lower(keyval);

        if ((pressed == target) && (modifiers == menu_item->mask)) {
            execute_menu_item_from_key_press(widget, menu_item);
            handled = TRUE;
            break;
        }
    }

    return handled;
}

#if (0 == TESTING_on_tree) && TESTING
static inline void
on_tree_functions_sink(void) {
    (void)on_tree_functions_sink;
    return;
}
#endif

#if TESTING_on_tree
#include "on.c"

int
main(void) {
    return 0;
}

#endif

#endif /* ON_TREE */
