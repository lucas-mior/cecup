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
#include "item.c"

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_aux 1
#elif !defined(TESTING_aux)
#define TESTING_aux 0
#endif
#if !defined(TESTING)
#define TESTING 0
#endif

static bool
aux_is_root(char *path) {
    if (path[0] == '.') {
        if (path[1] == '\0') {
            return true;
        }
        if ((path[1] == '/') && (path[2] == '\0')) {
            return true;
        }
    }
    return false;
}

static void
invalidate_preview(void) {
    cecup.preview_dirty = true;
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

    gtk_widget_set_sensitive(cecup.delete_after_button, !state);
    gtk_widget_set_sensitive(cecup.delete_ignored_button, !state);
    gtk_widget_set_sensitive(cecup.check_fs_button, !state);
    gtk_widget_set_sensitive(cecup.diff_entry, !state);
    gtk_widget_set_sensitive(cecup.term_entry, !state);
    gtk_widget_set_sensitive(cecup.browse_button[L], !state);
    gtk_widget_set_sensitive(cecup.browse_button[R], !state);

    if (state) {
        gtk_widget_set_sensitive(cecup.sync_button, FALSE);
    } else if (cecup.preview_dirty) {
        gtk_widget_set_sensitive(cecup.sync_button, FALSE);
        gtk_widget_set_tooltip_text(cecup.sync_button, _("Click Analysis first"));
    } else {
        gtk_widget_set_sensitive(cecup.sync_button, TRUE);
        gtk_widget_set_tooltip_text(cecup.sync_button, _("Start copying and updating all files"));
    }
    stop_working(false);
    return;
}

static void
traversal_allocate(Traversal *traversal) {
    int32 capacity = INITIAL_CAPACITY;

    traversal->arena = arena_create(SIZEMB(64));

    traversal->map = hash_create_fs_map(INITIAL_CAPACITY);
    traversal->inode_map = hash_create_inode_map(INITIAL_CAPACITY);

    traversal->stats = xmalloc(capacity*SIZEOF(*(traversal->stats)));
    traversal->patterns = xmalloc(capacity*SIZEOF(*(traversal->patterns)));
    traversal->link_targets = xmalloc(capacity*SIZEOF(*(traversal->link_targets)));
    traversal->paths = xmalloc(capacity*SIZEOF(*(traversal->paths)));

    traversal->paths_lens = xmalloc(capacity*SIZEOF(*(traversal->paths_lens)));
    traversal->link_targets_lens = xmalloc(capacity*SIZEOF(*(traversal->link_targets_lens)));
    traversal->patterns_lens = xmalloc(capacity*SIZEOF(*(traversal->patterns_lens)));
    traversal->nlinks = xmalloc(capacity*SIZEOF(*(traversal->nlinks)));
    traversal->row_ids = xmalloc(capacity*SIZEOF(*(traversal->row_ids)));

    traversal->ncapacity = capacity;
    traversal->nfiles = 0;
    return;
}

static void
traversal_clean(Traversal *traversal) {

    arena_reset(traversal->arena);
    hash_zero_fs_map(traversal->map);
    hash_zero_inode_map(traversal->inode_map);

    if (DEBUGGING) {
        dont_read(traversal->stats,
                  traversal->ncapacity*SIZEOF(*(traversal->stats)));
        dont_read(traversal->paths,
                  traversal->ncapacity*SIZEOF(*(traversal->paths)));
        dont_read(traversal->link_targets,
                  traversal->ncapacity*SIZEOF(*(traversal->link_targets)));
        dont_read(traversal->patterns,
                  traversal->ncapacity*SIZEOF(*(traversal->patterns)));
        dont_read(traversal->paths_lens,
                  traversal->ncapacity*SIZEOF(*(traversal->paths_lens)));
        dont_read(traversal->link_targets_lens,
                  traversal->ncapacity*SIZEOF(*(traversal->link_targets_lens)));
        dont_read(traversal->patterns_lens,
                  traversal->ncapacity*SIZEOF(*(traversal->patterns_lens)));
        dont_read(traversal->nlinks,
                  traversal->ncapacity*SIZEOF(*(traversal->nlinks)));
    }

    traversal->file_count = 0;
    traversal->nfiles = 0;

    return;
}

static void
traversal_free(Traversal *traversal) {
    int32 capacity = traversal->ncapacity;

    arena_destroy(traversal->arena);

    hash_destroy_fs_map(traversal->map);
    hash_destroy_inode_map(traversal->inode_map);

    free(traversal->stats, capacity*SIZEOF(*(traversal->stats)));
    free(traversal->patterns, capacity*SIZEOF(*(traversal->patterns)));
    free(traversal->link_targets, capacity*SIZEOF(*(traversal->link_targets)));
    free(traversal->paths, capacity*SIZEOF(*(traversal->paths)));

    free(traversal->paths_lens, capacity*SIZEOF(*(traversal->paths_lens)));
    free(traversal->link_targets_lens, capacity*SIZEOF(*(traversal->link_targets_lens)));
    free(traversal->patterns_lens, capacity*SIZEOF(*(traversal->patterns_lens)));
    free(traversal->nlinks, capacity*SIZEOF(*(traversal->nlinks)));
    free(traversal->row_ids, capacity*SIZEOF(*(traversal->row_ids)));

    return;
}

static int32
traversal_push(Traversal *traversal, struct stat *stat,
               char *path, int32 path_len,
               char *link_target, int32 link_target_len,
               char *matched_pattern, int32 matched_pattern_len,
               int32 nlinks) {
    struct stat stat_copy = *stat;
    int32 idx;

    if (traversal->nfiles >= traversal->ncapacity) {
        int32 old_capacity = traversal->ncapacity;
        traversal->ncapacity *= 2;

        traversal->stats = realloc(traversal->stats,
                                   old_capacity, traversal->ncapacity,
                                   SIZEOF(*(traversal->stats)));

        traversal->paths = realloc(traversal->paths,
                                   old_capacity, traversal->ncapacity,
                                   SIZEOF(*(traversal->paths)));
        traversal->link_targets = realloc(traversal->link_targets,
                                          old_capacity, traversal->ncapacity,
                                          SIZEOF(*(traversal->link_targets)));
        traversal->patterns = realloc(traversal->patterns,
                                      old_capacity, traversal->ncapacity,
                                      SIZEOF(*(traversal->patterns)));

        traversal->paths_lens = realloc(traversal->paths_lens,
                                        old_capacity, traversal->ncapacity,
                                        SIZEOF(*(traversal->paths_lens)));
        traversal->link_targets_lens = realloc(traversal->link_targets_lens,
                                               old_capacity, traversal->ncapacity,
                                               SIZEOF(*(traversal->link_targets_lens)));
        traversal->patterns_lens = realloc(traversal->patterns_lens,
                                           old_capacity, traversal->ncapacity,
                                           SIZEOF(*(traversal->patterns_lens)));
        traversal->nlinks = realloc(traversal->nlinks,
                                    old_capacity, traversal->ncapacity,
                                    SIZEOF(*(traversal->nlinks)));

        traversal->row_ids = realloc(traversal->row_ids,
                                     old_capacity, traversal->ncapacity,
                                     SIZEOF(*(traversal->row_ids)));

        for (int32 i = old_capacity; i < traversal->ncapacity; i += 1) {
            traversal->row_ids[i] = -1;
        }
    }

    idx = traversal->nfiles;
    traversal->nfiles += 1;

    memcpy64(&traversal->stats[idx], &stat_copy, SIZEOF(struct stat));

    traversal->paths[idx] = path;
    traversal->paths_lens[idx] = (int16)path_len;
    traversal->link_targets[idx] = link_target;
    traversal->link_targets_lens[idx] = (int16)link_target_len;
    traversal->patterns[idx] = matched_pattern;
    traversal->patterns_lens[idx] = (int16)matched_pattern_len;
    traversal->row_ids[idx] = -1;
    traversal->nlinks[idx] = (int16)nlinks;

    if (traversal->map) {
        hash_insert_fs_map(traversal->map, path, path_len, idx);
    }

    return idx;
}

static void
traversal_patch_nlinks(Traversal *traversal) {
    for (int32 i = 0; i < traversal->nfiles; i += 1) {
        if (S_ISREG(traversal->stats[i].st_mode)
                && (traversal->stats[i].st_nlink > 1)) {
            char inode[32];
            int32 inode_len;
            int32 *first_idx_ptr;

            inode_len = ITOA(inode, (long)traversal->stats[i].st_ino);
            if ((first_idx_ptr = hash_lookup_inode_map(traversal->inode_map, inode, inode_len))) {
                traversal->nlinks[i] = traversal->nlinks[*first_idx_ptr];
            } else {
                hash_insert_inode_map(traversal->inode_map, inode, inode_len, i);
            }
        }
    }
    return;
}

static void
traversal_unlink(Traversal *traversal, int32 idx) {
    if (S_ISREG(traversal->stats[idx].st_mode)
            && (traversal->stats[idx].st_nlink > 1)) {
        char inode[32];
        int32 inode_len;
        int32 *first_idx_ptr;

        inode_len = ITOA(inode, (long)traversal->stats[idx].st_ino);
        if ((first_idx_ptr = hash_lookup_inode_map(traversal->inode_map, inode, inode_len))) {
            traversal->nlinks[*first_idx_ptr] -= 1;
            if (traversal->nlinks[*first_idx_ptr] <= 0) {
                hash_remove_inode_map(traversal->inode_map, inode, inode_len);
            }
        }
    }
    return;
}

static void
free_task_list(TaskList *tasks) {
    if (tasks == NULL) {
        return;
    }

    for (int32 i = 0; i < tasks->count; i += 1) {
        Task *task = tasks->items[i];

        free(task->path, task->path_len + 1);
        free(task->link_target, task->link_target_len + 1);
        free(task, SIZEOF(*task));
    }

    free(tasks, STRUCT_ARRAY_SIZE(tasks, Task *, tasks->count));
    return;
}

static TaskList *
get_target_tasks(int8 side, char *clicked_path, enum Action clicked_action) {
    TaskList *tasks;
    int64 tasks_size;
    int32 count;

    tasks_size = STRUCT_ARRAY_SIZE(tasks, Task *, cecup.rows_len);
    count = 0;
    tasks = xmalloc(tasks_size);
    memset64(tasks, 0, tasks_size);

    for (int32 i = 0; i < cecup.rows_len; i += 1) {
        int32 row_id;
        char *filepath;
        int32 path_len;
        enum Action action;
        enum Action actions[2];
        enum Reason reason;
        char *link_target;
        int32 link_target_len;
        Task *task;

        row_id = i;
        if (!(cecup.rows_selected[row_id])) {
            continue;
        }

        item_get_actions_reasons(row_id, &actions[L], &actions[R], &reason);
        filepath = item_path_side(row_id, side);
        path_len = item_path_len_side(row_id, side);
        action = actions[side];

        if (filepath == NULL) {
            continue;
        }

        task = xmalloc(SIZEOF(*task));
        memset64(task, 0, SIZEOF(*task));

        task->path_len = path_len;
        task->path = xmalloc(path_len + 1);
        memcpy64(task->path, filepath, path_len + 1);

        link_target = item_link_target_side(row_id, side);
        link_target_len = item_link_target_len_side(row_id, side);

        if (link_target) {
            task->link_target_len = link_target_len;
            task->link_target = xmalloc(task->link_target_len + 1);
            memcpy64(task->link_target, link_target, task->link_target_len + 1);
        }

        task->action = action;
        task->side = side;

        tasks->items[count] = task;
        count += 1;
    }

    if ((count == 0) && clicked_path) {
        Task *task;
        Traversal *traversal;
        int32 *idx_ptr;

        count = 1;
        tasks = xrealloc(tasks, STRUCT_ARRAY_SIZE(tasks, Task *, count));
        tasks->count = count;

        task = xmalloc(SIZEOF(*task));
        memset64(task, 0, SIZEOF(*task));

        task->path_len = strlen32(clicked_path);
        task->path = xmalloc(task->path_len + 1);
        memcpy64(task->path, clicked_path, task->path_len + 1);

        task->action = clicked_action;
        task->side = side;

        traversal = &cecup.traversal[side];

        if ((idx_ptr = hash_lookup_fs_map(traversal->map, clicked_path, task->path_len))) {
            int32 idx;
            char *link_target;

            idx = *idx_ptr;
            if ((link_target = traversal->link_targets[idx])) {
                task->link_target_len = traversal->link_targets_lens[idx];
                task->link_target = xmalloc(task->link_target_len + 1);
                memcpy64(task->link_target, link_target, task->link_target_len + 1);
            }
        }

        tasks->items[0] = task;
    } else {
        tasks = xrealloc(tasks, STRUCT_ARRAY_SIZE(tasks, Task *, count));
        tasks->count = count;
    }

    return tasks;
}

static void
cecup_reset_dir(int32 side) {
    gtk_editable_set_text(GTK_EDITABLE(cecup.dir_entry[side]), "./");
    invalidate_preview();
    return;
}

static bool
cecup_get_dirs(void) {
    char full_src[PATH_MAX];
    char full_dst[PATH_MAX];
    char *tmp_src;
    char *tmp_dst;

    tmp_src = (char *)gtk_editable_get_text(GTK_EDITABLE(cecup.dir_entry[L]));
    tmp_dst = (char *)gtk_editable_get_text(GTK_EDITABLE(cecup.dir_entry[R]));

    save_config();

    if (strlen32(tmp_src) <= 0) {
        LOG_ERROR(_("Error: Invalid source directory.\n"));
        cecup_reset_dir(L);
        return false;
    }
    if (strlen32(tmp_dst) <= 0) {
        LOG_ERROR(_("Error: Invalid destination directory.\n"));
        cecup_reset_dir(R);
        return false;
    }

    if (realpath(tmp_src, full_src) == NULL) {
        LOG_ERROR(_("Error getting full path of %s: %s.\n"), tmp_src, strerror(errno));
        cecup_reset_dir(L);
        return false;
    }
    if (realpath(tmp_dst, full_dst) == NULL) {
        LOG_ERROR(_("Error getting full path of %s: %s.\n"), tmp_dst, strerror(errno));
        cecup_reset_dir(R);
        return false;
    }

    if (!strcmp(full_src, full_dst)) {
        LOG_ERROR(_("Error: source and backup are the same directory\n"));
        cecup_reset_dir(R);
        return false;
    }

    {
        int32 len_src = strlen32(full_src);
        int32 len_dst = strlen32(full_dst);

        if ((len_src > len_dst) && !memcmp64(full_src, full_dst, len_dst)) {
            if ((len_dst == 1 && full_dst[0] == '/') || (full_src[len_dst] == '/')) {
                LOG_ERROR(_("Error: source directory is contained in the destination directory\n"));
                cecup_reset_dir(L);
                return false;
            }
        }
        if ((len_dst > len_src) && !memcmp64(full_dst, full_src, len_src)) {
            if ((len_src == 1 && full_src[0] == '/') || (full_dst[len_src] == '/')) {
                LOG_ERROR(_("Error: destination directory is contained in the source directory\n"));
                cecup_reset_dir(R);
                return false;
            }
        }
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

    invalidate_preview();

    return true;
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

    config_bool_set(key, "Options", "check_fs",       cecup.check_fs_button);
    config_bool_set(key, "Options", "delete_after",   cecup.delete_after_button);
    config_bool_set(key, "Options", "delete_ignored", cecup.delete_ignored_button);

    out = g_key_file_to_data(key, &len, NULL);
    g_file_set_contents(cecup.config_path, out, (gssize)len, NULL);

    g_free(out);
    g_key_file_free(key);
    return;
}

static void
log_internal(char *file, int line, enum MsgType type, char *format, ...) {
    Message *message;
    char buffer[MAX_PATH_LENGTH*2];
    int32 n;
    int32 m;
    va_list va_args;
    char fileline[64];

    va_start(va_args, format);
    n = vsnprintf(buffer, SIZEOF(buffer), format, va_args);

    if ((n < 0) || (n >= SIZEOF(buffer))) {
        error("%s:%d: Error in vsnprintf(%s) (n = %lld)\n", file, line, format, (llong)n);
        fatal(EXIT_FAILURE);
    }
    va_end(va_args);

    message = xmalloc(SIZEOF(*message));
    memset64(message, 0, SIZEOF(*message));

    if (RELEASING) {
        m = SNPRINTF(fileline, "%s", "");
    } else {
        m = SNPRINTF(fileline, "%s:%d: ", file, line);
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
    Message *message;

    // NOTE: free(pointer, size)
    //       only uses size for verbose logging
    //       and pointer == NULL is ignored, so we don't need to check NULL here.
    if ((message = data)) {
        free(message->text, message->text_len + 1);
        if (message->dst_path != message->src_path) {
            // in this case,
            // dst_path might be NULL or a different allocation than src_path
            free(message->src_path, message->src_path_len + 1);
            free(message->dst_path, message->dst_path_len + 1);
        } else {
            // in this case,
            // either both are NULL (which free() simply ignores)
            // or only src_path is allocated.
            // dst_path might be the same pointer to the src_path allocation, or be NULL.
            free(message->src_path, message->src_path_len + 1);
        }

        free(message, SIZEOF(*message));
    }
    return;
}

static void
check_consistent_traversal_rows(Traversal *traversal, int32 *rows,
                                char *which_traversal, char *which_rows) {
    for (int32 idx = 0; idx < traversal->nfiles; idx += 1) {
        int32 row_id;
        char *path;
        int32 path_len;
        int32 *lookup_ptr;

        row_id = traversal->row_ids[idx];
        path = traversal->paths[idx];
        path_len = (int32)traversal->paths_lens[idx];
        lookup_ptr = hash_lookup_fs_map(traversal->map, path, path_len);

        if (row_id != -1) {
            if (traversal->nlinks[idx] > 1) {
                if (traversal->link_targets[idx] == NULL) {
                    error("Consistency error:"
                          " %s index %d (path %s) has nlinks > 1 but target is NULL.\n",
                          which_traversal, idx, path);
                    fatal(EXIT_FAILURE);
                }
            }

            if (traversal->nlinks[idx] <= 0) {
                error("Consistency error:"
                      "nlinks should equal or greater than 1, but %s->nlinks[%d] = %d\n",
                      which_traversal, idx, traversal->nlinks[idx]);
                fatal(EXIT_FAILURE);
            }
            if ((ulong)traversal->nlinks[idx] > traversal->stats[idx].st_nlink) {
                error("Consistency error:"
                      "%s index %d (path %s) has nlinks (%d) > st_nlink (%d).\n",
                      which_traversal, idx, path,
                      traversal->nlinks[idx], (int32)traversal->stats[idx].st_nlink);
                fatal(EXIT_FAILURE);
            }

            if (S_ISREG(traversal->stats[idx].st_mode)
                    && (traversal->stats[idx].st_nlink > 1)) {
                char inode[32];
                int32 inode_len;
                int32 *first_idx_ptr;

                inode_len = ITOA(inode, (long)traversal->stats[idx].st_ino);
                if ((first_idx_ptr = hash_lookup_inode_map(traversal->inode_map, inode, inode_len))) {
                    if (traversal->nlinks[idx] != traversal->nlinks[*first_idx_ptr]) {
                        error("Consistency error:"
                              "%s index %d (path %s) has mismatched nlinks %d != %d.\n",
                              which_traversal, idx, path,
                              traversal->nlinks[idx], traversal->nlinks[*first_idx_ptr]);
                        fatal(EXIT_FAILURE);
                    }
                }
            }

            if (row_id >= cecup.rows_len) {
                error("Consistency error: %s.row_ids[%d] points to invalid row %d.\n",
                      which_traversal, idx, row_id);
                fatal(EXIT_FAILURE);
            }
            if (rows[row_id] != idx) {
                error("Consistency error:"
                      " %s.row_ids["RED"%d"RESET"] -> row_id="YELLOW"%d"RESET","
                      " but %s["YELLOW"%d"RESET"] -> idx="RED"%d"RESET".\n",
                      which_traversal, idx, row_id,
                      which_rows, row_id, rows[row_id]);
                fatal(EXIT_FAILURE);
            }

            if (lookup_ptr == NULL) {
                error("Consistency error:"
                      " %s index %d (path %s) is mapped to row but missing in hash map.\n",
                      which_traversal, idx, path);
                fatal(EXIT_FAILURE);
            } else if (*lookup_ptr != idx) {
                error("Consistency error:"
                      " %s index %d (path %s) is mapped to row but mismatched in hash map.\n",
                      which_traversal, idx, path);
                fatal(EXIT_FAILURE);
            }
        } else {
            if (lookup_ptr) {
                if (*lookup_ptr == idx) {
                    error("Consistency error:"
                          " %s index %d (path %s) has no row but exists in hash.\n",
                          which_traversal, idx, path);
                    fatal(EXIT_FAILURE);
                }
            }
        }
    }

    for (uint32 bucket_idx = 0; bucket_idx < traversal->map->capacity; bucket_idx += 1) {
        Bucket_fs_map *bucket;
        int32 v;

        bucket = &traversal->map->array[bucket_idx];
        if ((int64)bucket->key > 0) {
            v = bucket->value;
            if (v < 0 || v >= traversal->nfiles) {
                error("Consistency error: %s hash map contains invalid index %d.\n",
                      which_traversal, v);
                fatal(EXIT_FAILURE);
            }
            if (traversal->row_ids[v] == -1) {
                error("Consistency error: %s hash map contains index %d with no row_id.\n",
                      which_traversal, v);
                fatal(EXIT_FAILURE);
            }
        }
    }

    return;
}

#define CHECK_CONSISTENT_TRAVERSAL_ROWS(TRAVERSAl, ROWS) \
    check_consistent_traversal_rows(TRAVERSAl, ROWS, #TRAVERSAl, #ROWS)

static void
check_consistent_state(void) {
    g_mutex_lock(&cecup.arena_mutex);

    error("Checking consistent state...\n");

    for (int32 row_id = 0; row_id < cecup.rows_len; row_id += 1) {
        int32 src_idx = cecup.rows[L][row_id];
        int32 dst_idx = cecup.rows[R][row_id];

        if ((src_idx == -1) && (dst_idx == -1)) {
            error("Consistency error: Row %d has no index for either side.\n", row_id);
            fatal(EXIT_FAILURE);
        }

        if (src_idx >= 0) {
            if (src_idx >= cecup.traversal[L].nfiles) {
                error("Consistency error: Row %d points to invalid src_idx %d.\n", row_id, src_idx);
                fatal(EXIT_FAILURE);
            }
            if (cecup.traversal[L].row_ids[src_idx] != row_id) {
                error("Consistency error:"
                      " rows_src["RED"%d"RESET"] -> src_idx="GREEN"%d"RESET","
                      " but src.row_ids["GREEN"%d"RESET"] -> row_id="RED"%d"RESET".\n",
                      row_id, src_idx,
                      src_idx, cecup.traversal[L].row_ids[src_idx]);
                fatal(EXIT_FAILURE);
            }
        }

        if (dst_idx >= 0) {
            if (dst_idx >= cecup.traversal[R].nfiles) {
                error("Consistency error: Row %d points to invalid dst_idx %d.\n", row_id, dst_idx);
                fatal(EXIT_FAILURE);
            }
            if (cecup.traversal[R].row_ids[dst_idx] != row_id) {
                error("Consistency error:"
                      " rows_dst["RED"%d"RESET"] -> dst_idx="GREEN"%d"RESET","
                      " but dst.row_ids["GREEN"%d"RESET"] -> row_id="RED"%d"RESET".\n",
                      row_id, dst_idx,
                      dst_idx, cecup.traversal[R].row_ids[dst_idx]);
                fatal(EXIT_FAILURE);
            }
        }
    }

    CHECK_CONSISTENT_TRAVERSAL_ROWS(&cecup.traversal[L], cecup.rows[L]);
    CHECK_CONSISTENT_TRAVERSAL_ROWS(&cecup.traversal[R], cecup.rows[R]);

    g_mutex_unlock(&cecup.arena_mutex);

    error("State is consistent...\n");
    return;
}

#if (0 == TESTING_aux) && TESTING
static inline void
aux_functions_sink(void) {
    (void)cecup_get_dirs;
    (void)get_target_tasks;
    (void)free_task_list;
    (void)free_message;
    (void)traversal_push;
    (void)protect_interface_from_user;
    (void)invalidate_preview;
    (void)traversal_free;
    (void)traversal_allocate;
    return;
}
#endif

#if TESTING_aux
#include "update.c"
#include "work.c"
#include "on.c"

int main(void) {
    (void)traversal_free;
    (void)traversal_allocate;
    return 0;
}
#endif

#endif /* AUX_C */
