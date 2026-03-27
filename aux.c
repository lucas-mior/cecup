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

static char *
item_path_get(CecupItem *item) {
    if (item->src_idx >= 0) {
        return cecup.traversal_src.paths[item->src_idx];
    } else if (item->dst_idx >= 0) {
        return cecup.traversal_dst.paths[item->dst_idx];
    } else {
        error2("Error: src_path and dst_path are NULL.\n");
        fatal(EXIT_FAILURE);
    }
}

static int32
item_path_len_get(CecupItem *item) {
    if (item->src_idx >= 0) {
        return cecup.traversal_src.paths_lens[item->src_idx];
    }
    if (item->dst_idx >= 0) {
        return cecup.traversal_dst.paths_lens[item->dst_idx];
    }
    return 0;
}

static char *
item_path_side(CecupItem *item, int32 side) {
    if (side == L) {
        if (item->src_idx >= 0) {
            return cecup.traversal_src.paths[item->src_idx];
        }
    } else {
        if (item->dst_idx >= 0) {
            return cecup.traversal_dst.paths[item->dst_idx];
        }
    }
    return NULL;
}

static int64
item_size_side(CecupItem *item, int32 side) {
    if (side == L) {
        if (item->src_idx >= 0) {
            if (!S_ISDIR(cecup.traversal_src.stats[item->src_idx].st_mode)) {
                return cecup.traversal_src.stats[item->src_idx].st_size;
            }
        }
    } else {
        if (item->dst_idx >= 0) {
            if (!S_ISDIR(cecup.traversal_dst.stats[item->dst_idx].st_mode)) {
                return cecup.traversal_dst.stats[item->dst_idx].st_size;
            }
        }
    }
    return -1;
}

static int64
item_mtime_side(CecupItem *item, int32 side) {
    if (side == L) {
        if (item->src_idx >= 0) {
            return cecup.traversal_src.stats[item->src_idx].st_mtime;
        }
    } else {
        if (item->dst_idx >= 0) {
            return cecup.traversal_dst.stats[item->dst_idx].st_mtime;
        }
    }
    return 0;
}

static char *
item_ignore_pattern_side(CecupItem *item, int32 side) {
    if (side == L) {
        if (item->src_idx >= 0) {
            return cecup.traversal_src.matched_patterns[item->src_idx];
        }
    } else {
        if (item->dst_idx >= 0) {
            return cecup.traversal_dst.matched_patterns[item->dst_idx];
        }
    }
    return NULL;
}

static int32
item_path_len_side(CecupItem *item, int32 side) {
    if (side == L) {
        if (item->src_idx >= 0) {
            return (int32)cecup.traversal_src.paths_lens[item->src_idx];
        }
    } else {
        if (item->dst_idx >= 0) {
            return (int32)cecup.traversal_dst.paths_lens[item->dst_idx];
        }
    }
    return 0;
}

static char *
item_link_target_side(CecupItem *item, int32 side) {
    if (side == L) {
        if (item->src_idx >= 0) {
            return cecup.traversal_src.link_targets[item->src_idx];
        }
    } else {
        if (item->dst_idx >= 0) {
            return cecup.traversal_dst.link_targets[item->dst_idx];
        }
    }
    return NULL;
}

static int32
item_link_target_len_side(CecupItem *item, int32 side) {
    if (side == L) {
        if (item->src_idx >= 0) {
            return (int32)cecup.traversal_src.link_targets_lens[item->src_idx];
        }
    } else {
        if (item->dst_idx >= 0) {
            return (int32)cecup.traversal_dst.link_targets_lens[item->dst_idx];
        }
    }
    return 0;
}

static void
item_get_actions_reasons(CecupItem *item, enum Action *action_src,
                         enum Action *action_dst, enum Reason *reason) {
    int32 src_idx = item->src_idx;
    int32 dst_idx = item->dst_idx;
    bool delete_excluded
        = gtk_check_button_get_active(GTK_CHECK_BUTTON(cecup.delete_excluded));

    *reason = 0;

    if ((cecup.traversal_src.stats == NULL)
         || (cecup.traversal_dst.stats == NULL)) {
        error("Function %s called while the traversal stats array is null.",
              __func__);
        error("This probably means that there is some race condition.\n");
        error("Or another bug.\n");
        fatal(EXIT_FAILURE);
    }

    if (src_idx < 0) {
        char *matched_dst;

        matched_dst = cecup.traversal_dst.matched_patterns[dst_idx];
        *reason |= REASON_MISSING;

        if (matched_dst) {
            *reason |= REASON_IGNORED;
        }

        *action_src = ACTION_IGNORE;

        if (delete_excluded) {
            *action_dst = ACTION_DELETE;
        } else {
            *action_dst = ACTION_IGNORE;
        }

        return;
    }

    if (dst_idx < 0) {
        char *matched_src = cecup.traversal_src.matched_patterns[src_idx];
        struct stat *stat_src = &cecup.traversal_src.stats[src_idx];
        bool is_symlink = S_ISLNK(stat_src->st_mode);
        bool is_hardlink = S_ISREG(stat_src->st_mode)
                           && cecup.traversal_src.link_targets[src_idx];

        if (matched_src) {
            *action_src = ACTION_IGNORE;
            *action_dst = ACTION_IGNORE;
            *reason |= REASON_IGNORED;
            return;
        }

        *reason |= REASON_NEW;

        if (is_hardlink) {
            *action_src = ACTION_HARDLINK;
            *action_dst = ACTION_HARDLINK;
            *reason |= REASON_HARDLINK;
        } else if (is_symlink) {
            *action_src = ACTION_SYMLINK;
            *action_dst = ACTION_SYMLINK;
            *reason |= REASON_SYMLINK;
        } else {
            *action_src = ACTION_NEW;
            *action_dst = ACTION_NEW;
        }

        return;
    }

    {
        char *matched_src = cecup.traversal_src.matched_patterns[src_idx];
        struct stat *stat_src = &cecup.traversal_src.stats[src_idx];
        struct stat *stat_dst = &cecup.traversal_dst.stats[dst_idx];
        char *path_src = cecup.traversal_src.paths[src_idx];
        char *target_src = cecup.traversal_src.link_targets[src_idx];
        char *target_dst = cecup.traversal_dst.link_targets[dst_idx];
        int32 nlinks_src = cecup.traversal_src.nlinks[src_idx];
        int32 nlinks_dst = cecup.traversal_dst.nlinks[dst_idx];
        bool is_symlink = S_ISLNK(stat_src->st_mode);
        bool is_hardlink = S_ISREG(stat_src->st_mode) && (target_src != NULL);
        bool is_dir = S_ISDIR(stat_src->st_mode);
        bool equal = false;
        bool attributes_differ = false;
        bool delete_after
            = gtk_check_button_get_active(GTK_CHECK_BUTTON(cecup.delete_after));

        if (matched_src) {
            *action_src = ACTION_IGNORE;
            *reason |= REASON_IGNORED;

            if (delete_excluded) {
                *action_dst = ACTION_DELETE;
            } else {
                *action_dst = ACTION_IGNORE;
            }

            return;
        }

        if (is_symlink) {
            *reason |= REASON_SYMLINK;

            if (S_ISLNK(stat_dst->st_mode)) {
                if (target_src) {
                    if (target_dst) {
                        if (strcmp(target_src, target_dst) == 0) {
                            equal = true;
                        }
                    }
                }
            }
        } else {
            if (is_hardlink) {
                *reason |= REASON_HARDLINK;
            }

            if (!is_dir) {
                if (stat_src->st_size != stat_dst->st_size) {
                    *reason |= REASON_SIZE;
                    attributes_differ = true;
                }
            }

            if (stat_src->st_mtime > stat_dst->st_mtime) {
                *reason |= REASON_MTIME_NEWER;
                attributes_differ = true;
            }

            if (stat_src->st_mtime < stat_dst->st_mtime) {
                *reason |= REASON_MTIME_OLDER;
                if (delete_after) {
                    attributes_differ = true;
                } else {
                    *action_src = ACTION_IGNORE;
                    *action_dst = ACTION_IGNORE;
                    return;
                }
            }

            if (is_dir) {
                if (stat_src->st_ctime > stat_dst->st_ctime) {
                    *reason |= REASON_CTIME;
                    attributes_differ = true;
                }
            }

            if (stat_src->st_uid != stat_dst->st_uid) {
                *reason |= REASON_OWNER;
                attributes_differ = true;
            }

            if (stat_src->st_gid != stat_dst->st_gid) {
                *reason |= REASON_GROUP;
                attributes_differ = true;
            }

            if ((stat_src->st_mode & 07777) != (stat_dst->st_mode & 07777)) {
                *reason |= REASON_PERM;
                attributes_differ = true;
            }

            if (is_hardlink) {
                if (!S_ISREG(stat_dst->st_mode)) {
                    equal = false;
                    *reason |= REASON_HARDLINK_NOT_REGULAR;
                    attributes_differ = true;
                } else if (target_dst == NULL) {
                    equal = false;
                    attributes_differ = true;
                    *reason |= REASON_HARDLINK_MISSING_LINK;
                } else if (
                        strcmp(target_src, target_dst)
                        && strcmp(path_src, target_dst)
                        && (nlinks_src != nlinks_dst)
                        ) {
                    equal = false;
                    attributes_differ = true;
                    *reason |= REASON_HARDLINK_NOT_MATCH;
                }
            }

            if (!attributes_differ) {
                equal = true;
            }
        }

        if (equal) {
            *action_src = ACTION_EQUAL;
            *action_dst = ACTION_EQUAL;
            *reason |= REASON_EQUAL;
        } else {
            if (is_hardlink) {
                *action_src = ACTION_HARDLINK;
                *action_dst = ACTION_HARDLINK;
            } else if (is_symlink) {
                *action_src = ACTION_SYMLINK;
                *action_dst = ACTION_SYMLINK;
            } else {
                *action_src = ACTION_UPDATE;
                *action_dst = ACTION_UPDATE;
            }
        }
    }

    return;
}

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
    data->nlinks[idx] = 1;

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

        free(task->path, task->path_len + 1);
        free(task->link_target, task->link_target_len + 1);
        free(task->message, task->message_len);

        free(task, SIZEOF(*task));
    }

    free(tasks, STRUCT_ARRAY_SIZE(tasks, Task *, tasks->count));
    return;
}

static TaskList *
get_target_tasks(int32 side, char *clicked_path,
                 enum Action clicked_action) {
    TaskList *tasks;
    int64 tasks_size = STRUCT_ARRAY_SIZE(tasks, Task *, cecup.rows_len);
    int32 count = 0;

    tasks = xmalloc(tasks_size);
    memset64(tasks, 0, tasks_size);

    for (int32 i = 0; i < cecup.rows_len; i += 1) {
        CecupItem *item = cecup.rows[i];
        char *filepath;
        int32 path_len;
        enum Action action;
        enum Action action_src;
        enum Action action_dst;
        enum Reason reason;
        char *link_target;
        int32 link_target_len;
        Task *task;

        if (!(item->selected)) {
            continue;
        }

        item_get_actions_reasons(item, &action_src, &action_dst, &reason);

        if (side == L) {
            filepath = item_path_side(item, L);
            path_len = item_path_len_side(item, L);
            action = action_src;
        } else {
            filepath = item_path_side(item, R);
            path_len = item_path_len_side(item, R);
            action = action_dst;
        }

        if (filepath == NULL) {
            continue;
        }

        task = xmalloc(SIZEOF(*task));
        memset64(task, 0, SIZEOF(*task));

        task->path_len = path_len;
        task->path = xmalloc(path_len + 1);
        memcpy64(task->path, filepath, path_len + 1);

        link_target = item_link_target_side(item, side);
        link_target_len = item_link_target_len_side(item, side);

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

        if (side == L) {
            traversal = &cecup.traversal_src;
        } else {
            traversal = &cecup.traversal_dst;
        }

        if ((idx_ptr = hash_lookup_fs_map(traversal->map,
                                          clicked_path, task->path_len))) {
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

static int32
cecup_item_compare(const void *a, const void *b) {
    CecupItem *item_a;
    CecupItem *item_b;
    int64 result;
    char *path_a;
    char *path_b;
    int64 size_a;
    int64 size_b;
    int64 mtime_a;
    int64 mtime_b;
    enum Action src_act_a;
    enum Action dst_act_a;
    enum Action src_act_b;
    enum Action dst_act_b;
    enum Reason reason_a;
    enum Reason reason_b;

    item_a = *(CecupItem **)a;
    item_b = *(CecupItem **)b;

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
        path_a = item_path_side(item_a, L);
        path_b = item_path_side(item_b, L);
        if (path_a == NULL && path_b == NULL) {
            result = 0;
        } else if (path_a == NULL) {
            result = -1;
        } else if (path_b == NULL) {
            result = 1;
        } else {
            result = strcmp(path_a, path_b);
        }
        break;
    case COL_DST_PATH:
        path_a = item_path_side(item_a, R);
        path_b = item_path_side(item_b, R);
        if (path_a == NULL && path_b == NULL) {
            result = 0;
        } else if (path_a == NULL) {
            result = -1;
        } else if (path_b == NULL) {
            result = 1;
        } else {
            result = strcmp(path_a, path_b);
        }
        break;
    case COL_SIZE_RAW:
        size_a = item_size_side(item_a, L);
        size_b = item_size_side(item_b, L);
        COMPARE(size_a, size_b);
        break;
    case COL_MTIME_RAW:
        mtime_a = item_mtime_side(item_a, L);
        mtime_b = item_mtime_side(item_b, L);
        COMPARE(mtime_a, mtime_b);
        break;
    case COL_DST_ACTION:
        item_get_actions_reasons(item_a, &src_act_a, &dst_act_a, &reason_a);
        item_get_actions_reasons(item_b, &src_act_b, &dst_act_b, &reason_b);
        COMPARE(dst_act_a, dst_act_b);
        break;
    case COL_MTIME_TEXT:
    case COL_ITEM_PTR:
    case COL_SELECTED:
    case COL_SIZE_TEXT:
    case COL_SRC_ACTION:
    case NUM_COLS:
    default:
        item_get_actions_reasons(item_a, &src_act_a, &dst_act_a, &reason_a);
        item_get_actions_reasons(item_b, &src_act_b, &dst_act_b, &reason_b);
        COMPARE(src_act_a, src_act_b);
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

    free(message->text, message->text_len + 1);
    if (message->src_path) {
        free(message->src_path, message->path_len + 1);
    } else if (message->dst_path) {
        free(message->dst_path, message->path_len + 1);
    }
    free(message->link_target, message->link_target_len + 1);
    free(message->ignore_pattern, message->ignore_pattern_len + 1);
    free(message->old_path, message->old_path_len + 1);
    free(message->new_path, message->new_path_len + 1);

    free(message, SIZEOF(*message));
    return;
}

#if 0 == TESTING_aux
static inline void
aux_functions_sink(void) {
    (void)cecup_get_dirs;
    (void)get_target_tasks;
    (void)free_task_list;
    (void)free_message;
    (void)cecup_item_compare;
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

    {
        char root1[] = ".";
        char root2[] = "./";
        char not_root1[] = "..";
        char not_root2[] = "foo";
        char not_root3[] = "./foo";

        ASSERT(aux_is_root(root1));
        ASSERT(aux_is_root(root2));
        ASSERT(!aux_is_root(not_root1));
        ASSERT(!aux_is_root(not_root2));
        ASSERT(!aux_is_root(not_root3));
    }

    {
        Traversal t;
        int32 idx1;
        int32 idx2;
        char path1[] = "file1.txt";
        char path2[] = "file2.txt";

        memset64(&t, 0, SIZEOF(t));

        idx1 = traversal_push(&t, path1, 9, NULL, NULL, 0, NULL, 0);
        ASSERT(idx1 == 0);
        ASSERT(t.nfiles == 1);
        ASSERT(t.ncapacity == 1024);
        ASSERT(strcmp(t.paths[0], path1) == 0);

        idx2 = traversal_push(&t, path2, 9, NULL, NULL, 0, NULL, 0);
        ASSERT(idx2 == 1);
        ASSERT(t.nfiles == 2);
        ASSERT(t.ncapacity == 1024);
        ASSERT(strcmp(t.paths[1], path2) == 0);

        for (int32 i = 2; i < 1025; i += 1) {
            traversal_push(&t, path1, 9, NULL, NULL, 0, NULL, 0);
        }

        ASSERT(t.nfiles == 1025);
        ASSERT(t.ncapacity == 2048);
    }

    {
        CecupItem item;
        char src_path[] = "src/file";
        char dst_path[] = "dst/file";
        char *paths_src[1];
        char *paths_dst[1];
        int16 paths_lens_src[1];
        int16 paths_lens_dst[1];

        paths_src[0] = src_path;
        paths_dst[0] = dst_path;
        paths_lens_src[0] = 8;
        paths_lens_dst[0] = 8;

        memset64(&cecup, 0, SIZEOF(cecup));
        cecup.traversal_src.paths = paths_src;
        cecup.traversal_dst.paths = paths_dst;
        cecup.traversal_src.paths_lens = paths_lens_src;
        cecup.traversal_dst.paths_lens = paths_lens_dst;

        item.src_idx = 0;
        item.dst_idx = -1;

        ASSERT(strcmp(item_path_side(&item, L), src_path) == 0);
        ASSERT(item_path_side(&item, R) == NULL);
        ASSERT(item_path_len_side(&item, L) == 8);
        ASSERT(item_path_len_side(&item, R) == 0);

        item.src_idx = -1;
        item.dst_idx = 0;

        ASSERT(item_path_side(&item, L) == NULL);
        ASSERT(strcmp(item_path_side(&item, R), dst_path) == 0);
        ASSERT(item_path_len_side(&item, L) == 0);
        ASSERT(item_path_len_side(&item, R) == 8);
    }

    exit(EXIT_SUCCESS);
}
#endif

#endif /* AUX_C */
