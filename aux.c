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

#if !defined(AUX_C)
#define AUX_C

#include <gtk/gtk.h>

#include "cecup.h"

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_aux 1
#elif !defined(TESTING_aux)
#define TESTING_aux 0
#endif

static void
invalidate_preview(void) {
    cecup.preview_dirty = true;
    /* Only update visibility if a task isn't currently running,
     * otherwise protect_interface_from_user will handle it when the task ends. */
    if (!gtk_widget_get_sensitive(cecup.stop_button)) {
        gtk_widget_set_sensitive(cecup.sync_button, FALSE);
        gtk_widget_set_tooltip_text(cecup.sync_button, _("Click Analysis first"));
    }
    return;
}

static void
protect_interface_from_user(bool state) {
    gtk_widget_set_sensitive(cecup.preview_button, !state);
    gtk_widget_set_sensitive(cecup.ignore_button, !state);
    gtk_widget_set_sensitive(cecup.dir_entry[L], !state);
    gtk_widget_set_sensitive(cecup.dir_entry[R], !state);
    gtk_widget_set_sensitive(cecup.invert_button, !state);
    gtk_widget_set_sensitive(cecup.stop_button, state);

    if (state) {
        gtk_widget_set_sensitive(cecup.sync_button, FALSE);
    } else {
        if (cecup.preview_dirty) {
            gtk_widget_set_sensitive(cecup.sync_button, FALSE);
            gtk_widget_set_tooltip_text(cecup.sync_button, _("Click Analysis first"));
        } else {
            gtk_widget_set_sensitive(cecup.sync_button, TRUE);
            gtk_widget_set_tooltip_text(cecup.sync_button,
                                        _("Start copying and updating all files"));
        }
    }
    cecup.stop_working = false;
    return;
}

static int32
traversal_push(Traversal *data, char *path, int32 path_len,
               struct stat *st, char *link_target, int32 link_target_len,
               char *matched_pattern, int32 matched_pattern_len) {
    int32 idx;

    if (data->nfiles >= data->ncapacity) {
        int64 lens_type_size = SIZEOF(*(data->paths_lens));

        ASSERT_EQUAL(SIZEOF(*(data->paths_lens)),
                     SIZEOF(*(data->link_targets_lens)));
        ASSERT_EQUAL(SIZEOF(*(data->paths_lens)),
                     SIZEOF(*(data->matched_patterns_lens)));
        ASSERT_EQUAL(SIZEOF(*(data->paths_lens)),
                     SIZEOF(*(data->nlinks)));

        if (data->ncapacity == 0) {
            data->ncapacity = 1024;
        } else {
            data->ncapacity *= 2;
        }

        data->stats = xrealloc(data->stats,
                               data->ncapacity*SIZEOF(*(data->stats)));

        data->paths = xrealloc(data->paths,
                               data->ncapacity*SIZEOF(char *));
        data->link_targets = xrealloc(data->link_targets,
                                      data->ncapacity*SIZEOF(char *));
        data->matched_patterns = xrealloc(data->matched_patterns,
                                          data->ncapacity*SIZEOF(char *));

        data->paths_lens = xrealloc(data->paths_lens,
                                    data->ncapacity*lens_type_size);
        data->link_targets_lens = xrealloc(data->link_targets_lens,
                                           data->ncapacity*lens_type_size);
        data->matched_patterns_lens = xrealloc(data->matched_patterns_lens,
                                               data->ncapacity*lens_type_size);
        data->nlinks= xrealloc(data->nlinks,
                               data->ncapacity*lens_type_size);
    }

    idx = data->nfiles;
    data->nfiles += 1;

    memset64(&data->stats[idx], 0, SIZEOF(struct stat));
    if (st) {
        memcpy64(&data->stats[idx], st, SIZEOF(struct stat));
    }

    data->paths[idx] = path;
    data->paths_lens[idx] = (int16)path_len;
    data->link_targets[idx] = link_target;
    data->link_targets_lens[idx] = (int16)link_target_len;
    data->matched_patterns[idx] = matched_pattern;
    data->matched_patterns_lens[idx] = (int16)matched_pattern_len;
    data->nlinks[idx] = 0;

    if (data->map) {
        hash_insert_fs_map(data->map, path, path_len, idx);
    }

    return idx;
}

static void
free_task_list(TaskList *tasks) {
    if (tasks == NULL) {
        return;
    }

    for (int32 i = 0; i < tasks->count; i += 1) {
        Task *task = tasks->items[i];

        free(task->link_target, task->link_target_len + 1);
        free(task->message, task->message_len);
        free(task, sizeof(*task));
    }

    free(tasks, STRUCT_ARRAY_SIZE(tasks, Task, tasks->count));
    return;
}

static TaskList *
get_target_tasks(int32 side, char *clicked_path,
                 enum CecupAction clicked_action) {
    TaskList *tasks;
    int64 tasks_size = STRUCT_ARRAY_SIZE(tasks, Task *, cecup.rows_len);
    int32 count = 0;

    tasks = xmalloc(tasks_size);
    memset64(tasks, 0, tasks_size);

    for (int32 i = 0; i < cecup.rows_len; i += 1) {
        CecupRow *row = cecup.rows[i];
        char *filepath;
        int32 path_len;
        enum CecupAction action;
        Task *task;

        if (!(row->selected)) {
            continue;
        }

        if (side == L) {
            filepath = row->src_path;
            action = row->src_action;
        } else {
            filepath = row->dst_path;
            action = row->dst_action;
        }
        path_len = row->path_len;

        if (filepath == NULL) {
            continue;
        }

        task = xmalloc(SIZEOF(*task));
        memset64(task, 0, SIZEOF(*task));

        task->path_len = path_len;
        task->path = xmalloc(path_len + 1);
        memcpy64(task->path, filepath, path_len + 1);

        if (row->link_target) {
            task->link_target_len = row->link_target_len;
            task->link_target
                = xmalloc(task->link_target_len + 1);
            memcpy64(task->link_target, row->link_target,
                     task->link_target_len + 1);
        }

        task->action = action;
        task->side = side;

        tasks->items[count] = task;
        count += 1;
    }

    tasks = xrealloc(tasks, STRUCT_ARRAY_SIZE(tasks, Task *, count + 1));
    tasks->count = count;

    if ((tasks->count == 0) && clicked_path) {
        Task *task = xmalloc(SIZEOF(*task));
        memset64(task, 0, SIZEOF(*task));

        task->path_len = strlen32(clicked_path);
        task->path = xmalloc(task->path_len + 1);
        memcpy64(task->path, clicked_path, task->path_len + 1);

        task->action = clicked_action;
        task->side = side;

        tasks->items[0] = task;
        tasks->count = 1;
    }

    return tasks;
}

static int32
cecup_row_compare(const void *a, const void *b) {
    CecupRow *row_a;
    CecupRow *row_b;
    int64 result;

    row_a = *(CecupRow **)a;
    row_b = *(CecupRow **)b;

#define COMPARE(A, B) \
    do { \
        if (A > B) { \
            result = 1; \
        } else if (A < B) { \
            result = -1; \
        } else { \
            result = 0; \
        } \
    } while (0)

    switch (cecup.sort_col) {
    case COL_SRC_PATH:
        if (row_a->src_path == NULL && row_b->src_path == NULL) {
            result = 0;
        } else if (row_a->src_path == NULL) {
            result = -1;
        } else if (row_b->src_path == NULL) {
            result = 1;
        } else {
            result = strcmp(row_a->src_path, row_b->src_path);
        }
        break;
    case COL_DST_PATH:
        if (row_a->dst_path == NULL && row_b->dst_path == NULL) {
            result = 0;
        } else if (row_a->dst_path == NULL) {
            result = -1;
        } else if (row_b->dst_path == NULL) {
            result = 1;
        } else {
            result = strcmp(row_a->dst_path, row_b->dst_path);
        }
        break;
    case COL_SIZE_RAW:
        COMPARE(row_a->src_size_raw, row_b->src_size_raw);
        break;
    case COL_MTIME_RAW:
        COMPARE(row_a->src_mtime_raw, row_b->src_mtime_raw);
        break;
    case COL_DST_ACTION:
    case COL_MTIME_TEXT:
    case COL_ROW_PTR:
    case COL_SELECTED:
    case COL_SIZE_TEXT:
    case COL_SRC_ACTION:
    case NUM_COLS:
    default:
        COMPARE(row_a->src_action, row_b->src_action);
        break;
    }

#undef COMPARE

    if (cecup.sort_order == GTK_SORT_DESCENDING) {
        result *= -1;
    }

    return (int32)result;
}

static void
cecup_reset_dir(int32 side) {
    if (side == L) {
        gtk_editable_set_text(GTK_EDITABLE(cecup.dir_entry[L]), "./");
    } else {
        gtk_editable_set_text(GTK_EDITABLE(cecup.dir_entry[R]), "./");
    }
    return;
}

static void
cecup_get_dirs(void) {
    char full_src[MAX_PATH_LENGTH];
    char full_dst[MAX_PATH_LENGTH];
    char *tmp_src;
    char *tmp_dst;

    tmp_src = (char *)gtk_editable_get_text(GTK_EDITABLE(cecup.dir_entry[L]));
    tmp_dst = (char *)gtk_editable_get_text(GTK_EDITABLE(cecup.dir_entry[R]));

    save_config();

    if (strlen32(tmp_src) <= 0) {
        LOG_ERROR("Error: Invalid source directory.\n");
        cecup_reset_dir(L);
        return;
    }
    if (strlen32(tmp_dst) <= 0) {
        LOG_ERROR("Error: Invalid source directory.\n");
        cecup_reset_dir(R);
        return;
    }

    if (realpath(tmp_src, full_src) == NULL) {
        LOG_ERROR("Error getting full path of %s: %s.\n", tmp_src,
                           strerror(errno));
        cecup_reset_dir(L);
        return;
    }
    if (realpath(tmp_dst, full_dst) == NULL) {
        LOG_ERROR("Error getting full path of %s: %s.\n", tmp_dst,
                           strerror(errno));
        cecup_reset_dir(R);
        return;
    }

    if (cecup.src_base) {
        free(cecup.src_base, cecup.src_base_len + 1);
    }
    if (cecup.dst_base) {
        free(cecup.dst_base, cecup.dst_base_len + 1);
    }

    {
        int32 len;

        len = strlen32(full_src);
        cecup.src_base = xmalloc(len + 1);
        memcpy64(cecup.src_base, full_src, len + 1);
        cecup.src_base_len = len;

        len = strlen32(full_dst);
        cecup.dst_base = xmalloc(len + 1);
        memcpy64(cecup.dst_base, full_dst, len + 1);
        cecup.dst_base_len = len;
    }

    g_signal_handler_block(cecup.dir_entry[L], cecup.src_entry_id);
    g_signal_handler_block(cecup.dir_entry[R], cecup.dst_entry_id);

    gtk_editable_set_text(GTK_EDITABLE(cecup.dir_entry[L]), cecup.src_base);
    gtk_editable_set_text(GTK_EDITABLE(cecup.dir_entry[R]), cecup.dst_base);

    g_signal_handler_unblock(cecup.dir_entry[L], cecup.src_entry_id);
    g_signal_handler_unblock(cecup.dir_entry[R], cecup.dst_entry_id);

    return;
}

static void
config_bool_set(GKeyFile *key, char *section, char *name, GtkWidget *button) {
    gboolean state;

    state = false;
    if (GTK_IS_CHECK_BUTTON(button)) {
        state = gtk_check_button_get_active(GTK_CHECK_BUTTON(button));
    } else if (GTK_IS_TOGGLE_BUTTON(button)) {
        state = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button));
    }

    g_key_file_set_boolean(key, section, name, state);
    return;
}

static void
save_config(void) {
    GKeyFile *key;
    char *out;
    gsize len;

    key = g_key_file_new();

    g_key_file_set_string(key, "Paths", "src",
                          gtk_editable_get_text(GTK_EDITABLE(cecup.dir_entry[L])));
    g_key_file_set_string(key, "Paths", "dst",
                          gtk_editable_get_text(GTK_EDITABLE(cecup.dir_entry[R])));
    g_key_file_set_string(key, "Tools", "diff",
                          gtk_editable_get_text(GTK_EDITABLE(cecup.diff_entry)));
    g_key_file_set_string(key, "Tools", "term",
                          gtk_editable_get_text(GTK_EDITABLE(cecup.term_entry)));

    config_bool_set(key, "Filters", "new",    cecup.filter_new);
    config_bool_set(key, "Filters", "hard",   cecup.filter_hard);
    config_bool_set(key, "Filters", "update", cecup.filter_update);
    config_bool_set(key, "Filters", "equal",  cecup.filter_equal);
    config_bool_set(key, "Filters", "delete", cecup.filter_delete);
    config_bool_set(key, "Filters", "ignore", cecup.filter_ignore);

    config_bool_set(key, "Options", "check_fs",        cecup.check_fs);
    config_bool_set(key, "Options", "delete_after",    cecup.delete_after);
    config_bool_set(key, "Options", "delete_excluded", cecup.delete_excluded);

    out = g_key_file_to_data(key, &len, NULL);
    g_file_set_contents(cecup.config_path, out, (gssize)len, NULL);

    g_free(out);
    g_key_file_free(key);
    return;
}

static void
log_internal(char *file, int line,
             enum DataType type, char *format, ...) {
    Message *message;
    char buffer[MAX_PATH_LENGTH*2];
    int32 n;
    int32 m;
    va_list va_args;
    char fileline[64];

    va_start(va_args, format);
    n = vsnprintf(buffer, SIZEOF(buffer), format, va_args);

    if ((n < 0) || (n >= SIZEOF(buffer))) {
        error("%s:%d: Error in vsnprintf(%s) (n = %lld)\n",
              file, line, format, (llong)n);
        fatal(EXIT_FAILURE);
    }
    va_end(va_args);

    message = xmalloc(SIZEOF(*message));
    memset64(message, 0, SIZEOF(*message));

    if (!RELEASING) {
        m = SNPRINTF(fileline, "%s:%d: ", file, line);
    } else {
        m = SNPRINTF(fileline, "%s", "");
    }

    message->text_len = n + m;
    message->text = xmalloc(n + m + 1);

    memcpy64(message->text, fileline, m);
    memcpy64(message->text + m, buffer, n + 1);

    message->type = type;
    g_idle_add(update_ui_handler, message);
    return;
}

static void
free_message(void *data) {
    Message *message = data;

    free(message->src_path, message->path_len + 1);
    free(message->old_path, message->old_path_len + 1);
    free(message->new_path, message->new_path_len + 1);
    free(message, sizeof(*message));
    return;
}

static char *
row_path_get(CecupRow *row) {
    if (row->src_path) {
        return row->src_path;
    } else if (row->dst_path) {
        return row->dst_path;
    } else {
        error2("Error: src_path and dst_path are NULL.\n");
        fatal(EXIT_FAILURE);
    }
}

#if 0 == TESTING_aux
static inline void
aux_functions_sink(void) {
    (void)cecup_get_dirs;
    (void)get_target_tasks;
    (void)free_task_list;
    (void)free_message;
    (void)cecup_row_compare;
    (void)traversal_push;
    (void)protect_interface_from_user;
    (void)invalidate_preview;
    return;
}
#endif

#if TESTING_aux
#include <assert.h>
#include <string.h>

#include "update.c"

int
main(void) {
    (void)cecup_get_dirs;
    (void)get_target_tasks;
    (void)free_task_list;
    (void)free_message;
    ASSERT(true);
    exit(EXIT_SUCCESS);
}

#endif

#endif /* AUX_C */
