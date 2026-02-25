#include <gtk/gtk.h>
#include <stdlib.h>
#include "cecup.h"
#include "rsync.c"
#include "config.c"

static void
free_cecup_row(CecupRow *row) {
    (void)row;
    return;
}

static void
free_task_list(GPtrArray *tasks) {
    for (int32 i = 0; i < (int32)tasks->len; i += 1) {
        UIUpdateData *t;

        t = (UIUpdateData *)g_ptr_array_index(tasks, i);
        g_mutex_lock(&cecup_state.ui_arena_mutex);
        if (t->filepath) {
            arena_pop(cecup_state.ui_arena, t->filepath);
        }
        if (t->src_base) {
            arena_pop(cecup_state.ui_arena, t->src_base);
        }
        if (t->dst_base) {
            arena_pop(cecup_state.ui_arena, t->dst_base);
        }
        if (t->term_cmd) {
            arena_pop(cecup_state.ui_arena, t->term_cmd);
        }
        if (t->diff_tool) {
            arena_pop(cecup_state.ui_arena, t->diff_tool);
        }
        if (t->message) {
            arena_pop(cecup_state.ui_arena, t->message);
        }
        arena_pop(cecup_state.ui_arena, t);
        g_mutex_unlock(&cecup_state.ui_arena_mutex);
    }

    g_ptr_array_unref(tasks);
    return;
}

static GPtrArray *
get_target_tasks(int32 side, char *clicked_path,
                 enum CecupAction clicked_action) {
    GPtrArray *tasks;
    char *shared_src;
    char *shared_dst;

    tasks = g_ptr_array_new();
    shared_src = (char *)gtk_entry_get_text(GTK_ENTRY(cecup_state.src_entry));
    shared_dst = (char *)gtk_entry_get_text(GTK_ENTRY(cecup_state.dst_entry));

    for (int32 i = 0; i < cecup_state.rows_count; i += 1) {
        CecupRow *row;

        row = cecup_state.rows[i];
        if (row->selected) {
            char *f_path;
            enum CecupAction action;
            UIUpdateData *task;

            f_path = (side == 0) ? row->src_path : row->dst_path;
            action = (side == 0) ? row->src_action : row->dst_action;

            if (g_strcmp0(f_path, "-") != 0) {
                g_mutex_lock(&cecup_state.ui_arena_mutex);
                task = arena_push(cecup_state.ui_arena, sizeof(UIUpdateData));
                memset64(task, 0, sizeof(UIUpdateData));

                int64 path_len = strlen64(f_path) + 1;
                task->filepath = arena_push(cecup_state.ui_arena, path_len);
                memcpy64(task->filepath, f_path, path_len);

                int64 src_len = strlen64(shared_src) + 1;
                task->src_base = arena_push(cecup_state.ui_arena, src_len);
                memcpy64(task->src_base, shared_src, src_len);

                int64 dst_len = strlen64(shared_dst) + 1;
                task->dst_base = arena_push(cecup_state.ui_arena, dst_len);
                memcpy64(task->dst_base, shared_dst, dst_len);
                g_mutex_unlock(&cecup_state.ui_arena_mutex);

                task->action = action;
                task->side = side;
                g_ptr_array_add(tasks, task);
            }
        }
    }

    if (tasks->len == 0 && g_strcmp0(clicked_path, "-") != 0) {
        UIUpdateData *task;

        g_mutex_lock(&cecup_state.ui_arena_mutex);
        task = arena_push(cecup_state.ui_arena, sizeof(UIUpdateData));
        memset64(task, 0, sizeof(UIUpdateData));

        int64 path_len = strlen64(clicked_path) + 1;
        task->filepath = arena_push(cecup_state.ui_arena, path_len);
        memcpy64(task->filepath, clicked_path, path_len);

        int64 src_len = strlen64(shared_src) + 1;
        task->src_base = arena_push(cecup_state.ui_arena, src_len);
        memcpy64(task->src_base, shared_src, src_len);

        int64 dst_len = strlen64(shared_dst) + 1;
        task->dst_base = arena_push(cecup_state.ui_arena, dst_len);
        memcpy64(task->dst_base, shared_dst, dst_len);
        g_mutex_unlock(&cecup_state.ui_arena_mutex);

        task->action = clicked_action;
        task->side = side;
        g_ptr_array_add(tasks, task);
    }

    if (tasks->len == 0) {
        g_ptr_array_unref(tasks);
        return NULL;
    }

    return tasks;
}

static int
cecup_row_compare(const void *a, const void *b) {
    CecupRow *ra;
    CecupRow *rb;
    int32 result;

    ra = *(CecupRow **)a;
    rb = *(CecupRow **)b;
    result = 0;

    switch (cecup_state.sort_col) {
    case COL_SRC_PATH:
    case COL_DST_PATH:
        result = g_strcmp0(ra->src_path, rb->src_path);
        break;
    case COL_SIZE_RAW:
        result = (ra->size_raw > rb->size_raw)
                     ? 1
                     : (ra->size_raw < rb->size_raw ? -1 : 0);
        break;
    default:
        result = (ra->src_action > rb->src_action)
                     ? 1
                     : (ra->src_action < rb->src_action ? -1 : 0);
        break;
    }

    if (cecup_state.sort_order == GTK_SORT_DESCENDING) {
        result *= -1;
    }

    return result;
}

static void
refresh_ui_list(void) {
    gboolean show_new;
    gboolean show_hard;
    gboolean show_update;
    gboolean show_equal;
    gboolean show_delete;
    gboolean show_ignore;
    int32 current_store_count;
    int32 count_new = 0;
    int32 count_hard = 0;
    int32 count_update = 0;
    int32 count_equal = 0;
    int32 count_delete = 0;
    int32 count_ignore = 0;
    int64 total_size_bytes = 0;
    char *pretty_size;
    char stats_text[128];
    char btn_label[64];

    show_new = gtk_toggle_button_get_active(
        GTK_TOGGLE_BUTTON(cecup_state.filter_new));
    show_hard = gtk_toggle_button_get_active(
        GTK_TOGGLE_BUTTON(cecup_state.filter_hard));
    show_update = gtk_toggle_button_get_active(
        GTK_TOGGLE_BUTTON(cecup_state.filter_update));
    show_equal = gtk_toggle_button_get_active(
        GTK_TOGGLE_BUTTON(cecup_state.filter_equal));
    show_delete = gtk_toggle_button_get_active(
        GTK_TOGGLE_BUTTON(cecup_state.filter_delete));
    show_ignore = gtk_toggle_button_get_active(
        GTK_TOGGLE_BUTTON(cecup_state.filter_ignore));

    cecup_state.visible_count = 0;
    for (int32 i = 0; i < cecup_state.rows_count; i += 1) {
        CecupRow *row = cecup_state.rows[i];
        gboolean visible = FALSE;

        if (row->src_action == UI_ACTION_NEW) {
            visible = show_new;
            count_new += 1;
            total_size_bytes += row->size_raw;
        } else if (row->src_action == UI_ACTION_HARDLINK) {
            visible = show_hard;
            count_hard += 1;
            total_size_bytes += row->size_raw;
        } else if (row->src_action == UI_ACTION_UPDATE) {
            visible = show_update;
            count_update += 1;
            total_size_bytes += row->size_raw;
        } else if (row->src_action == UI_ACTION_EQUAL) {
            visible = show_equal;
            count_equal += 1;
        } else if (row->src_action == UI_ACTION_DELETED) {
            visible = show_delete;
            count_delete += 1;
        } else if (row->src_action == UI_ACTION_IGNORE) {
            visible = show_ignore;
            count_ignore += 1;
        }

        if (visible) {
            cecup_state.visible_rows[cecup_state.visible_count] = row;
            cecup_state.visible_count += 1;
        }
    }

    SNPRINTF(btn_label, "%s %d", EMOJI_NEW, count_new);
    gtk_button_set_label(GTK_BUTTON(cecup_state.filter_new), btn_label);
    SNPRINTF(btn_label, "%s %d", EMOJI_LINK, count_hard);
    gtk_button_set_label(GTK_BUTTON(cecup_state.filter_hard), btn_label);
    SNPRINTF(btn_label, "%s %d", EMOJI_UPDATE, count_update);
    gtk_button_set_label(GTK_BUTTON(cecup_state.filter_update), btn_label);
    SNPRINTF(btn_label, "%s %d", EMOJI_EQUAL, count_equal);
    gtk_button_set_label(GTK_BUTTON(cecup_state.filter_equal), btn_label);
    SNPRINTF(btn_label, "%s %d", EMOJI_DELETE, count_delete);
    gtk_button_set_label(GTK_BUTTON(cecup_state.filter_delete), btn_label);
    SNPRINTF(btn_label, "%s %d", EMOJI_IGNORE, count_ignore);
    gtk_button_set_label(GTK_BUTTON(cecup_state.filter_ignore), btn_label);

    pretty_size = bytes_pretty(total_size_bytes);
    SNPRINTF(stats_text, "Total Transfer Size: 📦 %s", pretty_size);
    gtk_label_set_text(GTK_LABEL(cecup_state.stats_label), stats_text);
    g_free(pretty_size);

    if (cecup_state.visible_count > 0) {
        qsort64(cecup_state.visible_rows, cecup_state.visible_count,
                sizeof(CecupRow *), cecup_row_compare);
    }

    current_store_count = gtk_tree_model_iter_n_children(
        GTK_TREE_MODEL(cecup_state.store), NULL);

    if (cecup_state.visible_count > current_store_count) {
        for (int32 i = 0; i < (cecup_state.visible_count - current_store_count);
             i += 1) {
            GtkTreeIter iter;
            gtk_list_store_append(cecup_state.store, &iter);
        }
    } else if (cecup_state.visible_count < current_store_count) {
        for (int32 i = 0; i < (current_store_count - cecup_state.visible_count);
             i += 1) {
            GtkTreeIter iter;
            if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(cecup_state.store),
                                              &iter)) {
                gtk_list_store_remove(cecup_state.store, &iter);
            }
        }
    }

    gtk_widget_queue_draw(cecup_state.l_tree);
    gtk_widget_queue_draw(cecup_state.r_tree);
    return;
}

static gboolean
refresh_ui_timeout_callback(gpointer data) {
    (void)data;
    refresh_ui_list();
    cecup_state.refresh_id = 0;
    return G_SOURCE_REMOVE;
}

static void
on_menu_apply(GtkWidget *m, gpointer data) {
    UIUpdateData *ud;
    GPtrArray *tasks;

    (void)m;
    ud = (UIUpdateData *)data;
    tasks = get_target_tasks(ud->side, ud->filepath, ud->action);

    if (tasks != NULL) {
        cecup_state.cancel_sync = 0;
        gtk_widget_set_sensitive(cecup_state.preview_button, FALSE);
        gtk_widget_set_sensitive(cecup_state.sync_button, FALSE);
        gtk_widget_set_sensitive(cecup_state.stop_button, TRUE);
        g_thread_new("bulk_sync", bulk_sync_worker, tasks);
    }

    g_mutex_lock(&cecup_state.ui_arena_mutex);
    arena_pop(cecup_state.ui_arena, ud->filepath);
    arena_pop(cecup_state.ui_arena, ud);
    g_mutex_unlock(&cecup_state.ui_arena_mutex);
    return;
}

static void
on_menu_open(GtkWidget *m, gpointer data) {
    UIUpdateData *ud;
    GPtrArray *tasks;
    char cmd[4096];

    (void)m;
    ud = (UIUpdateData *)data;

    if ((tasks = get_target_tasks(ud->side, ud->filepath, ud->action))) {
        for (int32 i = 0; i < (int32)tasks->len; i += 1) {
            UIUpdateData *t;
            char *full;

            t = (UIUpdateData *)g_ptr_array_index(tasks, i);
            full = g_build_filename(ud->side == 0 ? t->src_base : t->dst_base,
                                    t->filepath, NULL);
            SNPRINTF(cmd, "xdg-open '%s' &", full);
            system(cmd);
            g_free(full);
        }
        free_task_list(tasks);
    }

    g_mutex_lock(&cecup_state.ui_arena_mutex);
    arena_pop(cecup_state.ui_arena, ud->filepath);
    arena_pop(cecup_state.ui_arena, ud);
    g_mutex_unlock(&cecup_state.ui_arena_mutex);
    return;
}

static void
on_menu_open_dir(GtkWidget *m, gpointer data) {
    UIUpdateData *ud;
    GPtrArray *tasks;
    char cmd[4096];

    (void)m;
    ud = (UIUpdateData *)data;
    tasks = get_target_tasks(ud->side, ud->filepath, ud->action);

    if (tasks != NULL) {
        for (int32 i = 0; i < (int32)tasks->len; i += 1) {
            UIUpdateData *t;
            char *full;
            char *dir;

            t = (UIUpdateData *)g_ptr_array_index(tasks, i);
            full = g_build_filename(ud->side == 0 ? t->src_base : t->dst_base,
                                    t->filepath, NULL);
            if ((dir = g_path_get_dirname(full)) != NULL) {
                SNPRINTF(cmd, "xdg-open '%s' &", dir);
                system(cmd);
                g_free(dir);
            }
            g_free(full);
        }
        free_task_list(tasks);
    }

    g_mutex_lock(&cecup_state.ui_arena_mutex);
    arena_pop(cecup_state.ui_arena, ud->filepath);
    arena_pop(cecup_state.ui_arena, ud);
    g_mutex_unlock(&cecup_state.ui_arena_mutex);
    return;
}

static void
on_menu_copy_relative(GtkWidget *m, gpointer data) {
    UIUpdateData *ud;
    GPtrArray *tasks;
    GString *buffer;

    (void)m;
    ud = (UIUpdateData *)data;
    buffer = g_string_new("");
    tasks = get_target_tasks(ud->side, ud->filepath, ud->action);

    if (tasks != NULL) {
        for (int32 i = 0; i < (int32)tasks->len; i += 1) {
            UIUpdateData *t;

            t = (UIUpdateData *)g_ptr_array_index(tasks, i);
            if (i > 0) {
                g_string_append(buffer, "\n");
            }
            g_string_append(buffer, t->filepath);
        }
        gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),
                               buffer->str, -1);
        free_task_list(tasks);
    }

    g_string_free(buffer, TRUE);
    g_mutex_lock(&cecup_state.ui_arena_mutex);
    arena_pop(cecup_state.ui_arena, ud->filepath);
    arena_pop(cecup_state.ui_arena, ud);
    g_mutex_unlock(&cecup_state.ui_arena_mutex);
    return;
}

static void
on_menu_copy_full(GtkWidget *m, gpointer data) {
    UIUpdateData *ud;
    GPtrArray *tasks;
    GString *buffer;

    (void)m;
    ud = (UIUpdateData *)data;
    buffer = g_string_new("");
    tasks = get_target_tasks(ud->side, ud->filepath, ud->action);

    if (tasks != NULL) {
        for (int32 i = 0; i < (int32)tasks->len; i += 1) {
            UIUpdateData *t;
            char *full;

            t = (UIUpdateData *)g_ptr_array_index(tasks, i);
            full = g_build_filename(ud->side == 0 ? t->src_base : t->dst_base,
                                    t->filepath, NULL);
            if (i > 0) {
                g_string_append(buffer, "\n");
            }
            g_string_append(buffer, full);
            g_free(full);
        }
        gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),
                               buffer->str, -1);
        free_task_list(tasks);
    }

    g_string_free(buffer, TRUE);
    g_mutex_lock(&cecup_state.ui_arena_mutex);
    arena_pop(cecup_state.ui_arena, ud->filepath);
    arena_pop(cecup_state.ui_arena, ud);
    g_mutex_unlock(&cecup_state.ui_arena_mutex);
    return;
}

static void
on_menu_delete(GtkWidget *m, gpointer data) {
    UIUpdateData *ud;
    GPtrArray *tasks;
    GtkWidget *dialog;
    int32 count;

    (void)m;
    ud = (UIUpdateData *)data;
    tasks = get_target_tasks(ud->side, ud->filepath, UI_ACTION_DELETE);

    if (tasks == NULL) {
        g_mutex_lock(&cecup_state.ui_arena_mutex);
        arena_pop(cecup_state.ui_arena, ud->filepath);
        arena_pop(cecup_state.ui_arena, ud);
        g_mutex_unlock(&cecup_state.ui_arena_mutex);
        return;
    }

    count = (int32)tasks->len;
    dialog = gtk_message_dialog_new(GTK_WINDOW(cecup_state.gtk_window),
                                    GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING,
                                    GTK_BUTTONS_YES_NO,
                                    "Permanently delete %d item(s)?", count);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES) {
        cecup_state.cancel_sync = 0;
        gtk_widget_set_sensitive(cecup_state.preview_button, FALSE);
        gtk_widget_set_sensitive(cecup_state.sync_button, FALSE);
        gtk_widget_set_sensitive(cecup_state.stop_button, TRUE);
        g_thread_new("bulk_delete", bulk_sync_worker, tasks);
    } else {
        free_task_list(tasks);
    }

    gtk_widget_destroy(dialog);
    g_mutex_lock(&cecup_state.ui_arena_mutex);
    arena_pop(cecup_state.ui_arena, ud->filepath);
    arena_pop(cecup_state.ui_arena, ud);
    g_mutex_unlock(&cecup_state.ui_arena_mutex);
    return;
}

static void
on_menu_diff(GtkWidget *m, gpointer data) {
    UIUpdateData *ud;
    GPtrArray *tasks;
    char *diff_tool;
    char *term_cmd;

    (void)m;
    ud = (UIUpdateData *)data;
    diff_tool = (char *)gtk_entry_get_text(GTK_ENTRY(cecup_state.diff_entry));
    term_cmd = (char *)gtk_entry_get_text(GTK_ENTRY(cecup_state.term_entry));
    tasks = get_target_tasks(ud->side, ud->filepath, ud->action);

    if (tasks != NULL) {
        for (int32 i = 0; i < (int32)tasks->len; i += 1) {
            UIUpdateData *t;

            t = (UIUpdateData *)g_ptr_array_index(tasks, i);
            g_mutex_lock(&cecup_state.ui_arena_mutex);
            int64 diff_len = strlen64(diff_tool) + 1;
            t->diff_tool = arena_push(cecup_state.ui_arena, diff_len);
            memcpy64(t->diff_tool, diff_tool, diff_len);

            int64 term_len = strlen64(term_cmd) + 1;
            t->term_cmd = arena_push(cecup_state.ui_arena, term_len);
            memcpy64(t->term_cmd, term_cmd, term_len);
            g_mutex_unlock(&cecup_state.ui_arena_mutex);

            g_thread_new("diff_worker", diff_worker, t);
        }
        g_ptr_array_unref(tasks);
    }

    g_mutex_lock(&cecup_state.ui_arena_mutex);
    arena_pop(cecup_state.ui_arena, ud->filepath);
    arena_pop(cecup_state.ui_arena, ud);
    g_mutex_unlock(&cecup_state.ui_arena_mutex);
    return;
}

static void
on_menu_exclude_ext(GtkWidget *m, gpointer data) {
    UIUpdateData *ud;
    GPtrArray *tasks;
    FILE *fp;

    (void)m;
    ud = (UIUpdateData *)data;
    tasks = get_target_tasks(ud->side, ud->filepath, ud->action);

    if (tasks != NULL) {
        if ((fp = fopen(cecup_state.exclude_path, "a")) != NULL) {
            for (uint32 i = 0; i < tasks->len; i += 1) {
                UIUpdateData *t;
                char *ext;

                t = (UIUpdateData *)g_ptr_array_index(tasks, i);
                if ((ext = strrchr(t->filepath, '.')) != NULL) {
                    fprintf(fp, "\n*%s", ext);
                }
            }
            fclose(fp);
            on_preview_clicked(NULL, NULL);
        }
        free_task_list(tasks);
    }

    g_mutex_lock(&cecup_state.ui_arena_mutex);
    arena_pop(cecup_state.ui_arena, ud->filepath);
    arena_pop(cecup_state.ui_arena, ud);
    g_mutex_unlock(&cecup_state.ui_arena_mutex);
    return;
}

static void
on_menu_exclude_dir(GtkWidget *m, gpointer data) {
    UIUpdateData *ud;
    GPtrArray *tasks;
    FILE *fp;

    (void)m;
    ud = (UIUpdateData *)data;
    tasks = get_target_tasks(ud->side, ud->filepath, ud->action);

    if (tasks != NULL) {
        if ((fp = fopen(cecup_state.exclude_path, "a")) != NULL) {
            for (int32 i = 0; i < (int32)tasks->len; i += 1) {
                UIUpdateData *t;
                char *dir;

                t = (UIUpdateData *)g_ptr_array_index(tasks, i);
                if ((dir = g_path_get_dirname(t->filepath)) != NULL) {
                    if (g_strcmp0(dir, ".") != 0) {
                        fprintf(fp, "\n/%s/", dir);
                    }
                    g_free(dir);
                }
            }
            fclose(fp);
            on_preview_clicked(NULL, NULL);
        }
        free_task_list(tasks);
    }

    g_mutex_lock(&cecup_state.ui_arena_mutex);
    arena_pop(cecup_state.ui_arena, ud->filepath);
    arena_pop(cecup_state.ui_arena, ud);
    g_mutex_unlock(&cecup_state.ui_arena_mutex);
    return;
}

static void
on_config_changed(GtkWidget *widget, gpointer data) {
    (void)widget;
    (void)data;
    save_config();
    return;
}

static void
on_reset_clicked(GtkWidget *b, gpointer data) {
    (void)b;
    (void)data;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup_state.filter_new),
                                 TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup_state.filter_hard),
                                 TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup_state.filter_update),
                                 TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup_state.filter_equal),
                                 FALSE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup_state.filter_delete),
                                 TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup_state.filter_ignore),
                                 TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cecup_state.check_fs_toggle),
                                 FALSE);
    gtk_toggle_button_set_active(
        GTK_TOGGLE_BUTTON(cecup_state.check_equal_toggle), TRUE);
    gtk_entry_set_text(GTK_ENTRY(cecup_state.diff_entry), "unidiff.bash");
    gtk_entry_set_text(GTK_ENTRY(cecup_state.term_entry), "xterm");
    save_config();
    return;
}

static void
on_stop_clicked(GtkWidget *b, gpointer data) {
    (void)b;
    (void)data;
    cecup_state.cancel_sync = 1;
    return;
}

static void
on_preview_clicked(GtkWidget *b, gpointer data) {
    char *s;
    char *d;
    ThreadData *td;

    (void)data;
    s = (char *)gtk_entry_get_text(GTK_ENTRY(cecup_state.src_entry));
    d = (char *)gtk_entry_get_text(GTK_ENTRY(cecup_state.dst_entry));
    (void)b;

    if (strlen64(s) < 1 || strlen64(d) < 1) {
        return;
    }

    cecup_state.cancel_sync = 0;
    gtk_widget_set_sensitive(cecup_state.preview_button, FALSE);
    gtk_widget_set_sensitive(cecup_state.sync_button, FALSE);
    gtk_widget_set_sensitive(cecup_state.stop_button, TRUE);

    g_mutex_lock(&cecup_state.ui_arena_mutex);
    td = arena_push(cecup_state.ui_arena, sizeof(ThreadData));
    memset64(td, 0, sizeof(ThreadData));
    g_mutex_unlock(&cecup_state.ui_arena_mutex);

    td->is_preview = 1;
    td->scan_equal = gtk_toggle_button_get_active(
        GTK_TOGGLE_BUTTON(cecup_state.check_equal_toggle));
    strncpy(td->src_path, s, MAX_PATH_LENGTH - 1);
    strncpy(td->dst_path, d, MAX_PATH_LENGTH - 1);
    g_thread_new("worker", sync_worker, td);
    return;
}

static void
on_filter_toggled(GtkToggleButton *b, gpointer data) {
    (void)data;
    (void)b;
    refresh_ui_list();
    save_config();
    return;
}

static void
on_sort_changed(GtkTreeSortable *s, gpointer d) {
    int32 id;
    GtkSortType o;

    (void)d;
    if (gtk_tree_sortable_get_sort_column_id(s, &id, &o)) {
        cecup_state.sort_col = id;
        cecup_state.sort_order = o;
        refresh_ui_list();
    }
    return;
}

static void
on_cell_toggled(GtkCellRendererToggle *cell, char *path_str, gpointer data) {
    GtkTreePath *p;
    int32 row_idx;

    (void)cell;
    (void)data;
    p = gtk_tree_path_new_from_string(path_str);
    row_idx = gtk_tree_path_get_indices(p)[0];

    if (row_idx >= 0 && row_idx < cecup_state.visible_count) {
        cecup_state.visible_rows[row_idx]->selected
            = !cecup_state.visible_rows[row_idx]->selected;
        gtk_widget_queue_draw(cecup_state.l_tree);
        gtk_widget_queue_draw(cecup_state.r_tree);
    }

    gtk_tree_path_free(p);
    return;
}

static void
on_exclude_clicked(GtkWidget *b, gpointer data) {
    GtkWidget *dialog;
    GtkWidget *scroll;
    GtkWidget *view;
    GtkTextBuffer *buffer;
    char *text;
    gsize len;

    (void)data;
    dialog = gtk_dialog_new_with_buttons(
        "Exclusions", GTK_WINDOW(cecup_state.gtk_window), GTK_DIALOG_MODAL,
        "_Save", GTK_RESPONSE_ACCEPT, "_Close", GTK_RESPONSE_CLOSE, NULL);
    (void)b;
    gtk_window_set_default_size(GTK_WINDOW(dialog), 600, 500);
    scroll = gtk_scrolled_window_new(NULL, NULL);
    view = gtk_text_view_new();
    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));

    if (g_file_get_contents(cecup_state.exclude_path, &text, &len, NULL)) {
        gtk_text_buffer_set_text(buffer, text, -1);
        g_free(text);
    }

    gtk_container_add(GTK_CONTAINER(scroll), view);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
                       scroll, TRUE, TRUE, 5);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        GtkTextIter s;
        GtkTextIter e;
        char *content;

        gtk_text_buffer_get_bounds(buffer, &s, &e);
        content = gtk_text_buffer_get_text(buffer, &s, &e, FALSE);
        g_file_set_contents(cecup_state.exclude_path, content, -1, NULL);
        g_free(content);
        on_preview_clicked(NULL, NULL);
    }
    gtk_widget_destroy(dialog);
    return;
}

static void
on_invert_clicked(GtkWidget *b, gpointer data) {
    char *path_src;
    char *path_dst;

    (void)b;
    (void)data;
    path_src = g_strdup(gtk_entry_get_text(GTK_ENTRY(cecup_state.src_entry)));
    path_dst = g_strdup(gtk_entry_get_text(GTK_ENTRY(cecup_state.dst_entry)));
    gtk_entry_set_text(GTK_ENTRY(cecup_state.src_entry), path_dst);
    gtk_entry_set_text(GTK_ENTRY(cecup_state.dst_entry), path_src);
    g_free(path_src);
    g_free(path_dst);
    on_preview_clicked(NULL, NULL);
    return;
}

static void
on_sync_clicked(GtkWidget *b, gpointer data) {
    char *path_src;
    char *path_dst;
    GtkWidget *dialog;

    (void)data;
    path_src = (char *)gtk_entry_get_text(GTK_ENTRY(cecup_state.src_entry));
    path_dst = (char *)gtk_entry_get_text(GTK_ENTRY(cecup_state.dst_entry));
    (void)b;
    dialog = gtk_message_dialog_new(GTK_WINDOW(cecup_state.gtk_window),
                                    GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
                                    GTK_BUTTONS_YES_NO, "Sync %s -> %s?",
                                    path_src, path_dst);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES) {
        ThreadData *thread_data;

        cecup_state.cancel_sync = 0;
        gtk_widget_set_sensitive(cecup_state.preview_button, FALSE);
        gtk_widget_set_sensitive(cecup_state.sync_button, FALSE);
        gtk_widget_set_sensitive(cecup_state.stop_button, TRUE);

        g_mutex_lock(&cecup_state.ui_arena_mutex);
        thread_data = arena_push(cecup_state.ui_arena, sizeof(ThreadData));
        memset64(thread_data, 0, sizeof(ThreadData));
        g_mutex_unlock(&cecup_state.ui_arena_mutex);

        thread_data->is_preview = 0;
        thread_data->scan_equal = 0;
        thread_data->check_different_fs = gtk_toggle_button_get_active(
            GTK_TOGGLE_BUTTON(cecup_state.check_fs_toggle));
        strncpy(thread_data->src_path, path_src, MAX_PATH_LENGTH - 1);
        strncpy(thread_data->dst_path, path_dst, MAX_PATH_LENGTH - 1);
        g_thread_new("worker", sync_worker, thread_data);
    }
    gtk_widget_destroy(dialog);
    return;
}

static void
on_browse_src(GtkWidget *b, gpointer data) {
    GtkWidget *dialog;

    (void)data;
    (void)b;
    dialog = gtk_file_chooser_dialog_new(
        "Src", GTK_WINDOW(cecup_state.gtk_window),
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "_Cancel", GTK_RESPONSE_CANCEL,
        "_Select", GTK_RESPONSE_ACCEPT, NULL);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *path;

        path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        gtk_entry_set_text(GTK_ENTRY(cecup_state.src_entry), path);
        g_free(path);
    }
    gtk_widget_destroy(dialog);
    return;
}

static void
on_browse_dst(GtkWidget *b, gpointer data) {
    GtkWidget *dialog;

    (void)data;
    (void)b;
    dialog = gtk_file_chooser_dialog_new(
        "Dst", GTK_WINDOW(cecup_state.gtk_window),
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "_Cancel", GTK_RESPONSE_CANCEL,
        "_Select", GTK_RESPONSE_ACCEPT, NULL);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *path;

        path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        gtk_entry_set_text(GTK_ENTRY(cecup_state.dst_entry), path);
        g_free(path);
    }
    gtk_widget_destroy(dialog);
    return;
}

static void
on_scroll_sync(GtkAdjustment *s, gpointer d) {
    double v;

    v = gtk_adjustment_get_value(s);
    if (gtk_adjustment_get_value(GTK_ADJUSTMENT(d)) != v) {
        gtk_adjustment_set_value(GTK_ADJUSTMENT(d), v);
    }
    return;
}

static gboolean
on_tree_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    int32 side;
    GtkTreePath *path;

    (void)data;
    side = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "side"));

    if (event->type != GDK_BUTTON_PRESS) {
        return false;
    }

    switch (event->button) {
    case GDK_BUTTON_SECONDARY:
        if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), (gint)event->x,
                                          (gint)event->y, &path, NULL, NULL,
                                          NULL)) {
            int32 row_idx;
            CecupRow *row;
            UIUpdateData *ud;
            GtkWidget *menu;
            GtkWidget *item;
            GtkWidget *sub;
            GtkWidget *sub_ext;
            GtkWidget *sub_dir;
            int32 is_disabled;
            char *f_path;
            char *other_path;
            enum CecupAction action;

            row_idx = gtk_tree_path_get_indices(path)[0];
            row = cecup_state.visible_rows[row_idx];

            f_path = (side == 0) ? row->src_path : row->dst_path;
            other_path = (side == 0) ? row->dst_path : row->src_path;
            action = (side == 0) ? row->src_action : row->dst_action;

            g_mutex_lock(&cecup_state.ui_arena_mutex);
            ud = arena_push(cecup_state.ui_arena, sizeof(UIUpdateData));
            memset64(ud, 0, sizeof(UIUpdateData));
            int64 path_len = strlen64(f_path) + 1;
            ud->filepath = arena_push(cecup_state.ui_arena, path_len);
            memcpy64(ud->filepath, f_path, path_len);
            g_mutex_unlock(&cecup_state.ui_arena_mutex);

            ud->action = action;
            ud->side = side;

            menu = gtk_menu_new();
            item = gtk_menu_item_new_with_label("Open File");
            g_signal_connect(item, "activate", G_CALLBACK(on_menu_open), ud);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

            item = gtk_menu_item_new_with_label("Open Folder");
            g_signal_connect(item, "activate", G_CALLBACK(on_menu_open_dir),
                             ud);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

            item = gtk_menu_item_new_with_label("Copy Relative Path");
            g_signal_connect(item, "activate",
                             G_CALLBACK(on_menu_copy_relative), ud);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

            item = gtk_menu_item_new_with_label("Copy Full Path");
            g_signal_connect(item, "activate", G_CALLBACK(on_menu_copy_full),
                             ud);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

            item = gtk_menu_item_new_with_label("Apply");
            g_signal_connect(item, "activate", G_CALLBACK(on_menu_apply), ud);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

            item = gtk_menu_item_new_with_label("Exclude...");
            sub = gtk_menu_new();
            gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), sub);
            sub_ext = gtk_menu_item_new_with_label("Ext");
            g_signal_connect(sub_ext, "activate",
                             G_CALLBACK(on_menu_exclude_ext), ud);
            gtk_menu_shell_append(GTK_MENU_SHELL(sub), sub_ext);
            sub_dir = gtk_menu_item_new_with_label("Dir");
            g_signal_connect(sub_dir, "activate",
                             G_CALLBACK(on_menu_exclude_dir), ud);
            gtk_menu_shell_append(GTK_MENU_SHELL(sub), sub_dir);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

            item = gtk_menu_item_new_with_label("Diff");
            is_disabled = (g_strcmp0(f_path, "-") == 0
                           || g_strcmp0(other_path, "-") == 0
                           || action == UI_ACTION_HARDLINK);
            if (is_disabled) {
                gtk_widget_set_sensitive(item, FALSE);
            } else {
                g_signal_connect(item, "activate", G_CALLBACK(on_menu_diff),
                                 ud);
            }
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

            item = gtk_menu_item_new_with_label("Delete");
            if (g_strcmp0(f_path, "-") == 0) {
                gtk_widget_set_sensitive(item, FALSE);
            } else {
                g_signal_connect(item, "activate", G_CALLBACK(on_menu_delete),
                                 ud);
            }
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

            gtk_widget_show_all(menu);
            gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
            gtk_tree_path_free(path);
            return TRUE;
        }
        break;
    default:
        break;
    }
    return FALSE;
}

static gboolean
on_tree_tooltip(GtkWidget *w, gint x, gint y, gboolean k, GtkTooltip *t,
                gpointer d) {
    GtkTreePath *p;
    GtkTreeViewColumn *c;
    gint bx;
    gint by;

    (void)k;
    (void)d;
    gtk_tree_view_convert_widget_to_bin_window_coords(GTK_TREE_VIEW(w), x, y,
                                                      &bx, &by);
    if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(w), bx, by, &p, &c, NULL,
                                      NULL)) {
        int32 idx;
        int32 side;
        int32 view_col_idx;
        char *tip_text;

        idx = gtk_tree_path_get_indices(p)[0];
        side = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "side"));
        view_col_idx = -1;
        tip_text = NULL;

        for (int32 i = 0; i < 4; i += 1) {
            if (c == gtk_tree_view_get_column(GTK_TREE_VIEW(w), i)) {
                view_col_idx = i;
                break;
            }
        }

        if (idx >= 0 && idx < cecup_state.visible_count) {
            CecupRow *row;
            char *path;
            enum CecupAction action;

            row = cecup_state.visible_rows[idx];
            path = (side == 0) ? row->src_path : row->dst_path;
            action = (side == 0) ? row->src_action : row->dst_action;

            if (view_col_idx == 1) {
                const char **strings;
                strings = (side == 0) ? src_action_strings : dst_action_strings;
                tip_text = g_strdup(strings[action]);
            } else if (view_col_idx == 2) {
                tip_text = g_strdup_printf("%s: %s", path,
                                           reason_strings[row->reason]);
            } else if (view_col_idx == 3) {
                tip_text
                    = g_strdup_printf("%s: %ld bytes", path, row->size_raw);
            }
        }

        if (tip_text) {
            gtk_tooltip_set_text(t, tip_text);
            g_free(tip_text);
            gtk_tree_path_free(p);
            return TRUE;
        }
        gtk_tree_path_free(p);
    }
    return FALSE;
}

static gboolean
update_ui_handler(gpointer user_data) {
    UIUpdateData *data;

    data = (UIUpdateData *)user_data;

    switch (data->type) {
    case DATA_TYPE_LOG: {
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(cecup_state.log_buffer, &end);
        gtk_text_buffer_insert(cecup_state.log_buffer, &end, data->message, -1);
        break;
    }
    case DATA_TYPE_PROGRESS_RSYNC:
        gtk_progress_bar_set_fraction(
            GTK_PROGRESS_BAR(cecup_state.progress_rsync), data->fraction);
        break;
    case DATA_TYPE_PROGRESS_EQUAL:
        gtk_progress_bar_set_fraction(
            GTK_PROGRESS_BAR(cecup_state.progress_equal), data->fraction);
        break;
    case DATA_TYPE_PROGRESS_PREVIEW:
        gtk_progress_bar_set_fraction(
            GTK_PROGRESS_BAR(cecup_state.progress_preview), data->fraction);
        break;
    case DATA_TYPE_TREE_ROW: {
        CecupRow *row;
        char *bg_src = "#FFFFFF";
        char *bg_dst = "#FFFFFF";
        char *src_path_final = data->filepath;
        char *dst_path_final = data->filepath;
        enum CecupAction action_src = data->action;
        enum CecupAction action_dst = data->action;

        if (data->action == UI_ACTION_NEW) {
            bg_src = "#D4EDDA";
            dst_path_final = "-";
        } else if (data->action == UI_ACTION_UPDATE) {
            bg_src = "#CCE5FF";
            bg_dst = "#CCE5FF";
        } else if (data->action == UI_ACTION_HARDLINK) {
            bg_src = "#E2D1F9";
            bg_dst = "#E2D1F9";
        } else if (data->action == UI_ACTION_EQUAL) {
            bg_src = "#F0F0F0";
            bg_dst = "#F0F0F0";
        } else if (data->action == UI_ACTION_DELETE) {
            if (data->reason == UI_REASON_EXCLUDED) {
                bg_src = "#FFF3CD";
                bg_dst = "#FFF3CD";
                action_src = UI_ACTION_IGNORE;
                action_dst = UI_ACTION_DELETE;
            } else {
                bg_dst = "#F8D7DA";
                action_src = UI_ACTION_DELETED;
                action_dst = UI_ACTION_DELETE;
                src_path_final = "-";
            }
        }

        row = arena_push(cecup_state.row_arena, sizeof(CecupRow));
        memset64(row, 0, sizeof(CecupRow));
        row->src_action = action_src;
        row->dst_action = action_dst;

        char *pretty;
        pretty = bytes_pretty(data->size);
        int64 pretty_len = strlen64(pretty) + 1;
        row->size_text = arena_push(cecup_state.row_arena, pretty_len);
        memcpy64(row->size_text, pretty, pretty_len);
        g_free(pretty);

        row->size_raw = data->size;
        row->src_color = bg_src;
        row->dst_color = bg_dst;
        row->reason = data->reason;

        if (g_strcmp0(src_path_final, "-") == 0) {
            row->src_path = arena_push(cecup_state.row_arena, 2);
            memcpy64(row->src_path, "-", 2);
            int64 path_len = strlen64(data->filepath) + 1;
            row->dst_path = arena_push(cecup_state.row_arena, path_len);
            memcpy64(row->dst_path, data->filepath, path_len);
        } else if (g_strcmp0(dst_path_final, "-") == 0) {
            int64 path_len = strlen64(data->filepath) + 1;
            row->src_path = arena_push(cecup_state.row_arena, path_len);
            memcpy64(row->src_path, data->filepath, path_len);
            row->dst_path = arena_push(cecup_state.row_arena, 2);
            memcpy64(row->dst_path, "-", 2);
        } else {
            int64 path_len = strlen64(data->filepath) + 1;
            row->src_path = arena_push(cecup_state.row_arena, path_len);
            memcpy64(row->src_path, data->filepath, path_len);
            row->dst_path = arena_push(cecup_state.row_arena, path_len);
            memcpy64(row->dst_path, data->filepath, path_len);
        }

        if (cecup_state.rows_count >= cecup_state.rows_capacity) {
            int32 new_capacity;
            CecupRow **new_rows;
            CecupRow **new_visible;

            new_capacity = cecup_state.rows_capacity*2;
            new_rows = arena_push(cecup_state.row_arena,
                                  new_capacity*sizeof(CecupRow *));
            new_visible = arena_push(cecup_state.row_arena,
                                     new_capacity*sizeof(CecupRow *));

            memcpy64(new_rows, cecup_state.rows,
                     cecup_state.rows_count*sizeof(CecupRow *));
            memcpy64(new_visible, cecup_state.visible_rows,
                     cecup_state.rows_count*sizeof(CecupRow *));

            cecup_state.rows = new_rows;
            cecup_state.visible_rows = new_visible;
            cecup_state.rows_capacity = new_capacity;
        }
        cecup_state.rows[cecup_state.rows_count] = row;
        cecup_state.rows_count += 1;

        if (cecup_state.refresh_id == 0) {
            cecup_state.refresh_id
                = g_timeout_add(100, refresh_ui_timeout_callback, NULL);
        }
        break;
    }
    case DATA_TYPE_REMOVE_TREE_ROW: {
        for (int32 i = 0; i < cecup_state.rows_count; i += 1) {
            CecupRow *row = cecup_state.rows[i];
            if (g_strcmp0(row->src_path, data->filepath) == 0
                || g_strcmp0(row->dst_path, data->filepath) == 0) {
                free_cecup_row(row);
                for (int32 j = i; j < cecup_state.rows_count - 1; j += 1) {
                    cecup_state.rows[j] = cecup_state.rows[j + 1];
                }
                cecup_state.rows_count -= 1;
                break;
            }
        }
        if (cecup_state.refresh_id == 0) {
            cecup_state.refresh_id
                = g_timeout_add(50, refresh_ui_timeout_callback, NULL);
        }
        break;
    }
    case DATA_TYPE_ENABLE_BUTTONS:
        if (cecup_state.refresh_id != 0) {
            g_source_remove(cecup_state.refresh_id);
            cecup_state.refresh_id = 0;
        }
        refresh_ui_list();
        gtk_widget_set_sensitive(cecup_state.sync_button, TRUE);
        gtk_widget_set_sensitive(cecup_state.preview_button, TRUE);
        gtk_widget_set_sensitive(cecup_state.stop_button, FALSE);
        break;
    case DATA_TYPE_CLEAR_TREES:
        if (cecup_state.refresh_id != 0) {
            g_source_remove(cecup_state.refresh_id);
            cecup_state.refresh_id = 0;
        }
        arena_reset(cecup_state.row_arena);
        /* Re-allocate the initial arrays from the fresh arena */
        cecup_state.rows
            = arena_push(cecup_state.row_arena,
                         cecup_state.rows_capacity*sizeof(CecupRow *));
        cecup_state.visible_rows
            = arena_push(cecup_state.row_arena,
                         cecup_state.rows_capacity*sizeof(CecupRow *));

        cecup_state.rows_count = 0;
        cecup_state.visible_count = 0;
        gtk_list_store_clear(cecup_state.store);
        gtk_progress_bar_set_fraction(
            GTK_PROGRESS_BAR(cecup_state.progress_rsync), 0.0);
        gtk_progress_bar_set_fraction(
            GTK_PROGRESS_BAR(cecup_state.progress_equal), 0.0);
        gtk_progress_bar_set_fraction(
            GTK_PROGRESS_BAR(cecup_state.progress_preview), 0.0);
        break;
    default:
        break;
    }

    g_mutex_lock(&cecup_state.ui_arena_mutex);
    if (data->filepath) {
        arena_pop(cecup_state.ui_arena, data->filepath);
    }
    if (data->message) {
        arena_pop(cecup_state.ui_arena, data->message);
    }
    arena_pop(cecup_state.ui_arena, data);
    g_mutex_unlock(&cecup_state.ui_arena_mutex);
    return G_SOURCE_REMOVE;
}
