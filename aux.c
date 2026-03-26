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
#include <glib/gmain.h>
#include <stdlib.h>

#include "util.c"
#include "i18n.h"
#include "cecup.h"
#include "tree_model.c"
#include "ignore_patterns.c"

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_aux 1
#elif !defined(TESTING_aux)
#define TESTING_aux 0
#endif

#define UI_INTERVAL_MS 100

static void refresh_ui_list(enum RefreshType refresh_type, char *path_to_focus);

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
    gtk_widget_set_sensitive(cecup.src_entry, !state);
    gtk_widget_set_sensitive(cecup.dst_entry, !state);
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
                               data->ncapacity*SIZEOF(int32));
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
update_row_remove(Message *message) {
    char *pattern = message->src_path;
    int32 pattern_len = message->path_len;
    int32 deleted_side = message->side;

    for (int32 i = 0; i < cecup.rows_len;) {
        CecupRow *row = cecup.rows[i];
        char *path_test = row_path_get(row);
        bool match = false;

        if (pattern[pattern_len - 1] == '/') {
            if (BEGINS_WITH(path_test, pattern, pattern_len)) {
                match = true;
            }
        } else {
            if (row->path_len == pattern_len) {
                if (!memcmp64(path_test, pattern, pattern_len)) {
                    match = true;
                }
            }
        }

        if (match) {
            bool remove_entirely = false;
            Traversal *traversal;
            int32 *idx_ptr;

            if (deleted_side == L) {
                row->src_path = NULL;
                traversal = &cecup.traversal_src;
            } else {
                row->dst_path = NULL;
                traversal = &cecup.traversal_dst;
            }

            if (row->src_path == NULL) {
                if (row->dst_path == NULL) {
                    remove_entirely = true;
                }
            }

            if (!remove_entirely) {
                if (deleted_side == L) {
                    row->src_action = ACTION_IGNORE;
                    row->dst_action = ACTION_DELETE;
                    row->reason = REASON_MISSING;
                } else {
                    if (row->reason & REASON_HARDLINK) {
                        row->src_action = ACTION_HARDLINK;
                        row->dst_action = ACTION_HARDLINK;
                    } else if (row->reason & REASON_SYMLINK) {
                        row->src_action = ACTION_SYMLINK;
                        row->dst_action = ACTION_SYMLINK;
                    } else {
                        row->src_action = ACTION_NEW;
                        row->dst_action = ACTION_NEW;
                    }
                    row->reason
                        &= ~(REASON_EQUAL
                                | REASON_SIZE | REASON_MTIME | REASON_CTIME
                                | REASON_OWNER | REASON_GROUP | REASON_PERM);
                    row->reason |= REASON_NEW;
                }
            }

            if (traversal->map) {
                if ((idx_ptr = hash_lookup_fs_map(traversal->map,
                                                  path_test, row->path_len))) {
                    int32 idx = *idx_ptr;

                    if (traversal->inode_map) {
                        if (S_ISREG(traversal->stats[idx].st_mode)) {
                            if (traversal->stats[idx].st_nlink > 1) {
                                char inode_str[64];
                                int32 n;
                                n = itoa2(inode_str,
                                          (long)traversal->stats[idx].st_ino);
                                hash_remove_inode_map(traversal->inode_map,
                                                      inode_str, n);
                            }
                        }
                    }

                    hash_remove_fs_map(traversal->map,
                                       path_test, row->path_len);
                    memset64(&traversal->stats[idx], 0, SIZEOF(struct stat));
                }
            }

            if (remove_entirely) {
                LOG(_("Removing %s entirely from list...\n"), path_test);

                for (int32 k = 0; k < cecup.rows_visible_len; k += 1) {
                    if (cecup.rows_visible[k] == row) {
                        for (int32 j = k; j < (cecup.rows_visible_len - 1); j += 1) {
                            cecup.rows_visible[j] = cecup.rows_visible[j + 1];
                        }
                        cecup.rows_visible_len -= 1;

                        cecup_list_model_row_removed(CECUP_LIST_MODEL(cecup.store), k);
                        break;
                    }
                }

                for (int32 j = i; j < (cecup.rows_len - 1); j += 1) {
                    cecup.rows[j] = cecup.rows[j + 1];
                }
                cecup.rows_len -= 1;
            } else {
                LOG(_("Updated %s state (missing on side %d)\n"), path_test, deleted_side);
                for (int32 k = 0; k < cecup.rows_visible_len; k += 1) {
                    if (cecup.rows_visible[k] == row) {
                        cecup_list_model_row_changed(CECUP_LIST_MODEL(cecup.store), k);
                        break;
                    }
                }
                i += 1;
            }
        } else {
            i += 1;
        }
    }
    return;
}

static void
update_row_transfer(Message *message) {
    char *pattern;
    int32 pattern_len;

    pattern = message->src_path;
    pattern_len = message->path_len;

    for (int32 i = 0; i < cecup.rows_len; i += 1) {
        CecupRow *row = cecup.rows[i];
        char *path_test = row_path_get(row);
        bool show_equal;
        char full_path[MAX_PATH_LENGTH];
        int32 *idx_ptr;

        if (row->path_len != pattern_len) {
            continue;
        }
        if (memcmp64(path_test, pattern, pattern_len)) {
            continue;
        }

        row->src_action = ACTION_EQUAL;
        row->dst_action = ACTION_EQUAL;
        row->reason = REASON_EQUAL;

        if (row->dst_path == NULL) {
            row->dst_path = row->src_path;
        } else if (row->src_path == NULL) {
            row->src_path = row->dst_path;
        }

        row->dst_size_raw = row->src_size_raw;
        memcpy64(row->dst_size_text, row->src_size_text,
                 SIZEOF(row->dst_size_text));

        row->dst_mtime_raw = row->src_mtime_raw;
        memcpy64(row->dst_mtime_text, row->src_mtime_text,
                 SIZEOF(row->dst_mtime_text));

        SNPRINTF(full_path, "%s/%s", cecup.dst_base, path_test);

        if ((idx_ptr
             = hash_lookup_fs_map(cecup.traversal_dst.map,
                                  path_test, pattern_len))) {
            int32 idx = *idx_ptr;
            struct stat *stat = &cecup.traversal_dst.stats[idx];

            if (lstat(full_path, stat) < 0) {
                LOG_ERROR("Error in lstat(%s): %s.\n",
                          full_path, strerror(errno));
            } else if (S_ISLNK(stat->st_mode)) {
                char target[MAX_PATH_LENGTH];
                int64 target_len;

                if ((target_len = readlink(full_path, target, SIZEOF(target))) > 0) {
                    target[target_len] = '\0';
                    if (cecup.traversal_dst.link_targets[idx]) {
                        XFREE(cecup.traversal_dst.link_targets[idx],
                              cecup.traversal_dst.link_targets_lens[idx] + 1);
                    }
                    cecup.traversal_dst.link_targets[idx] = xmemdup(target, target_len + 1);
                    cecup.traversal_dst.link_targets_lens[idx] = (int16)target_len;
                }
            } else if (S_ISREG(stat->st_mode) && (stat->st_nlink > 1)) {
                char inode_str[64];
                int32 n;
                int32 *first_idx_ptr;

                n = itoa2(inode_str, (long)stat->st_ino);
                first_idx_ptr = hash_lookup_inode_map(cecup.traversal_dst.inode_map,
                                                      inode_str, n);
                if (first_idx_ptr) {
                    int32 first_idx;

                    first_idx = *first_idx_ptr;
                    if (first_idx != idx) {
                        cecup.traversal_dst.link_targets[idx]
                            = cecup.traversal_dst.paths[first_idx];
                        cecup.traversal_dst.link_targets_lens[idx]
                            = cecup.traversal_dst.paths_lens[first_idx];
                    }
                } else {
                    hash_insert_inode_map(cecup.traversal_dst.inode_map,
                                          inode_str, n, idx);
                }
            }
        } else {
            struct stat stat;
            char *path_copy;

            if (lstat(full_path, &stat) < 0) {
                LOG_ERROR("Error in lstat(%s): %s.\n",
                          full_path, strerror(errno));
            } else {
                char *link_target = NULL;
                int32 link_target_len = 0;

                if (S_ISLNK(stat.st_mode)) {
                    char target[MAX_PATH_LENGTH];
                    int64 target_len;

                    if ((target_len = readlink(full_path, target, SIZEOF(target))) > 0) {
                        target[target_len] = '\0';
                        link_target = xmemdup(target, target_len + 1);
                        link_target_len = (int32)target_len;
                    }
                } else if (S_ISREG(stat.st_mode) && (stat.st_nlink > 1)) {
                    char inode_str[64];
                    int32 n;
                    int32 *first_idx_ptr;

                    n = itoa2(inode_str, (long)stat.st_ino);
                    first_idx_ptr = hash_lookup_inode_map(cecup.traversal_dst.inode_map,
                                                          inode_str, n);
                    if (first_idx_ptr) {
                        int32 first_idx;

                        first_idx = *first_idx_ptr;
                        link_target = cecup.traversal_dst.paths[first_idx];
                        link_target_len = cecup.traversal_dst.paths_lens[first_idx];
                    } else {
                        hash_insert_inode_map(cecup.traversal_dst.inode_map,
                                              inode_str, n, cecup.traversal_dst.nfiles);
                    }
                }

                path_copy = xmemdup(path_test, pattern_len + 1);
                traversal_push(&cecup.traversal_dst,
                               path_copy, pattern_len,
                               &stat,
                               link_target, link_target_len,
                               NULL, 0);
            }
        }

        show_equal = gtk_toggle_button_get_active(
            GTK_TOGGLE_BUTTON(cecup.filter_equal));

        if (show_equal) {
            for (int32 k = 0; k < cecup.rows_visible_len; k += 1) {
                if (cecup.rows_visible[k] == row) {
                    cecup_list_model_row_changed(
                        CECUP_LIST_MODEL(cecup.store), k);
                    break;
                }
            }
        } else {
            for (int32 k = 0; k < cecup.rows_visible_len; k += 1) {
                if (cecup.rows_visible[k] == row) {
                    for (int32 j = k; j < (cecup.rows_visible_len - 1); j += 1) {
                        cecup.rows_visible[j] = cecup.rows_visible[j + 1];
                    }
                    cecup.rows_visible_len -= 1;

                    cecup_list_model_row_removed(CECUP_LIST_MODEL(cecup.store),
                                                 k);
                    break;
                }
            }
        }
        break;
    }
    return;
}

static void
update_row_rename(Message *message) {
    char *old_path;
    char *new_path;
    int32 old_path_len;
    int32 new_path_len;
    int32 side;
    Traversal *traversal;

    old_path = message->old_path;
    new_path = message->new_path;
    old_path_len = message->old_path_len;
    new_path_len = message->new_path_len;
    side = message->side;

    if (side == L) {
        traversal = &cecup.traversal_src;
    } else {
        traversal = &cecup.traversal_dst;
    }

    for (int32 i = 0; i < traversal->nfiles; i += 1) {
        char *path = traversal->paths[i];
        int32 path_len = traversal->paths_lens[i];
        bool match = false;

        if (old_path[old_path_len - 1] == '/') {
            if (BEGINS_WITH(path, old_path, old_path_len)) {
                match = true;
            }
        } else {
            if (path_len == old_path_len) {
                if (!memcmp64(path, old_path, old_path_len)) {
                    match = true;
                }
            }
        }

        if (match) {
            char *suffix;
            int32 suffix_len;
            char *updated_path;
            int32 updated_path_len;

            suffix = path + old_path_len;
            suffix_len = path_len - old_path_len;
            updated_path_len = new_path_len + suffix_len;
            updated_path = xmalloc(updated_path_len + 1);

            memcpy64(updated_path, new_path, new_path_len);
            memcpy64(updated_path + new_path_len, suffix, suffix_len + 1);

            if (traversal->map) {
                hash_remove_fs_map(traversal->map, path, path_len);
                hash_insert_fs_map(traversal->map, updated_path, updated_path_len, i);
            }

            XFREE(traversal->paths[i], path_len + 1);
            traversal->paths[i] = updated_path;
            traversal->paths_lens[i] = (int16)updated_path_len;
        }
    }

    for (int32 i = 0; i < cecup.rows_len;) {
        CecupRow *row = cecup.rows[i];
        bool match = false;
        char *my_path;

        if (side == L) {
            my_path = row->src_path;
        } else {
            my_path = row->dst_path;
        }

        if (my_path) {
            if (old_path[old_path_len - 1] == '/') {
                if (BEGINS_WITH(my_path, old_path, old_path_len)) {
                    match = true;
                }
            } else {
                if (row->path_len == old_path_len) {
                    if (!memcmp64(my_path, old_path, old_path_len)) {
                        match = true;
                    }
                }
            }
        }

        if (match) {
            char *suffix;
            int32 suffix_len;
            char *updated_path;
            int32 updated_path_len;
            bool remove_entirely;
            CecupRow *target_row;
            char *arena_path;

            suffix = my_path + old_path_len;
            suffix_len = row->path_len - old_path_len;
            updated_path_len = new_path_len + suffix_len;
            updated_path = xmalloc(updated_path_len + 1);

            memcpy64(updated_path, new_path, new_path_len);
            memcpy64(updated_path + new_path_len, suffix, suffix_len + 1);

            target_row = NULL;
            for (int32 j = 0; j < cecup.rows_len; j += 1) {
                CecupRow *r = cecup.rows[j];
                char *r_path;

                if ((r->src_path == NULL) && (r->dst_path == NULL)) {
                    continue;
                }

                r_path = row_path_get(r);
                if ((r->path_len == updated_path_len) && (!memcmp64(r_path, updated_path, updated_path_len))) {
                    target_row = r;
                    break;
                }
            }

            if (target_row == NULL) {
                g_mutex_lock(&cecup.arena_mutex);
                target_row = xarena_push(cecup.arena, SIZEOF(*target_row));
                memset64(target_row, 0, SIZEOF(*target_row));

                target_row->path_len = updated_path_len;
                if (cecup.rows_len >= cecup.rows_capacity) {
                    if (cecup.rows_capacity == 0) {
                        cecup.rows_capacity = 1024;
                    } else {
                        cecup.rows_capacity *= 2;
                    }
                    cecup.rows = xrealloc(cecup.rows, cecup.rows_capacity * SIZEOF(CecupRow *));
                    cecup.rows_visible = xrealloc(cecup.rows_visible, cecup.rows_capacity * SIZEOF(CecupRow *));
                }
                cecup.rows[cecup.rows_len] = target_row;
                cecup.rows_len += 1;
                g_mutex_unlock(&cecup.arena_mutex);
            }

            g_mutex_lock(&cecup.arena_mutex);
            arena_path = xarena_push(cecup.arena, updated_path_len + 1);
            memcpy64(arena_path, updated_path, updated_path_len + 1);
            g_mutex_unlock(&cecup.arena_mutex);

            if (side == L) {
                target_row->src_path = arena_path;
                target_row->src_size_raw = row->src_size_raw;
                target_row->src_mtime_raw = row->src_mtime_raw;
                memcpy64(target_row->src_size_text, row->src_size_text, SIZEOF(target_row->src_size_text));
                memcpy64(target_row->src_mtime_text, row->src_mtime_text, SIZEOF(target_row->src_mtime_text));
                row->src_path = NULL;
            } else {
                target_row->dst_path = arena_path;
                target_row->dst_size_raw = row->dst_size_raw;
                target_row->dst_mtime_raw = row->dst_mtime_raw;
                memcpy64(target_row->dst_size_text, row->dst_size_text, SIZEOF(target_row->dst_size_text));
                memcpy64(target_row->dst_mtime_text, row->dst_mtime_text, SIZEOF(target_row->dst_mtime_text));
                row->dst_path = NULL;
            }

            XFREE(updated_path, updated_path_len + 1);

            remove_entirely = false;
            if ((row->src_path == NULL) && (row->dst_path == NULL)) {
                remove_entirely = true;
            } else {
                if (side == L) {
                    row->src_action = ACTION_IGNORE;
                    row->dst_action = ACTION_DELETE;
                    row->reason = REASON_MISSING;
                } else {
                    if (row->reason & REASON_HARDLINK) {
                        row->src_action = ACTION_HARDLINK;
                        row->dst_action = ACTION_HARDLINK;
                    } else if (row->reason & REASON_SYMLINK) {
                        row->src_action = ACTION_SYMLINK;
                        row->dst_action = ACTION_SYMLINK;
                    } else {
                        row->src_action = ACTION_NEW;
                        row->dst_action = ACTION_NEW;
                    }
                    row->reason &= ~(REASON_EQUAL | REASON_SIZE | REASON_MTIME | REASON_CTIME | REASON_OWNER | REASON_GROUP | REASON_PERM);
                    row->reason |= REASON_NEW;
                }

                for (int32 k = 0; k < cecup.rows_visible_len; k += 1) {
                    if (cecup.rows_visible[k] == row) {
                        cecup_list_model_row_changed(CECUP_LIST_MODEL(cecup.store), k);
                        break;
                    }
                }
            }

            target_row->reason = 0;
            if ((target_row->src_path != NULL) && (target_row->dst_path != NULL)) {
                bool attr_diff = false;

                if (target_row->src_size_raw != target_row->dst_size_raw) {
                    target_row->reason |= REASON_SIZE;
                    attr_diff = true;
                }
                if (target_row->src_mtime_raw != target_row->dst_mtime_raw) {
                    target_row->reason |= REASON_MTIME;
                    attr_diff = true;
                }

                if (!attr_diff) {
                    target_row->src_action = ACTION_EQUAL;
                    target_row->dst_action = ACTION_EQUAL;
                    target_row->reason |= REASON_EQUAL;
                } else {
                    target_row->src_action = ACTION_UPDATE;
                    target_row->dst_action = ACTION_UPDATE;
                }
            } else if (target_row->src_path != NULL) {
                target_row->src_action = ACTION_NEW;
                target_row->dst_action = ACTION_NEW;
                target_row->reason |= REASON_NEW;
            } else {
                target_row->src_action = ACTION_IGNORE;
                target_row->dst_action = ACTION_DELETE;
                target_row->reason |= REASON_MISSING;
            }

            if (remove_entirely) {
                for (int32 k = 0; k < cecup.rows_visible_len; k += 1) {
                    if (cecup.rows_visible[k] == row) {
                        for (int32 p = k; p < (cecup.rows_visible_len - 1); p += 1) {
                            cecup.rows_visible[p] = cecup.rows_visible[p + 1];
                        }
                        cecup.rows_visible_len -= 1;
                        cecup_list_model_row_removed(CECUP_LIST_MODEL(cecup.store), k);
                        break;
                    }
                }
                for (int32 p = i; p < (cecup.rows_len - 1); p += 1) {
                    cecup.rows[p] = cecup.rows[p + 1];
                }
                cecup.rows_len -= 1;
            } else {
                i += 1;
            }
        } else {
            i += 1;
        }
    }

    refresh_ui_list(REFRESH_FINAL, NULL);
    return;
}

static void
free_task_list(TaskList *tasks) {
    if (tasks == NULL) {
        return;
    }

    for (int32 i = 0; i < tasks->count; i += 1) {
        Task *task = tasks->items[i];

        XFREE(task->link_target, task->link_target_len + 1);
        XFREE(task->message, task->message_len);
        XFREE(task, sizeof(*task));
    }

    XFREE(tasks, sizeof(*tasks));
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
refresh_ui_list(enum RefreshType refresh_type, char *path_to_focus) {
    int32 count_new = 0;
    int32 count_hard = 0;
    int32 count_update = 0;
    int32 count_equal = 0;
    int32 count_delete = 0;
    int32 count_ignore = 0;
    int64 count_selected = 0;
    int64 total_size_bytes = 0;
    int64 current_store_count;
    char pretty_size[16];
    char stats_text[256];
    char button_label[64];
    bool show_new;
    bool show_link;
    bool show_update;
    bool show_equal;
    bool show_delete;
    bool show_ignore;

    struct timespec t0;
    struct timespec t1;

    clock_gettime(CLOCK_MONOTONIC_RAW, &t0);

    current_store_count = (int64)g_list_model_get_n_items(cecup.store);

    show_new
        = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_new));
    show_link
        = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_hard));
    show_update
        = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_update));
    show_equal
        = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_equal));
    show_delete
        = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_delete));
    show_ignore
        = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_ignore));

    cecup.rows_visible_len = 0;
    for (int32 i = 0; i < cecup.rows_len; i += 1) {
        CecupRow *row = cecup.rows[i];
        bool visible = false;

        if (row->selected) {
            count_selected += 1;
        }

        switch (row->src_action) {
        case ACTION_NEW:
            visible = show_new;
            count_new += 1;
            total_size_bytes += row->src_size_raw;
            break;
        case ACTION_HARDLINK:
        case ACTION_SYMLINK:
            visible = show_link;
            count_hard += 1;
            total_size_bytes += row->src_size_raw;
            break;
        case ACTION_UPDATE:
            visible = show_update;
            count_update += 1;
            total_size_bytes += row->src_size_raw;
            break;
        case ACTION_EQUAL:
            visible = show_equal;
            count_equal += 1;
            break;
        case ACTION_DELETED:
            visible = show_delete;
            count_delete += 1;
            break;
        case ACTION_IGNORE:
            if (row->dst_action == ACTION_DELETE) {
                visible = show_delete;
                count_delete += 1;
            } else {
                visible = show_ignore;
                count_ignore += 1;
            }
            break;
        case ACTION_DELETE:
        case ACTION_LAST:
        default:
            error("Invalid row->src_action: %u\n", row->src_action);
            fatal(EXIT_FAILURE);
        }

        if (visible && cecup.search_query && (cecup.search_query[0] != '\0')) {
            char *path = row_path_get(row);

            if (strcasestr(path, cecup.search_query) == NULL) {
                visible = false;
            }
        }

        if (visible) {
            cecup.rows_visible[cecup.rows_visible_len] = row;
            cecup.rows_visible_len += 1;
        }
    }

    SNPRINTF(button_label, "%s %d", EMOJI_NEW, count_new);
    gtk_button_set_label(GTK_BUTTON(cecup.filter_new), button_label);

    SNPRINTF(button_label, "%s/%s %d", EMOJI_LINK, EMOJI_SYMLINK, count_hard);
    gtk_button_set_label(GTK_BUTTON(cecup.filter_hard), button_label);

    SNPRINTF(button_label, "%s %d", EMOJI_UPDATE, count_update);
    gtk_button_set_label(GTK_BUTTON(cecup.filter_update), button_label);

    SNPRINTF(button_label, "%s %d", EMOJI_EQUAL, count_equal);
    gtk_button_set_label(GTK_BUTTON(cecup.filter_equal), button_label);

    SNPRINTF(button_label, "%s %d", EMOJI_DELETE, count_delete);
    gtk_button_set_label(GTK_BUTTON(cecup.filter_delete), button_label);

    SNPRINTF(button_label, "%s %d", EMOJI_IGNORE, count_ignore);
    gtk_button_set_label(GTK_BUTTON(cecup.filter_ignore), button_label);

    bytes_pretty(pretty_size, total_size_bytes);
    SNPRINTF(stats_text, _("Selected files: %lld\nTotal Transfer Size: 📦 %s"),
             (llong)count_selected, pretty_size);
    gtk_label_set_text(GTK_LABEL(cecup.stats_label), stats_text);

    if (cecup.rows_visible_len > 0) {
        qsort64(cecup.rows_visible, cecup.rows_visible_len, SIZEOF(CecupRow *),
                cecup_row_compare);
    }

    cecup_list_model_update(CECUP_LIST_MODEL(cecup.store),
                            (int32)current_store_count,
                            cecup.rows_visible_len);

    if ((refresh_type & REFRESH_FINAL) && path_to_focus) {
        for (int32 i = 0; i < cecup.rows_visible_len; i += 1) {
            char *row_path = row_path_get(cecup.rows_visible[i]);
            char row_full_rel[MAX_PATH_LENGTH];
            int32 row_rel_len;

            row_rel_len = SNPRINTF(row_full_rel, "/%s", row_path);
            normalize(row_full_rel, &row_rel_len);

            if (!strcmp(row_full_rel, path_to_focus)) {
                GtkSelectionModel *sel_l;
                GtkSelectionModel *sel_r;
                GtkColumnView *view_left = GTK_COLUMN_VIEW(cecup.tree[L]);
                GtkColumnView *view_right = GTK_COLUMN_VIEW(cecup.tree[R]);

                sel_l = gtk_column_view_get_model(view_left);
                sel_r = gtk_column_view_get_model(view_right);

                gtk_selection_model_select_item(sel_l, (uint32)i, TRUE);
                gtk_selection_model_select_item(sel_r, (uint32)i, TRUE);
                break;
            }
        }
    }

    clock_gettime(CLOCK_MONOTONIC_RAW, &t1);
    PRINT_TIMINGS(cecup.rows_visible_len, t0, t1);
    return;
}

static void
update_row_ignore(Message *message) {
    bool delete_excluded;

    (void)message;
    delete_excluded = gtk_check_button_get_active(GTK_CHECK_BUTTON(cecup.delete_excluded));

    invalidate_preview();
    ignore_patterns_load();

    for (int32 i = 0; i < cecup.rows_len; i += 1) {
        CecupRow *row = cecup.rows[i];
        char *path = row_path_get(row);
        bool is_dir = false;
        IgnorePattern *match;

        if (row->path_len > 0 && path[row->path_len - 1] == '/') {
            is_dir = true;
        }

        if ((match = ignore_patterns_match(path, row->path_len, is_dir,
                                           cecup.ignore_patterns,
                                           cecup.ignore_count))) {
            row->src_action = ACTION_IGNORE;
            row->reason |= REASON_IGNORED;

            g_mutex_lock(&cecup.arena_mutex);
            row->ignore_pattern_len = match->len;
            row->ignore_pattern = xarena_push(cecup.arena, match->len + 1);
            memcpy64(row->ignore_pattern, match->str, match->len + 1);
            g_mutex_unlock(&cecup.arena_mutex);

            if (row->dst_path != NULL) {
                if (delete_excluded) {
                    row->dst_action = ACTION_DELETE;
                } else {
                    row->dst_action = ACTION_IGNORE;
                }
            } else {
                row->dst_action = ACTION_IGNORE;
            }

            for (int32 k = 0; k < cecup.rows_visible_len; k += 1) {
                if (cecup.rows_visible[k] == row) {
                    cecup_list_model_row_changed(CECUP_LIST_MODEL(cecup.store), k);
                    break;
                }
            }
        }
    }

    refresh_ui_list(REFRESH_FILTER_CHANGED, NULL);
    return;
}

static void cecup_list_model_row_removed(CecupListModel *self, int32 index);
static void cecup_list_model_row_changed(CecupListModel *self, int32 index);

static gboolean
update_ui_handler(void *data) {
    GtkTextIter end;
    GtkTextIter start_line;
    GtkTextTagTable *table;
    int32 current_store_count;
    bool is_cr;
    bool buffer_ends_in_lf;
    Message *message = data;

    switch (message->type) {
    case DATA_TYPE_LOG:
    case DATA_TYPE_LOG_CMD:
    case DATA_TYPE_LOG_ERROR:
        is_cr = false;

        if (message->text_len > 0) {
            if (message->text[message->text_len - 1] == '\r') {
                is_cr = true;
                message->text[message->text_len - 1] = '\0';
            }
        }

        gtk_text_buffer_get_end_iter(cecup.log_buffer, &end);

        buffer_ends_in_lf = true;
        if (gtk_text_iter_get_offset(&end) > 0) {
            GtkTextIter last_char = end;
            gtk_text_iter_backward_char(&last_char);
            if (gtk_text_iter_get_char(&last_char) != '\n') {
                buffer_ends_in_lf = false;
            }
        }

        if (is_cr || !buffer_ends_in_lf) {
            start_line = end;
            gtk_text_iter_set_line_offset(&start_line, 0);
            gtk_text_buffer_delete(cecup.log_buffer, &start_line, &end);
            gtk_text_buffer_get_end_iter(cecup.log_buffer, &end);
        }

        table = gtk_text_buffer_get_tag_table(cecup.log_buffer);

        if (message->type == DATA_TYPE_LOG_ERROR) {
            if (gtk_text_tag_table_lookup(table, "err_red") == NULL) {
                gtk_text_buffer_create_tag(cecup.log_buffer, "err_red",
                                           "foreground", "red", NULL);
            }
            gtk_text_buffer_insert_with_tags_by_name(
                cecup.log_buffer, &end, message->text, -1, "err_red", NULL);
        } else if (message->type == DATA_TYPE_LOG_CMD) {
            if (gtk_text_tag_table_lookup(table, "err_blue") == NULL) {
                gtk_text_buffer_create_tag(cecup.log_buffer, "err_blue",
                                           "foreground", "blue", NULL);
            }
            gtk_text_buffer_insert_with_tags_by_name(
                cecup.log_buffer, &end, message->text, -1, "err_blue", NULL);
        } else {
            gtk_text_buffer_insert(cecup.log_buffer,
                                   &end, message->text, -1);
        }

        gtk_text_view_scroll_to_mark(
            GTK_TEXT_VIEW(cecup.log_view),
            gtk_text_buffer_get_insert(cecup.log_buffer), 0.0, FALSE, 0.0, 0.0);
        break;
    case DATA_TYPE_PROGRESS_PREVIEW:
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(cecup.progress_preview),
                                      message->fraction);
        break;
    case DATA_TYPE_ROW_REMOVE:
        update_row_remove(message);
        break;
    case DATA_TYPE_ROW_RENAME:
        update_row_rename(message);
        break;
    case DATA_TYPE_ROW_TRANSFER:
        update_row_transfer(message);
        break;
    case DATA_TYPE_IGNORE_PATTERN:
        update_row_ignore(message);
        break;
    case DATA_TYPE_ENABLE_BUTTONS:
        if (cecup.refresh_id != 0) {
            g_source_remove(cecup.refresh_id);
            cecup.refresh_id = 0;
        }
        refresh_ui_list(REFRESH_FINAL, message->path_to_focus);

        if (message->preview_clean) {
            cecup.preview_dirty = false;
        } else {
            cecup.preview_dirty = true;
        }

        protect_interface_from_user(false);
        break;
    case DATA_TYPE_CLEAR_TREES:
        if (cecup.refresh_id != 0) {
            g_source_remove(cecup.refresh_id);
            cecup.refresh_id = 0;
        }
        g_mutex_lock(&cecup.arena_mutex);

        current_store_count = (int32)g_list_model_get_n_items(cecup.store);

        arena_reset(cecup.arena);
        cecup.rows_len = 0;
        cecup.rows_visible_len = 0;

        cecup_list_model_update(CECUP_LIST_MODEL(cecup.store),
                                current_store_count, 0);

        g_mutex_unlock(&cecup.arena_mutex);

        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(cecup.progress_preview),
                                      0.0);
        break;
    case DATA_TYPE_ADD_ROW:
    case DATA_TYPE_LAST:
    default:
        LOG("Ignoring %s.\n", DATA_TYPE_str(message->type));
        break;
    }

    XFREE(message->text, message->text_len + 1);
    XFREE(message->path_to_focus, message->focus_len + 1);

    if (message->src_path) {
        XFREE(message->src_path, message->path_len + 1);
    } else if (message->dst_path) {
        XFREE(message->dst_path, message->path_len + 1);
    }

    XFREE(message->old_path, message->old_path_len + 1);
    XFREE(message->new_path, message->new_path_len + 1);

    XFREE(message->link_target, message->link_target_len + 1);
    XFREE(message->ignore_pattern, message->ignore_pattern_len + 1);
    XFREE(message, sizeof(*message));

    return G_SOURCE_REMOVE;
}

static void
cecup_reset_dir(int32 side) {
    if (side == L) {
        gtk_editable_set_text(GTK_EDITABLE(cecup.src_entry), "./");
    } else {
        gtk_editable_set_text(GTK_EDITABLE(cecup.dst_entry), "./");
    }
    return;
}

static void
cecup_get_dirs(void) {
    char full_src[MAX_PATH_LENGTH];
    char full_dst[MAX_PATH_LENGTH];
    char *tmp_src;
    char *tmp_dst;

    tmp_src = (char *)gtk_editable_get_text(GTK_EDITABLE(cecup.src_entry));
    tmp_dst = (char *)gtk_editable_get_text(GTK_EDITABLE(cecup.dst_entry));

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
        XFREE(cecup.src_base, cecup.src_base_len + 1);
    }
    if (cecup.dst_base) {
        XFREE(cecup.dst_base, cecup.dst_base_len + 1);
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

    g_signal_handler_block(cecup.src_entry, cecup.src_entry_id);
    g_signal_handler_block(cecup.dst_entry, cecup.dst_entry_id);

    gtk_editable_set_text(GTK_EDITABLE(cecup.src_entry), cecup.src_base);
    gtk_editable_set_text(GTK_EDITABLE(cecup.dst_entry), cecup.dst_base);

    g_signal_handler_unblock(cecup.src_entry, cecup.src_entry_id);
    g_signal_handler_unblock(cecup.dst_entry, cecup.dst_entry_id);

    return;
}

static void
save_config(void) {
    GKeyFile *key;
    char *out;
    gsize len;

    key = g_key_file_new();
    g_key_file_set_string(key, "Paths", "src",
                          gtk_editable_get_text(GTK_EDITABLE(cecup.src_entry)));
    g_key_file_set_string(key, "Paths", "dst",
                          gtk_editable_get_text(GTK_EDITABLE(cecup.dst_entry)));
    g_key_file_set_string(key, "Tools", "diff",
                          gtk_editable_get_text(GTK_EDITABLE(cecup.diff_entry)));
    g_key_file_set_string(key, "Tools", "term",
                          gtk_editable_get_text(GTK_EDITABLE(cecup.term_entry)));
    g_key_file_set_boolean(
        key, "Filters", "new",
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_new)));
    g_key_file_set_boolean(
        key, "Filters", "hard",
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_hard)));
    g_key_file_set_boolean(
        key, "Filters", "update",
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_update)));
    g_key_file_set_boolean(
        key, "Filters", "equal",
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_equal)));
    g_key_file_set_boolean(
        key, "Filters", "delete",
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_delete)));
    g_key_file_set_boolean(
        key, "Filters", "ignore",
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_ignore)));
    g_key_file_set_boolean(
        key, "Options", "check_fs",
        gtk_check_button_get_active(GTK_CHECK_BUTTON(cecup.check_fs)));
    g_key_file_set_boolean(
        key, "Options", "delete_after",
        gtk_check_button_get_active(GTK_CHECK_BUTTON(cecup.delete_after)));
    g_key_file_set_boolean(
        key, "Options", "delete_excluded",
        gtk_check_button_get_active(GTK_CHECK_BUTTON(cecup.delete_excluded)));

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
ipc_send_progress(enum DataType type, double fraction) {
    Message *message;
    static double last_fractions[4] = {0.0, 0.0, 0.0, 0.0};
    int32 index = 0;

    if (type == DATA_TYPE_PROGRESS_PREVIEW) {
        index = 3;
    }

    if ((fraction < 1.0) && ((fraction - last_fractions[index]) < 0.001)
        && ((fraction - last_fractions[index]) > -0.001)) {
        return;
    }
    last_fractions[index] = fraction;

    message = xmalloc(SIZEOF(*message));
    memset64(message, 0, SIZEOF(*message));

    message->type = type;
    message->fraction = fraction;
    g_idle_add(update_ui_handler, message);
    return;
}

#if 0 == TESTING_aux
static inline void
aux_functions_sink(void) {
    (void)ipc_send_progress;
    (void)cecup_get_dirs;
    (void)get_target_tasks;
    (void)free_task_list;
    (void)free_message;
}
#endif

#if TESTING_aux
#include <assert.h>
#include <string.h>

int
main(void) {
    (void)ipc_send_progress;
    (void)cecup_get_dirs;
    (void)get_target_tasks;
    (void)free_task_list;
    (void)free_message;
    ASSERT(true);
    exit(EXIT_SUCCESS);
}

#endif

#endif /* AUX_C */
