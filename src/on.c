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
#if !defined(TESTING)
#define TESTING 0
#endif

static gboolean
unparent_popover_idle(void *data) {
    GtkWidget *widget = data;

    if (gtk_widget_get_parent(widget) != NULL) {
        gtk_widget_unparent(widget);
    }
    g_object_unref(widget);

    return G_SOURCE_REMOVE;
}

static void
on_popover_closed(GtkWidget *popover, void *data) {
    (void)data;
    g_object_ref(popover);
    g_idle_add(unparent_popover_idle, popover);
    return;
}

static void
execute_menu_item_from_key_press(GtkWidget *tree, CecupMenuItem *menu_item) {
    GtkSelectionModel *selection;
    GtkSingleSelection *single_sel;
    uint32 pos;
    int32 row_id;
    char *filepath;
    int8 side;
    int32 path_len;

    if (menu_item == NULL) {
        return;
    }

    selection = gtk_column_view_get_model(GTK_COLUMN_VIEW(tree));
    side = (int8)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(tree), "side"));

    single_sel = GTK_SINGLE_SELECTION(selection);

    if ((gtk_widget_get_sensitive(cecup.stop_button))) {
        if ((menu_item->callback == on_menu_rename)
            || (menu_item->callback == on_menu_delete)
            || (menu_item->callback == on_menu_apply)) {
            LOG_ERROR(_("Action blocked: Background task is running.\n"));
            return;
        }
    }

    if ((pos = gtk_single_selection_get_selected(single_sel)) == GTK_INVALID_LIST_POSITION) {
        return;
    }

    if (pos >= (uint32)cecup.rows_visible_len) {
        error("Error in %s: list position returned by gtk is out of range for cecup.rows array.\n",
              __func__);
        return;
    }
    row_id = cecup.rows_visible[pos];
    filepath = item_path_side(row_id, side);
    path_len = item_path_len_side(row_id, side);

    if (filepath || (menu_item->callback == on_menu_rename)) {
        enum Action actions[2];
        enum Reason reason;
        Message *message = xmalloc(SIZEOF(*message));
        memset64(message, 0, SIZEOF(*message));

        if (filepath) {
            message->src_path_len = path_len;
            message->src_path = xmalloc(path_len + 1);
            memcpy64(message->src_path, filepath, path_len + 1);
        }

        item_get_actions_reasons(row_id, &actions[L], &actions[R], &reason);

        message->action = actions[side];
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
    aux_invalidate_preview();
    cecup_get_dirs();
    save_config();
    return;
}

static gboolean
on_search_timeout(void *data) {
    (void)data;

    cecup.search_timeout_id = 0;
    if (cecup.rows_len <= 0) {
        return G_SOURCE_REMOVE;
    }

    update_list_from_rows(UPDATE_ROWS_COMPLETE);
    gtk_entry_set_icon_from_icon_name(GTK_ENTRY(cecup.search_entry), GTK_ENTRY_ICON_SECONDARY, NULL);
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
        free2(cecup.search_query, cecup.search_query_len + 1);
    }

    cecup.search_query = xmemdup(text, len + 1);
    cecup.search_query_len = len;

    if (cecup.search_timeout_id != 0) {
        g_source_remove(cecup.search_timeout_id);
    }

    gtk_entry_set_icon_from_icon_name(GTK_ENTRY(cecup.search_entry),
                                      GTK_ENTRY_ICON_SECONDARY, "view-refresh-symbolic");

    cecup.search_timeout_id = g_timeout_add(350, on_search_timeout, NULL);
    return;
}

static void
on_preview_setting_toggled(GtkCheckButton *button, void *data) {
    (void)button;
    (void)data;
    save_config();
    aux_invalidate_preview();
    return;
}

static void
on_delete_after_toggled(GtkCheckButton *button, void *data) {
    bool active;

    (void)data;

    if ((active = gtk_check_button_get_active(button))) {
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
        aux_invalidate_preview();
    }

    cecup.delete_after = active;
    save_config();
    return;
}

static void
on_delete_ignored_toggled(GtkCheckButton *button, void *data) {
    (void)data;

    cecup.delete_ignored = gtk_check_button_get_active(button);
    save_config();
    return;
}

static void
on_reset_clicked(GtkWidget *button, void *data) {
    (void)button;
    (void)data;

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup.filter_new), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup.filter_link), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup.filter_update), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup.filter_equal), FALSE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup.filter_delete), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup.filter_ignore), TRUE);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(cecup.check_fs_button), FALSE);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(cecup.delete_ignored_button), FALSE);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(cecup.delete_after_button), FALSE);

    gtk_editable_set_text(GTK_EDITABLE(cecup.diff_entry), "diff");
    gtk_editable_set_text(GTK_EDITABLE(cecup.term_entry), "xterm");
    save_config();
    aux_invalidate_preview();
    return;
}

static void
on_preview_clicked(GtkWidget *button, void *data) {

    (void)data;
    (void)button;

    if (!cecup_get_dirs()) {
        LOG_ERROR(_("Invalid paths. Aborting preview...\n"));
        return;
    }

    aux_protect_interface_from_user(true);
    update_progress_bar(0.0);

    {
        Message *message = xmalloc(SIZEOF(*message));
        memset64(message, 0, SIZEOF(*message));

        message->type = MSG_CLEAR_TREES;
        update_ui_handler(message);
    }

    {
        ThreadData *thread_data = xmalloc(SIZEOF(*thread_data));
        memset64(thread_data, 0, SIZEOF(*thread_data));

        xpthread_create(&cecup.work_thread, NULL, work_preview, thread_data);
    }

    return;
}

static void
on_sync_response(GtkDialog *dialog, int32 response_id, void *data) {
    ThreadData *thread_data;

    (void)data;
    gtk_window_destroy(GTK_WINDOW(dialog));

    if (response_id != GTK_RESPONSE_YES) {
        return;
    }

    aux_protect_interface_from_user(true);

    thread_data = xmalloc(SIZEOF(*thread_data));
    memset64(thread_data, 0, SIZEOF(*thread_data));

    xpthread_create(&cecup.work_thread, NULL, work_rsync, thread_data);
    return;
}

static void
on_sync_clicked(GtkWidget *button, void *data) {
    char *path_src;
    char *path_dst;
    GtkWidget *dialog;

    (void)data;
    (void)button;
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
on_stop_clicked(GtkWidget *button, void *data) {
    (void)button;
    (void)data;

    stop_working(true);
    return;
}

static void
on_filter_toggled(GtkToggleButton *button, void *data) {
    enum UpdateRowsType change = UPDATE_ROWS_FILTER_OUT;

    (void) data;

    if (cecup.rows_len <= 0) {
        LOG_ERROR(_("No files to filter. Click Analysis first"));
        return;
    }

    if (gtk_toggle_button_get_active(button)) {
        change = UPDATE_ROWS_COMPLETE;
    }

    update_list_from_rows(change);
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

    if (cecup.rows_len <= 0) {
        LOG_ERROR(_("No files to sort. Click Analysis first"));
        return;
    }

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
            update_list_from_rows(UPDATE_ROWS_SORT);
        }
    }

    return;
}

static void on_cell_toggled(GtkCheckButton *renderer, void *user_data);

static void
update_visible_checkboxes(GtkWidget *widget, int8 side) {
    GtkWidget *child;
    void *row_id_ptr;

    if (widget == NULL) {
        return;
    }

    if (GTK_IS_CHECK_BUTTON(widget)) {
        if ((row_id_ptr = g_object_get_data(G_OBJECT(widget), "cecup-row-id"))) {
            int32 row_id;

            row_id = GPOINTER_TO_INT(row_id_ptr) - 1;
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
    char *toggled_path;
    int32 toggled_path_len;
    bool is_active;
    int8 side;
    static bool in_update = false;
    int32 count_selected = 0;
    int64 total_size_bytes = 0;
    bool toggled_is_root;

    if (in_update) {
        return;
    }

    side = (int8)GPOINTER_TO_INT(user_data);

    if ((row_id_ptr = g_object_get_data(G_OBJECT(renderer), "cecup-row-id")) == NULL) {
        return;
    }

    row_id_toggled = GPOINTER_TO_INT(row_id_ptr) - 1;
    if ((row_id_toggled < 0) || (row_id_toggled >= cecup.rows_len)) {
        error("Error in %s: Invalid row_id=%d passed via \"cecup-row-id\" object data.\n",
              __func__, row_id_toggled);
        return;
    }

    is_active = gtk_check_button_get_active(renderer);
    if (cecup.rows_selected[row_id_toggled] == is_active) {
        return;
    }

    in_update = true;
    cecup.rows_selected[row_id_toggled] = is_active;

    toggled_path = item_path_get(row_id_toggled);
    toggled_path_len = item_path_len_get(row_id_toggled);
    toggled_is_root = aux_is_root(toggled_path);

    for (int32 row_id = 0; row_id < cecup.rows_len; row_id += 1) {
        char *other_path = item_path_get(row_id);
        int32 other_path_len = item_path_len_get(row_id);

        if (cecup.rows_selected[row_id_toggled]) {
            if (toggled_is_root) {
                cecup.rows_selected[row_id] = true;
            } else if (toggled_path[toggled_path_len - 1] == '/') {
                if (BEGINS_WITH(other_path, toggled_path, toggled_path_len)) {
                    cecup.rows_selected[row_id] = true;
                }
            }
        } else {
            if (toggled_is_root) {
                cecup.rows_selected[row_id] = false;
            } else {
                if (toggled_path[toggled_path_len - 1] == '/') {
                    if (BEGINS_WITH(other_path, toggled_path, toggled_path_len)) {
                        cecup.rows_selected[row_id] = false;
                    }
                }
                if (aux_is_root(other_path)) {
                    cecup.rows_selected[row_id] = false;
                } else if (other_path[other_path_len - 1] == '/') {
                    if (BEGINS_WITH(toggled_path, other_path, other_path_len)) {
                        cecup.rows_selected[row_id] = false;
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

            if ((size = item_size_side(row_id, L)) < 0) {
                size = 0;
            }

            if ((action_src == ACTION_NEW)
                || (action_src == ACTION_HARDLINK)
                || (action_src == ACTION_SYMLINK)
                || (action_src == ACTION_UPDATE)) {
                total_size_bytes += size;
            }
        }
    }

    update_stats_text(count_selected, total_size_bytes);

    g_list_model_items_changed(cecup.store, 0,
                               (uint32)cecup.rows_visible_len,
                               (uint32)cecup.rows_visible_len);
    update_visible_checkboxes(cecup.tree[side], side);

    in_update = false;
    return;
}

static void
on_unselect_all_clicked(GtkWidget *button, void *data) {
    (void)button;
    (void)data;

    for (int32 i = 0; i < cecup.rows_len; i += 1) {
        cecup.rows_selected[i] = false;
    }

    update_list_from_rows(UPDATE_ROWS_COMPLETE);
    update_visible_checkboxes(cecup.tree[L], L);
    update_visible_checkboxes(cecup.tree[R], R);
    return;
}

static void
on_select_all_visible_clicked(GtkWidget *button, void *data) {
    (void)button;
    (void)data;

    for (int32 i = 0; i < cecup.rows_visible_len; i += 1) {
        int32 row_id;

        row_id = cecup.rows_visible[i];
        cecup.rows_selected[row_id] = true;
    }

    update_list_from_rows(UPDATE_ROWS_SELECT);
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
        aux_invalidate_preview();
    }
    gtk_window_destroy(GTK_WINDOW(dialog));
    return;
}

static void
on_ignore_clicked(GtkWidget *button, void *data) {
    GtkWidget *dialog;
    GtkWidget *scroll;
    GtkWidget *view;
    GtkTextBuffer *buffer;
    char *text;

    (void)data;
    (void)button;

    dialog = gtk_dialog_new_with_buttons(_("Ignore Rules"), GTK_WINDOW(cecup.gtk_window),
                                         GTK_DIALOG_MODAL,
                                         "_Save", GTK_RESPONSE_ACCEPT, "_Close", GTK_RESPONSE_CLOSE,
                                         NULL);

    gtk_window_set_default_size(GTK_WINDOW(dialog), 600, 500);

    scroll = gtk_scrolled_window_new();
    view = gtk_text_view_new();

    gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), TRUE);

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
on_invert_clicked(GtkWidget *button, void *data) {
    char path_src[MAX_PATH_LENGTH];
    char path_dst[MAX_PATH_LENGTH];
    char *entry_text;
    int32 entry_len;

    (void)button;
    (void)data;

    entry_text = (char *)gtk_editable_get_text(GTK_EDITABLE(cecup.dir_entry[L]));
    if ((entry_len = strlen32(entry_text)) >= (MAX_PATH_LENGTH / 2)) {
        LOG_ERROR(_("Error: source directory path is too long.\n"));
        return;
    }
    memcpy64(path_src, entry_text, entry_len + 1);

    entry_text = (char *)gtk_editable_get_text(GTK_EDITABLE(cecup.dir_entry[R]));
    if ((entry_len = strlen32(entry_text)) >= (MAX_PATH_LENGTH / 2)) {
        LOG_ERROR(_("Error: destination directory path is too long.\n"));
        return;
    }
    memcpy64(path_dst, entry_text, entry_len + 1);

    gtk_editable_set_text(GTK_EDITABLE(cecup.dir_entry[L]), path_dst);
    gtk_editable_set_text(GTK_EDITABLE(cecup.dir_entry[R]), path_src);
    aux_invalidate_preview();
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
        aux_invalidate_preview();
    }
    gtk_window_destroy(GTK_WINDOW(dialog));
    return;
}

static void
on_browse_src(GtkWidget *button, void *data) {
    GtkWidget *dialog;

    (void)data;
    (void)button;
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
        aux_invalidate_preview();
    }
    gtk_window_destroy(GTK_WINDOW(dialog));
    return;
}

static void
on_browse_dst(GtkWidget *button, void *data) {
    GtkWidget *dialog;

    (void)data;
    (void)button;
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

    stop_working(true);
    if (cecup.work_thread) {
        error("Joining thread...\n");
        xpthread_join(&cecup.work_thread, NULL);
    }

    if (cecup.child_pid > 0) {
        int32 waited;
        int32 waited_count = 0;

        xkill(-cecup.child_pid, SIGTERM);

        while ((waited = waitpid(cecup.child_pid, NULL, 0)) < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == ECHILD) {
                break;
            }
            waited_count += 1;
            if (waited_count >= 10) {
                break;
            }
            error("Error waiting for child: %s.\n", strerror(errno));
            sleep(1);
        }

        if ((waited < 0) && (errno != ECHILD)) {
            xkill(-cecup.child_pid, SIGKILL);
        }
    }

    if (cecup.search_timeout_id != 0) {
        g_source_remove(cecup.search_timeout_id);
        cecup.search_timeout_id = 0;
    }

    return;
}

#if TESTING == (0 == TESTING_on)
static inline void
on_functions_sink(void) {
    (void)on_functions_sink;
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
    (void)on_select_all_visible_clicked;
    (void)on_unselect_all_clicked;
    return;
}
#endif

#if TESTING_on
#include "assert.c"

#define MOCK_WIDGET(var, constructor) do { \
    var = constructor; \
    g_object_ref_sink(var); \
} while(0)

int
main(void) {
    char *text_src;
    char *text_dst;
    int32 num_test_rows = 5;

    if (!gtk_init_check()) {
        exit(EXIT_SUCCESS);
    }

    // 1. Initialize global widgets with proper reference sinking to satisfy GTK
    MOCK_WIDGET(cecup.gtk_window, gtk_window_new());
    MOCK_WIDGET(cecup.stop_button, gtk_button_new());
    MOCK_WIDGET(cecup.sync_button, gtk_button_new());
    MOCK_WIDGET(cecup.filter_new, gtk_toggle_button_new());
    MOCK_WIDGET(cecup.filter_link, gtk_toggle_button_new());
    MOCK_WIDGET(cecup.filter_update, gtk_toggle_button_new());
    MOCK_WIDGET(cecup.filter_equal, gtk_toggle_button_new());
    MOCK_WIDGET(cecup.filter_delete, gtk_toggle_button_new());
    MOCK_WIDGET(cecup.filter_ignore, gtk_toggle_button_new());
    MOCK_WIDGET(cecup.check_fs_button, gtk_check_button_new());
    MOCK_WIDGET(cecup.delete_ignored_button, gtk_check_button_new());
    MOCK_WIDGET(cecup.delete_after_button, gtk_check_button_new());
    MOCK_WIDGET(cecup.diff_entry, gtk_entry_new());
    MOCK_WIDGET(cecup.term_entry, gtk_entry_new());
    MOCK_WIDGET(cecup.search_entry, gtk_entry_new());
    MOCK_WIDGET(cecup.dir_entry[L], gtk_entry_new());
    MOCK_WIDGET(cecup.dir_entry[R], gtk_entry_new());
    cecup.entry_id[L] = g_signal_connect(cecup.dir_entry[L], "changed", G_CALLBACK(gtk_widget_show), NULL);
    cecup.entry_id[R] = g_signal_connect(cecup.dir_entry[R], "changed", G_CALLBACK(gtk_widget_show), NULL);

    MOCK_WIDGET(cecup.stats_label, gtk_label_new(""));
    MOCK_WIDGET(cecup.tree[L], gtk_column_view_new(NULL));
    MOCK_WIDGET(cecup.tree[R], gtk_column_view_new(NULL));

    // Mock store for update_list_from_rows
    cecup.store = G_LIST_MODEL(cecup_list_model_new());

    // 2. Setup the row infrastructure
    cecup.rows_len = num_test_rows;
    cecup.rows_visible_len = num_test_rows;
    cecup.rows[L] = xmalloc(num_test_rows*SIZEOF(int32));
    cecup.rows[R] = xmalloc(num_test_rows*SIZEOF(int32));
    cecup.rows_visible = xmalloc(num_test_rows*SIZEOF(int32));
    cecup.rows_selected = xmalloc(num_test_rows*SIZEOF(uint8));

    for (int32 i = 0; i < num_test_rows; i += 1) {
        cecup.rows[L][i] = i;
        cecup.rows[R][i] = i;
        cecup.rows_visible[i] = i;
        cecup.rows_selected[i] = true;
    }

    // 3. Mock BOTH sides of the traversal (item_get_actions_reasons needs this)
    for (int32 side = 0; side < 2; side += 1) {
        Traversal *t = &cecup.traversal[side];
        t->nfiles = num_test_rows;
        t->paths = xmalloc(num_test_rows*SIZEOF(char *));
        t->paths_lens = xmalloc(num_test_rows*SIZEOF(int32));
        t->stats = xmalloc(num_test_rows*SIZEOF(struct stat));
        t->patterns = xmalloc(num_test_rows*SIZEOF(char *));
        t->symlink_targets = xmalloc(num_test_rows*SIZEOF(char *));

        // Zero out to ensure pointers are NULL and stats are clean
        memset64(t->stats, 0, num_test_rows*SIZEOF(struct stat));
        memset64(t->patterns, 0, num_test_rows*SIZEOF(char *));
        memset64(t->symlink_targets, 0, num_test_rows*SIZEOF(char *));

        for (int32 i = 0; i < num_test_rows; i += 1) {
            t->paths[i] = "test/path";
            t->paths_lens[i] = 9;
            t->stats[i].st_mode = S_IFREG | 0644; // Mock as regular files
        }
    }

    gtk_editable_set_text(GTK_EDITABLE(cecup.dir_entry[L]), "/home/user/src");
    gtk_editable_set_text(GTK_EDITABLE(cecup.dir_entry[R]), "/mnt/backup/dst");

    on_invert_clicked(NULL, NULL);

    text_src = (char *)gtk_editable_get_text(GTK_EDITABLE(cecup.dir_entry[L]));
    text_dst = (char *)gtk_editable_get_text(GTK_EDITABLE(cecup.dir_entry[R]));

    ASSERT(strcmp(text_src, "/mnt/backup/dst") == 0);
    ASSERT(strcmp(text_dst, "/home/user/src") == 0);

    /* --- Test on_search_changed query allocation --- */
    gtk_editable_set_text(GTK_EDITABLE(cecup.search_entry), "test_query");
    on_search_changed(GTK_EDITABLE(cecup.search_entry), NULL);

    ASSERT(cecup.search_query != NULL);
    ASSERT_EQUAL(cecup.search_query_len, 10);
    ASSERT(strcmp(cecup.search_query, "test_query") == 0);

    /* --- Test on_unselect_all_clicked bulk logic --- */
    on_unselect_all_clicked(NULL, NULL);

    for (int32 i = 0; i < num_test_rows; i += 1) {
        ASSERT(cecup.rows_selected[i] == false);
    }

    {
        GtkWidget *mock_widget;
        GtkWidget *mock_child;
        GtkAdjustment *mock_adj;
        GtkCheckButton *mock_check;
        GtkWidget *dialog_sync;
        GtkWidget *dialog_ignore;
        GtkWidget *dialog_src;
        GtkWidget *dialog_dst;
        GtkTextBuffer *mock_buffer;
        GtkGesture *mock_gesture;
        GtkWidget *mock_editable;

        MOCK_WIDGET(mock_widget, gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));

        mock_child = gtk_button_new();
        gtk_box_append(GTK_BOX(mock_widget), mock_child);

        MOCK_WIDGET(mock_adj, gtk_adjustment_new(0.0, 0.0, 100.0, 1.0, 10.0, 10.0));

        mock_check = GTK_CHECK_BUTTON(gtk_check_button_new());
        g_object_ref_sink(mock_check);

        MOCK_WIDGET(dialog_sync, gtk_dialog_new());
        MOCK_WIDGET(dialog_ignore, gtk_dialog_new());
        MOCK_WIDGET(dialog_src, gtk_dialog_new());
        MOCK_WIDGET(dialog_dst, gtk_dialog_new());

        mock_buffer = gtk_text_buffer_new(NULL);

        mock_gesture = gtk_gesture_click_new();

        MOCK_WIDGET(mock_editable, gtk_editable_label_new(""));
        gtk_widget_add_controller(mock_editable, GTK_EVENT_CONTROLLER(mock_gesture));

        g_object_ref(mock_child);
        unparent_popover_idle(mock_child);
        on_popover_closed(mock_widget, NULL);
        g_main_context_iteration(NULL, FALSE);

        execute_menu_item_from_key_press(NULL, NULL);

        cecup.config_path[0] = '\0';
        on_config_changed(NULL, NULL);

        on_search_timeout(NULL);
        on_preview_setting_toggled(NULL, NULL);
        on_delete_after_toggled(mock_check, NULL);
        on_delete_ignored_toggled(mock_check, NULL);
        on_reset_clicked(NULL, NULL);

        gtk_editable_set_text(GTK_EDITABLE(cecup.dir_entry[L]), "/invalid/path/for/test");
        on_preview_clicked(NULL, NULL);

        on_sync_response(GTK_DIALOG(dialog_sync), GTK_RESPONSE_REJECT, NULL);
        on_sync_clicked(NULL, NULL);
        on_stop_clicked(NULL, NULL);
        on_filter_toggled(NULL, NULL);

        on_sort_changed(NULL, 0, cecup.tree[L]);

        update_visible_checkboxes(mock_widget, 0);

        g_object_set_data(G_OBJECT(mock_check), "cecup-row-id", GINT_TO_POINTER(1));
        on_cell_toggled(mock_check, GINT_TO_POINTER(L));

        on_select_all_visible_clicked(NULL, NULL);

        on_ignore_response(GTK_DIALOG(dialog_ignore), GTK_RESPONSE_REJECT, mock_buffer);
        cecup.ignore_path[0] = '\0';
        on_ignore_clicked(NULL, NULL);

        on_browse_response_src(GTK_DIALOG(dialog_src), GTK_RESPONSE_REJECT, NULL);
        on_browse_src(NULL, NULL);

        on_browse_response_dst(GTK_DIALOG(dialog_dst), GTK_RESPONSE_REJECT, NULL);
        on_browse_dst(NULL, NULL);

        on_scroll_sync(mock_adj, mock_adj);

        on_path_click_pressed(GTK_GESTURE_CLICK(mock_gesture), 0, 0, 0, NULL);

        cecup.child_pid = 0;
        memset64(&cecup.work_thread, 0, SIZEOF(cecup.work_thread));
        on_window_destroy(NULL, NULL);

        g_object_unref(mock_widget);
        g_object_unref(mock_adj);
        g_object_unref(mock_check);
        g_object_unref(mock_editable);
        g_object_unref(dialog_sync);
        g_object_unref(dialog_ignore);
        g_object_unref(dialog_src);
        g_object_unref(dialog_dst);
        g_object_unref(mock_buffer);
    }

    /* Cleanup mocks */
    g_object_unref(cecup.gtk_window);
    g_object_unref(cecup.stop_button);
    g_object_unref(cecup.sync_button);
    g_object_unref(cecup.filter_new);
    g_object_unref(cecup.filter_link);
    g_object_unref(cecup.filter_update);
    g_object_unref(cecup.filter_equal);
    g_object_unref(cecup.filter_delete);
    g_object_unref(cecup.filter_ignore);
    g_object_unref(cecup.check_fs_button);
    g_object_unref(cecup.delete_ignored_button);
    g_object_unref(cecup.delete_after_button);
    g_object_unref(cecup.diff_entry);
    g_object_unref(cecup.term_entry);
    g_object_unref(cecup.search_entry);
    g_object_unref(cecup.dir_entry[L]);
    g_object_unref(cecup.dir_entry[R]);
    g_object_unref(cecup.stats_label);
    g_object_unref(cecup.tree[L]);
    g_object_unref(cecup.tree[R]);
    g_object_unref(cecup.store);

    if (cecup.search_query) {
        free2(cecup.search_query, cecup.search_query_len + 1);
    }

    free2(cecup.rows[L], num_test_rows*SIZEOF(int32));
    free2(cecup.rows[R], num_test_rows*SIZEOF(int32));
    free2(cecup.rows_visible, num_test_rows*SIZEOF(int32));
    free2(cecup.rows_selected, num_test_rows*SIZEOF(uint8));

    for (int32 side = 0; side < 2; side += 1) {
        Traversal *t = &cecup.traversal[side];
        free2(t->paths, num_test_rows*SIZEOF(char *));
        free2(t->paths_lens, num_test_rows*SIZEOF(int32));
        free2(t->stats, num_test_rows*SIZEOF(struct stat));
        free2(t->patterns, num_test_rows*SIZEOF(char *));
        free2(t->symlink_targets, num_test_rows*SIZEOF(char *));
    }

    exit(EXIT_SUCCESS);
}
#endif

#endif /* ON_C */
