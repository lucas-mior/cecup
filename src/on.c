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

#if !defined(ON_C)
#define ON_C

#include <gtk/gtk.h>

#include "cecup.h"
#include "update.c"
#include "on_menu.c"
#include "on_log.c"
#include "on_tree.c"
#include "on_path.c"
#include "work.c"

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_on 1
#elif !defined(TESTING_on)
#define TESTING_on 0
#endif

static gboolean
unparent_popover_idle(void *data) {
    GtkWidget *widget;

    widget = data;
    gtk_widget_unparent(widget);
    return G_SOURCE_REMOVE;
}

static void
on_popover_closed(GtkWidget *popover, void *data) {
    (void)data;
    g_idle_add(unparent_popover_idle, popover);
    return;
}

static void
execute_menu_item(GtkWidget *tree, CecupMenuItem *menu_item) {
    GtkSelectionModel *selection;
    GtkSingleSelection *single_sel;
    uint32 pos;
    int32 row_id;
    char *filepath;
    int32 side;
    int32 path_len;
    enum Action action_src;
    enum Action action_dst;
    enum Reason reason;

    if (menu_item == NULL) {
        return;
    }

    selection = gtk_column_view_get_model(GTK_COLUMN_VIEW(tree));
    side = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(tree), "side"));

    single_sel = GTK_SINGLE_SELECTION(selection);

    if ((gtk_widget_get_sensitive(cecup.stop_button))) {
        if ((menu_item->callback == on_menu_rename)
            || (menu_item->callback == on_menu_delete)
            || (menu_item->callback == on_menu_apply)) {
            LOG_ERROR(_("Action blocked: Background task is running.\n"));
            return;
        }
    }

    if ((pos = gtk_single_selection_get_selected(single_sel))
         == GTK_INVALID_LIST_POSITION) {
        return;
    }

    row_id = cecup.rows_visible[pos];
    filepath = item_path_side(row_id, side);
    path_len = item_path_len_side(row_id, side);

    if (filepath || (menu_item->callback == on_menu_rename)) {
        Message *message;

        message = xmalloc(SIZEOF(*message));
        memset64(message, 0, SIZEOF(*message));

        if (filepath) {
            message->path_len = path_len;
            message->src_path = xmalloc(path_len + 1);
            memcpy64(message->src_path, filepath, path_len + 1);
        }

        item_get_actions_reasons(row_id, &action_src, &action_dst, &reason);

        if (side == L) {
            message->action = action_src;
        } else {
            message->action = action_dst;
        }
        message->side = side;

        if (menu_item->variant) {
            g_object_set_data_full(G_OBJECT(tree), "variant", menu_item->variant, NULL);
        }

        menu_item->callback(tree, message);
    }

    return;
}

static void
on_config_changed(GtkWidget *widget, void *data) {
    (void)widget;
    (void)data;
    invalidate_preview();
    cecup_get_dirs();
    save_config();
    return;
}

static gboolean
on_search_timeout(void *data) {
    (void)data;
    update_list_from_rows();
    gtk_entry_set_icon_from_icon_name(GTK_ENTRY(cecup.search_entry), GTK_ENTRY_ICON_SECONDARY, NULL);
    cecup.search_timeout_id = 0;
    return G_SOURCE_REMOVE;
}

static void
on_search_changed(GtkEditable *editable, void *data) {
    char *text;
    int32 len;
    (void)data;

    text = (char *)gtk_editable_get_text(editable);
    len = strlen32(text);

    if (cecup.search_query) {
        free(cecup.search_query, cecup.search_query_len + 1);
    }

    cecup.search_query = xmemdup(text, len + 1);
    cecup.search_query_len = len;

    if (cecup.search_timeout_id != 0) {
        g_source_remove(cecup.search_timeout_id);
    }

    gtk_entry_set_icon_from_icon_name(GTK_ENTRY(cecup.search_entry),
                                      GTK_ENTRY_ICON_SECONDARY, "view-refresh-symbolic");

    cecup.search_timeout_id = g_timeout_add(250, on_search_timeout, NULL);
    return;
}

static void
on_preview_setting_toggled(GtkCheckButton *b, void *data) {
    (void)b;
    (void)data;
    save_config();
    invalidate_preview();
    return;
}

static void
on_delete_after_toggled(GtkCheckButton *b, void *data) {
    bool active;

    (void)data;

    if ((active = gtk_check_button_get_active(b))) {
        GtkWidget *dialog;

        dialog = gtk_message_dialog_new(GTK_WINDOW(cecup.gtk_window),
                                        GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK,
                                        _("Warning: 'Sync 100%%' (delete-after) is enabled."
                                          " Files in the backup folder"
                                          " that do not exist in the source folder"
                                          " will be PERMANENTLY DELETED."
                                          " Also, files that are newer on the destination"
                                          " will be OVERWRITTEN."));

        g_signal_connect(dialog, "response", G_CALLBACK(gtk_window_destroy), NULL);
        gtk_widget_show(dialog);
        invalidate_preview();
    }

    cecup.delete_after = active;
    save_config();
    return;
}

static void
on_delete_ignored_toggled(GtkCheckButton *b, void *data) {
    bool active;

    (void)data;

    if ((active = gtk_check_button_get_active(b))) {
        g_signal_handlers_block_by_func(cecup.delete_after_button, on_delete_after_toggled, NULL);
        gtk_check_button_set_active(GTK_CHECK_BUTTON(cecup.delete_after), TRUE);
        g_signal_handlers_unblock_by_func(cecup.delete_after_button, on_delete_after_toggled, NULL);
        invalidate_preview();
    }

    cecup.delete_ignored = active;
    save_config();
    return;
}

static void
on_reset_clicked(GtkWidget *b, void *data) {
    (void)b;
    (void)data;

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup.filter_new), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup.filter_hard), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup.filter_update), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup.filter_equal), FALSE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup.filter_delete), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup.filter_ignore), TRUE);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(cecup.check_fs), FALSE);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(cecup.delete_ignored_button), FALSE);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(cecup.delete_after_button), FALSE);

    gtk_editable_set_text(GTK_EDITABLE(cecup.diff_entry), "unidiff.bash");
    gtk_editable_set_text(GTK_EDITABLE(cecup.term_entry), "xterm");
    save_config();
    invalidate_preview();
    return;
}

static void
on_preview_clicked(GtkWidget *b, void *data) {
    ThreadData *thread_data;
    Message *message;
    GThread *thread;

    (void)data;
    (void)b;

    cecup_get_dirs();

    protect_interface_from_user(true);

    message = xmalloc(SIZEOF(*message));
    memset64(message, 0, SIZEOF(*message));
    message->type = MSG_CLEAR_TREES;
    update_ui_handler(message);

    thread_data = xmalloc(SIZEOF(*thread_data));
    memset64(thread_data, 0, SIZEOF(*thread_data));

    thread = g_thread_new("work_preview", work_preview, thread_data);
    g_thread_unref(thread);
    return;
}

static int
transfers_compare(const void *transfer1, const void *transfer2) {
    const char *t1 = transfer1;
    const char *t2 = transfer2;
    return strcmp(t1, t2);
}

static void
on_sync_response(GtkDialog *dialog, int32 response_id, void *data) {
    ThreadData *thread_data;
    GThread *thread;

    (void)data;
    gtk_window_destroy(GTK_WINDOW(dialog));

    if (response_id != GTK_RESPONSE_YES) {
        return;
    }

    protect_interface_from_user(true);

    thread_data = xmalloc(SIZEOF(*thread_data));
    memset64(thread_data, 0, SIZEOF(*thread_data));

    thread = g_thread_new("work_rsync", work_rsync, thread_data);
    g_thread_unref(thread);
    return;
}

static void
on_sync_clicked(GtkWidget *b, void *data) {
    char *path_src;
    char *path_dst;
    GtkWidget *dialog;

    (void)data;
    (void)b;
    path_src = (char *)gtk_editable_get_text(GTK_EDITABLE(cecup.dir_entry[L]));
    path_dst = (char *)gtk_editable_get_text(GTK_EDITABLE(cecup.dir_entry[R]));

    dialog = gtk_message_dialog_new(GTK_WINDOW(cecup.gtk_window),
                                    GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
                                    _("Sync %s -> %s?"), path_src, path_dst);
    g_signal_connect(dialog, "response", G_CALLBACK(on_sync_response), NULL);
    gtk_widget_show(dialog);
    return;
}

static void
on_stop_clicked(GtkWidget *b, void *data) {
    int32 pid_to_kill;

    (void)b;
    (void)data;

    pid_to_kill = cecup.child_pid;
    if (pid_to_kill > 0) {
        xkill(-pid_to_kill, SIGTERM);
    }
    cecup.stop_working = true;
    return;
}

static void
on_filter_toggled(GtkToggleButton *b, void *data) {
    (void)data;
    (void)b;
    update_list_from_rows();
    save_config();
    return;
}

static void
on_sort_changed(GtkSorter *sorter, GtkSorterChange change, void *data) {
    GtkColumnView *view;
    GtkSorter *view_sorter;
    GtkColumnViewColumn *col;
    GtkSortType order;

    (void)sorter;
    (void)change;

    view = GTK_COLUMN_VIEW(data);
    view_sorter = gtk_column_view_get_sorter(view);

    if (GTK_IS_COLUMN_VIEW_SORTER(view_sorter)) {
        col = gtk_column_view_sorter_get_primary_sort_column(GTK_COLUMN_VIEW_SORTER(view_sorter));
        order = gtk_column_view_sorter_get_primary_sort_order(GTK_COLUMN_VIEW_SORTER(view_sorter));

        if (col) {
            void *col_data;

            col_data = g_object_get_data(G_OBJECT(col), "col_id");
            cecup.sort_col = (enum CecupColumn)GPOINTER_TO_INT(col_data);
            cecup.sort_order = order;
            update_list_from_rows();
        }
    }

    return;
}

static void on_cell_toggled(GtkCheckButton *renderer, void *user_data);

static void
update_visible_checkboxes(GtkWidget *widget, int32 side) {
    GtkWidget *child;
    void *row_id_ptr;

    if (widget == NULL) {
        return;
    }

    if (GTK_IS_CHECK_BUTTON(widget)) {
        if ((row_id_ptr = g_object_get_data(G_OBJECT(widget), "cecup-row-id"))) {
            int32 row_id;

            row_id = GPOINTER_TO_INT(row_id_ptr);
            g_signal_handlers_block_by_func(widget, on_cell_toggled, GINT_TO_POINTER(side));
            gtk_check_button_set_active(GTK_CHECK_BUTTON(widget), (bool)cecup.rows_selected[row_id]);
            g_signal_handlers_unblock_by_func(widget, on_cell_toggled, GINT_TO_POINTER(side));
        }
    }

    child = gtk_widget_get_first_child(widget);
    while (child != NULL) {
        update_visible_checkboxes(child, side);
        child = gtk_widget_get_next_sibling(child);
    }
    return;
}

static void
on_cell_toggled(GtkCheckButton *renderer, void *user_data) {
    int32 row_id_toggled;
    void *row_id_ptr;
    char *parent_path;
    int32 parent_path_len;
    bool is_root;
    bool is_active;
    int32 side;
    static bool in_update = false;

    if (in_update) {
        return;
    }

    side = GPOINTER_TO_INT(user_data);

    if ((row_id_ptr = g_object_get_data(G_OBJECT(renderer), "cecup-row-id")) == NULL) {
        return;
    }

    row_id_toggled = GPOINTER_TO_INT(row_id_ptr);
    is_active = gtk_check_button_get_active(renderer);
    if ((bool)cecup.rows_selected[row_id_toggled] == is_active) {
        return;
    }

    in_update = true;
    cecup.rows_selected[row_id_toggled] = (uint8)is_active;

    parent_path = item_path_side(row_id_toggled, side);
    if (parent_path == NULL) {
        parent_path = item_path_side(row_id_toggled, (side == L) ? R : L);
    }

    if (parent_path) {
        int32 count_selected = 0;
        int64 total_size_bytes = 0;
        bool filter_new = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_new));
        bool filter_hard = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_hard));
        bool filter_update = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_update));

        parent_path_len = strlen32(parent_path);
        is_root = aux_is_root(parent_path);

        for (int32 i = 0; i < cecup.rows_len; i += 1) {
            int32 row_id;
            char *path;

            row_id = i;
            path = item_path_side(row_id, side);
            if (path == NULL) {
                path = item_path_side(row_id, (side == L) ? R : L);
            }

            if (path != NULL) {
                int32 path_len;

                path_len = strlen32(path);
                if (cecup.rows_selected[row_id_toggled]) {
                    if (is_root) {
                        cecup.rows_selected[row_id] = 1;
                    } else if (parent_path_len > 0 && parent_path[parent_path_len - 1] == '/') {
                        if (path_len >= parent_path_len && !strncmp32(path, parent_path, parent_path_len)) {
                            cecup.rows_selected[row_id] = 1;
                        }
                    }
                } else {
                    if (is_root) {
                        cecup.rows_selected[row_id] = 0;
                    } else {
                        if (parent_path_len > 0 && parent_path[parent_path_len - 1] == '/') {
                            if (path_len >= parent_path_len && !strncmp32(path, parent_path, parent_path_len)) {
                                cecup.rows_selected[row_id] = 0;
                            }
                        }
                        if (aux_is_root(path)) {
                            cecup.rows_selected[row_id] = 0;
                        } else if (path_len < parent_path_len && path[path_len - 1] == '/') {
                            if (strncmp32(parent_path, path, path_len) == 0) {
                                cecup.rows_selected[row_id] = 0;
                            }
                        }
                    }
                }
            }

            if (cecup.rows_selected[row_id]) {
                enum Action action_src;
                enum Action action_dst;
                enum Reason reason;
                int64 size;

                count_selected += 1;
                item_get_actions_reasons(row_id, &action_src, &action_dst, &reason);

                size = item_size_side(row_id, L);
                if (size < 0) {
                    size = 0;
                }

                if ((action_src == ACTION_NEW && filter_new) ||
                    ((action_src == ACTION_HARDLINK || action_src == ACTION_SYMLINK) && filter_hard) ||
                    (action_src == ACTION_UPDATE && filter_update)) {
                    total_size_bytes += size;
                }
            }
        }

        update_stats_text(count_selected, total_size_bytes);
    }

    g_list_model_items_changed(cecup.store, 0,
                               (uint32)cecup.rows_visible_len,
                               (uint32)cecup.rows_visible_len);
    update_visible_checkboxes(cecup.tree[side], side);

    in_update = false;
    return;
}

static void
on_unselect_all_clicked(GtkWidget *b, void *data) {
    (void)b;
    (void)data;

    for (int32 i = 0; i < cecup.rows_len; i += 1) {
        cecup.rows_selected[i] = 0;
    }

    update_list_from_rows();
    update_visible_checkboxes(cecup.tree[L], L);
    update_visible_checkboxes(cecup.tree[R], R);
    return;
}

static void
on_select_all_visible_clicked(GtkWidget *b, void *data) {
    (void)b;
    (void)data;

    for (int32 i = 0; i < cecup.rows_visible_len; i += 1) {
        int32 row_id;

        row_id = cecup.rows_visible[i];
        cecup.rows_selected[row_id] = 1;
    }

    update_list_from_rows();
    update_visible_checkboxes(cecup.tree[L], L);
    update_visible_checkboxes(cecup.tree[R], R);
    return;
}

static void
on_ignore_response(GtkDialog *dialog, int32 response_id, void *data) {
    GtkTextBuffer *buffer;

    buffer = data;
    if (response_id == GTK_RESPONSE_ACCEPT) {
        GtkTextIter start;
        GtkTextIter end;
        char *content;

        gtk_text_buffer_get_bounds(buffer, &start, &end);
        content = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
        g_file_set_contents(cecup.ignore_path, content, -1, NULL);
        g_free(content);
        invalidate_preview();
    }
    gtk_window_destroy(GTK_WINDOW(dialog));
    return;
}

static void
on_ignore_clicked(GtkWidget *b, void *data) {
    GtkWidget *dialog;
    GtkWidget *scroll;
    GtkWidget *view;
    GtkTextBuffer *buffer;
    char *text;

    (void)data;
    (void)b;
    dialog = gtk_dialog_new_with_buttons(
        _("Ignore Rules"), GTK_WINDOW(cecup.gtk_window), GTK_DIALOG_MODAL,
        "_Save", GTK_RESPONSE_ACCEPT, "_Close", GTK_RESPONSE_CLOSE, NULL);

    gtk_window_set_default_size(GTK_WINDOW(dialog), 600, 500);
    scroll = gtk_scrolled_window_new();
    view = gtk_text_view_new();
    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));

    if (g_file_get_contents(cecup.ignore_path, &text, NULL, NULL)) {
        gtk_text_buffer_set_text(buffer, text, -1);
        g_free(text);
    }

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), view);
    gtk_box_append(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), scroll);
    gtk_widget_set_vexpand(scroll, TRUE);

    g_signal_connect(dialog, "response", G_CALLBACK(on_ignore_response), buffer);
    gtk_widget_show(dialog);
    return;
}

static void
on_invert_clicked(GtkWidget *b, void *data) {
    char path_src[MAX_PATH_LENGTH];
    char path_dst[MAX_PATH_LENGTH];
    char *entry_text;
    int32 entry_len;

    (void)b;
    (void)data;

    entry_text = (char *)gtk_editable_get_text(GTK_EDITABLE(cecup.dir_entry[L]));
    if ((entry_len = strlen32(entry_text)) >= (MAX_PATH_LENGTH / 2)) {
        LOG_ERROR("Error: source directory path is too long.\n");
        return;
    }
    memcpy64(path_src, entry_text, entry_len + 1);

    entry_text = (char *)gtk_editable_get_text(GTK_EDITABLE(cecup.dir_entry[R]));
    if ((entry_len = strlen32(entry_text)) >= (MAX_PATH_LENGTH / 2)) {
        LOG_ERROR("Error: source directory path is too long.\n");
        return;
    }
    memcpy64(path_dst, entry_text, entry_len + 1);

    gtk_editable_set_text(GTK_EDITABLE(cecup.dir_entry[L]), path_dst);
    gtk_editable_set_text(GTK_EDITABLE(cecup.dir_entry[R]), path_src);
    invalidate_preview();
    return;
}

static void
on_browse_response_src(GtkDialog *dialog, int32 response_id, void *data) {
    (void)data;
    if (response_id == GTK_RESPONSE_ACCEPT) {
        GFile *file;
        char *path;

        file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(dialog));
        path = g_file_get_path(file);
        gtk_editable_set_text(GTK_EDITABLE(cecup.dir_entry[L]), path);
        g_free(path);
        g_object_unref(file);
        invalidate_preview();
    }
    gtk_window_destroy(GTK_WINDOW(dialog));
    return;
}

static void
on_browse_src(GtkWidget *b, void *data) {
    GtkWidget *dialog;

    (void)data;
    (void)b;
    dialog = gtk_file_chooser_dialog_new(
        _("Src"), GTK_WINDOW(cecup.gtk_window),
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "_Cancel",
        GTK_RESPONSE_CANCEL, "_Select", GTK_RESPONSE_ACCEPT, NULL);

    g_signal_connect(dialog, "response", G_CALLBACK(on_browse_response_src), NULL);
    gtk_widget_show(dialog);
    return;
}

static void
on_browse_response_dst(GtkDialog *dialog, int32 response_id, void *data) {
    (void)data;
    if (response_id == GTK_RESPONSE_ACCEPT) {
        GFile *file;
        char *path;

        file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(dialog));
        path = g_file_get_path(file);
        gtk_editable_set_text(GTK_EDITABLE(cecup.dir_entry[R]), path);
        g_free(path);
        g_object_unref(file);
        invalidate_preview();
    }
    gtk_window_destroy(GTK_WINDOW(dialog));
    return;
}

static void
on_browse_dst(GtkWidget *b, void *data) {
    GtkWidget *dialog;

    (void)data;
    (void)b;
    dialog = gtk_file_chooser_dialog_new(
        _("Dst"), GTK_WINDOW(cecup.gtk_window),
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "_Cancel",
        GTK_RESPONSE_CANCEL, "_Select", GTK_RESPONSE_ACCEPT, NULL);

    g_signal_connect(dialog, "response", G_CALLBACK(on_browse_response_dst), NULL);
    gtk_widget_show(dialog);
    return;
}

static void
on_scroll_sync(GtkAdjustment *s, void *d) {
    double v;

    v = gtk_adjustment_get_value(s);
    if (gtk_adjustment_get_value(GTK_ADJUSTMENT(d)) != v) {
        gtk_adjustment_set_value(GTK_ADJUSTMENT(d), v);
    }
    return;
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
            execute_menu_item(widget, menu_item);
            handled = TRUE;
            break;
        }
    }

    return handled;
}

static void
on_path_click_pressed(GtkGestureClick *gesture,
                      int32 npress, double x, double y,
                      void *data) {
    GtkWidget *editable;
    GtkWidget *tree;
    uint32 button;

    (void)x;
    (void)y;

    editable = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    tree = data;
    button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));

    if (button != GDK_BUTTON_PRIMARY) {
        return;
    }

    gtk_widget_grab_focus(tree);

    if (gtk_editable_label_get_editing(GTK_EDITABLE_LABEL(editable))) {
        return;
    }

    if (npress == 1) {
        void *pos_ptr;

        if ((pos_ptr = g_object_get_data(G_OBJECT(editable), "cecup-pos"))) {
            GtkSelectionModel *model;
            uint32 pos;

            model = gtk_column_view_get_model(GTK_COLUMN_VIEW(tree));
            pos = GPOINTER_TO_UINT(pos_ptr) - 1;
            gtk_selection_model_select_item(model, pos, TRUE);
        }

        gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    } else if (npress == 2) {
        gtk_editable_label_start_editing(GTK_EDITABLE_LABEL(editable));
        gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    }

    return;
}

static void
on_window_destroy(GtkWidget *widget, void *user_data) {
    (void)widget;
    (void)user_data;

    if (cecup.child_pid > 0) {
        xkill(-cecup.child_pid, SIGTERM);
    }

    if (cecup.refresh_id != 0) {
        g_source_remove(cecup.refresh_id);
        cecup.refresh_id = 0;
    }

    if (cecup.search_timeout_id != 0) {
        g_source_remove(cecup.search_timeout_id);
        cecup.search_timeout_id = 0;
    }

    return;
}

/* #if 0 == TESTING_on */
static inline void
on_functions_sink(void) {
    (void)on_window_destroy;
    (void)on_tree_tooltip;
    (void)on_tree_button_press;
    (void)on_tree_key_press;
    (void)on_scroll_sync;
    (void)on_browse_dst;
    (void)on_browse_src;
    (void)on_invert_clicked;
    (void)on_ignore_clicked;
    (void)on_sort_changed;
    (void)on_filter_toggled;
    (void)on_stop_clicked;
    (void)on_sync_clicked;
    (void)on_preview_clicked;
    (void)on_reset_clicked;
    (void)on_delete_ignored_toggled;
    (void)on_preview_setting_toggled;
    (void)on_search_changed;
    (void)on_config_changed;
    (void)on_log_button_press;
    (void)on_log_copy;
    (void)on_path_editing_notify;
    (void)on_path_click_pressed;
}
/* #endif */

#if TESTING_on
int
main(void) {
    ASSERT(true);
    exit(EXIT_SUCCESS);
}
#endif

#endif /* ON_C */
