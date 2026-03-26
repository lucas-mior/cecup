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

#if !defined(WORK_C)
#define WORK_C

#include <fts.h>
#include <gtk/gtk.h>
#include <ctype.h>
#include <sys/wait.h>
#include <dirent.h>
#include <poll.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>

#include "cecup.h"
#include "util.c"
#include "aux.c"
#include "ignore_patterns.c"

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_work 1
#elif !defined(TESTING_work)
#define TESTING_work 0
#endif

static char *
work_check_itemize_line(char *buf_output) {
    switch (buf_output[0]) {
    case RSYNC_CHAR0_ACTION_SEND:
    case RSYNC_CHAR0_ACTION_RECEIVE:
    case RSYNC_CHAR0_ACTION_CHANGE:
    case RSYNC_CHAR0_ACTION_HARDLINK:
    case RSYNC_CHAR0_ACTION_NO_UPDATE:
        break;
    default:
        return NULL;
    }

    switch (buf_output[1]) {
    case RSYNC_CHAR1_TYPE_FILE:
    case RSYNC_CHAR1_TYPE_DIR:
    case RSYNC_CHAR1_TYPE_SYMLINK:
    case RSYNC_CHAR1_TYPE_DEVICE:
    case RSYNC_CHAR1_TYPE_SPECIAL:
        break;
    default:
        return NULL;
    }

    for (int32 i = 2; i < strlen32(RSYNC_ITEMIZE_PLACEHOLDERS); i += 1) {
        switch (buf_output[i]) {
        case RSYNC_CHAR_ATTR_NO_CHANGE:
        case RSYNC_CHAR_ATTR_ALL_SPACE_MEANS_ALL_UNCHANGED:
        case RSYNC_CHAR_ATTR_NEW:
        case RSYNC_CHAR_ATTR_UNKNOWN:
        case RSYNC_CHAR_ATTR_CHECKSUM:
        case RSYNC_CHAR_ATTR_SIZE:
        case RSYNC_CHAR_ATTR_TIME:
        case RSYNC_CHAR_ATTR_PERM:
        case RSYNC_CHAR_ATTR_OWNER:
        case RSYNC_CHAR_ATTR_GROUP:
        case RSYNC_CHAR_ATTR_ACL:
        case RSYNC_CHAR_ATTR_XATTR:
            break;
        default:
            return NULL;
        }
    }

    if (buf_output[strlen32(RSYNC_ITEMIZE_PLACEHOLDERS)] != ' ') {
        return NULL;
    }

    return buf_output + strlen32(RSYNC_ITEMIZE_PLACEHOLDERS);
}

static void
work_finalize(void) {
    Message *message = xmalloc(SIZEOF(*message));
    memset64(message, 0, SIZEOF(*message));
    message->type = DATA_TYPE_ENABLE_BUTTONS;

    ipc_send_progress(DATA_TYPE_PROGRESS_PREVIEW, 1.0);

    g_idle_add(update_ui_handler, message);
    return;
}

static void
work_add_row(enum CecupAction src_action, enum CecupAction dst_action,
             enum CecupReason reason, bool is_dir,
             char *src_path, char *dst_path, int32 path_len,
             char *link_target, int32 link_target_len,
             char *ignore_pattern, int32 ignore_pattern_len,
             int64 src_size_raw, int64 src_mtime_raw,
             int64 dst_size_raw, int64 dst_mtime_raw) {
    int32 slash;
    char *final_src_path;
    char *final_dst_path;
    char *path;
    time_t unix_timestamp;
    CecupRow *row;

    g_mutex_lock(&cecup.arena_mutex);

    final_src_path = NULL;
    final_dst_path = NULL;

    slash = 0;
    if (is_dir) {
        slash = 1;
    }

    path = xarena_push(cecup.arena, path_len + slash + 1);

    if (src_path) {
        memcpy64(path, src_path, path_len + 1);
        final_src_path = path;
        if (dst_path) {
            final_dst_path = path;
        }
    } else if (dst_path) {
        memcpy64(path, dst_path, path_len + 1);
        final_dst_path = path;
    } else {
        LOG_ERROR("Both source and destination paths are NULL.\n");
        g_mutex_unlock(&cecup.arena_mutex);
        return;
    }

    if (is_dir) {
        if (path[path_len - 1] != '/') {
            path[path_len] = '/';
            path[path_len + 1] = '\0';
            path_len += 1;
        }
    }

    row = xarena_push(cecup.arena, SIZEOF(*row));
    memset64(row, 0, SIZEOF(*row));

    row->src_action = src_action;
    row->dst_action = dst_action;
    row->reason = reason;

    bytes_pretty(row->src_size_text, src_size_raw);
    bytes_pretty(row->dst_size_text, dst_size_raw);
    row->src_size_raw = src_size_raw;
    row->dst_size_raw = dst_size_raw;

    if (src_mtime_raw > 0) {
        struct tm time_information;
        unix_timestamp = (time_t)src_mtime_raw;
        unix_timestamp += timezone_offset;
        gmtime_r(&unix_timestamp, &time_information);
        STRFTIME(row->src_mtime_text, "%Y-%m-%d %H:%M:%S", &time_information);
        row->src_mtime_raw = src_mtime_raw;
    }

    if (dst_mtime_raw > 0) {
        struct tm time_information;
        unix_timestamp = (time_t)dst_mtime_raw;
        unix_timestamp += timezone_offset;
        gmtime_r(&unix_timestamp, &time_information);
        STRFTIME(row->dst_mtime_text, "%Y-%m-%d %H:%M:%S", &time_information);
        row->dst_mtime_raw = dst_mtime_raw;
    }

    if (link_target) {
        row->link_target_len = link_target_len;
        row->link_target = xarena_push(cecup.arena, link_target_len + 1);
        memcpy64(row->link_target, link_target, link_target_len + 1);
    }

    row->ignore_pattern_len = ignore_pattern_len;
    row->ignore_pattern = ignore_pattern;

    row->src_path = final_src_path;
    row->dst_path = final_dst_path;
    row->path_len = path_len;

    if (cecup.rows_len >= cecup.rows_capacity) {
        if (cecup.rows_capacity == 0) {
            cecup.rows_capacity = 1024;
        } else {
            cecup.rows_capacity *= 2;
        }
        cecup.rows = xrealloc(cecup.rows,
                              cecup.rows_capacity*SIZEOF(CecupRow *));
        cecup.rows_visible = xrealloc(cecup.rows_visible,
                                      cecup.rows_capacity*SIZEOF(CecupRow *));
    }

    cecup.rows[cecup.rows_len] = row;
    cecup.rows_len += 1;

    g_mutex_unlock(&cecup.arena_mutex);
    return;
}

static int64
work_traverse_fs(TraversalData *data) {
    int64 file_count;
    char *paths[2];
    FTS *fts_handle;
    FTSENT *ent;

    if (cecup.stop_working) {
        return 0;
    }

    file_count = 0;
    paths[0] = data->base_path;
    paths[1] = NULL;

    if ((fts_handle
         = fts_open(paths, FTS_PHYSICAL | FTS_NOCHDIR, NULL)) == NULL) {
        if (cecup.stop_working == false) {
            error(_("Error walking directory %s: %s.\n"),
                    data->base_path, strerror(errno));
        }
        return 0;
    }

    while ((ent = fts_read(fts_handle))) {
        char *d_name;
        int32 name_len;
        int32 old_full_len;
        bool changed;
        char *path;
        int32 path_len;
        bool is_dir;
        int32 index;

        if (cecup.stop_working) {
            break;
        }

        if (ent->fts_info == FTS_D || ent->fts_info == FTS_DOT) {
            continue;
        }

        if (ent->fts_info == FTS_ERR
                || ent->fts_info == FTS_NS
                || ent->fts_info == FTS_DNR) {
            continue;
        }

        d_name = ent->fts_name;
        name_len = (int32)ent->fts_namelen;
        old_full_len = (int32)ent->fts_pathlen;

        if (old_full_len >= (MAX_PATH_LENGTH / 2)) {
            LOG_ERROR(_("Error: file path is too long:\n"));
            LOG_ERROR("%s\n", ent->fts_path);
            LOG_ERROR(_("Please fix your file system.\n"));
            cecup.stop_working = true;
            break;
        }

        if (name_len > 0) {
            if (isspace(d_name[0])) {
                LOG_ERROR(_("Error: there is a space in the start of the fileneme:\n"));
                LOG_ERROR("'%s'\n", ent->fts_path);
                LOG_ERROR(_("Please fix your file system.\n"));
                cecup.stop_working = true;
                break;
            }

            if (isspace(d_name[name_len - 1])) {
                LOG_ERROR(_("Error: there is space in the end of the fileneme:\n"));
                LOG_ERROR("'%s'\n", ent->fts_path);
                LOG_ERROR(_("Please fix your file system.\n"));
                cecup.stop_working = true;
                break;
            }
        }

        changed = false;

        if (true) {
            char new_name[MAX_PATH_LENGTH];
            char new_full[MAX_PATH_LENGTH];
            int32 j = 0;
            int32 k = 0;

            while (k < name_len) {
                char *earliest_match = NULL;
                int32 replace_i = -1;

                for (int32 ri = 0; ri < (int32)LENGTH(replacements); ri += 1) {
                    char *search = replacements[ri].problem;
                    int64 search_len = strlen32(search);
                    char *match;

                    if ((match = memmem64(&d_name[k], name_len - k,
                                          search, search_len))) {
                        if (earliest_match == NULL || match < earliest_match) {
                            earliest_match = match;
                            replace_i = ri;
                        }
                    }
                }

                if (earliest_match) {
                    int64 prefix_len = earliest_match - &d_name[k];
                    char *replace_str = replacements[replace_i].rename;
                    int64 replace_len = strlen32(replace_str);

                    if (prefix_len > 0) {
                        memcpy64(&new_name[j], &d_name[k], prefix_len);
                        j += (int32)prefix_len;
                        k += (int32)prefix_len;
                    }

                    memcpy64(&new_name[j], replace_str, replace_len);
                    j += (int32)replace_len;
                    k += (int32)strlen32(replacements[replace_i].problem);
                    changed = true;
                } else {
                    int64 remaining = name_len - k;
                    memcpy64(&new_name[j], &d_name[k], remaining);
                    j += (int32)remaining;
                    k += (int32)remaining;
                }
            }
            new_name[j] = '\0';

            if (changed) {
                int32 base_len = (int32)(ent->fts_pathlen - ent->fts_namelen);

                memcpy64(new_full, ent->fts_path, base_len);
                memcpy64(new_full + base_len, new_name, j + 1);

                if (renameat2(AT_FDCWD, ent->fts_path,
                              AT_FDCWD, new_full,
                              RENAME_NOREPLACE) < 0) {
                    LOG_ERROR(_("Error renaming %s to %s: %s\n"),
                              ent->fts_path, new_full, strerror(errno));
                } else {
                    LOG(_("Fixed: %s -> %s\n"), d_name, new_name);
                }
            }
        }

        if (ent->fts_info != FTS_DP) {
            file_count += 1;
        }

        path = ent->fts_path + data->base_path_len;
        path_len = old_full_len - data->base_path_len;

        if (path[0] == '/') {
            path += 1;
            path_len -= 1;
        }

        if (path_len == 0) {
            path = "./";
            path_len = 2;
        }

        path = xmemdup(path, path_len + 1);
        is_dir = (ent->fts_info == FTS_DP);

        if (data->nfiles >= data->ncapacity) {
            if (data->ncapacity == 0) {
                data->ncapacity = 1024;
            } else {
                data->ncapacity *= 2;
            }

            data->stats = xrealloc(data->stats,
                                   data->ncapacity*SIZEOF(*(data->stats)));

            data->paths
                = xrealloc(data->paths,
                           data->ncapacity*SIZEOF(*(data->paths)));
            data->link_targets
                = xrealloc(data->link_targets,
                           data->ncapacity*SIZEOF(*(data->link_targets)));
            data->matched_patterns
                = xrealloc(data->matched_patterns,
                           data->ncapacity*SIZEOF(*(data->matched_patterns)));

            data->paths_lens
                = xrealloc(data->paths_lens,
                           data->ncapacity*SIZEOF(*(data->paths_lens)));
            data->link_targets_lens
                = xrealloc(data->link_targets_lens,
                           data->ncapacity*SIZEOF(*(data->link_targets_lens)));
            data->matched_patterns_lens
                = xrealloc(data->matched_patterns_lens,
                           data->ncapacity*SIZEOF(*(data->matched_patterns_lens)));
        }

        index = data->nfiles;
        data->nfiles += 1;

        memset64(&data->stats[index], 0, SIZEOF(struct stat));
        memcpy64(&data->stats[index], (void *)ent->fts_statp, SIZEOF(struct stat));

        data->paths[index] = path;
        data->link_targets[index] = NULL;
        data->matched_patterns[index] = NULL;

        data->paths_lens[index] = (int16)path_len;
        data->link_targets_lens[index] = 0;
        data->matched_patterns_lens[index] = 0;

        if (ent->fts_info == FTS_SL || ent->fts_info == FTS_SLNONE) {
            char target[MAX_PATH_LENGTH];
            int64 target_len;

            if ((target_len
                 = readlink(ent->fts_path, target, SIZEOF(target))) < 0) {
                LOG_ERROR("Error in readlink(%s): %s.\n",
                          ent->fts_path, strerror(errno));
            } else {
                target[target_len] = '\0';
                data->link_targets[index] = xmemdup(target, target_len + 1);
                data->link_targets_lens[index] = (int16)target_len;
                error("Found symlink, insert at index=%d -> %s\n",
                      index, data->link_targets[index]);
            }
        } else if (ent->fts_info == FTS_F && (ent->fts_statp->st_nlink > 1)) {
            char inode_str[64];
            uint32 n;
            int32 *first_idx_ptr;

            n = (uint32)itoa2(inode_str, (long)ent->fts_statp->st_ino);
            first_idx_ptr = hash_lookup_fs_map(data->inode_map, xstrdup(inode_str), n);
            if (first_idx_ptr) {
                int32 first_idx = *first_idx_ptr;
                data->link_targets[index] = data->paths[first_idx];
                data->link_targets_lens[index] = data->paths_lens[first_idx];

                if (data->link_targets[first_idx] == NULL) {
                    data->link_targets[first_idx] = data->paths[index];
                    data->link_targets_lens[first_idx] = data->paths_lens[index];
                }

                error("Found hardlink, insert at index=%d -> %s\n",
                      index, data->link_targets[index]);
            } else {
                error("CANT Found hardlink\n");
                hash_insert_fs_map(data->inode_map, inode_str, n, index);
            }
        }

        {
            IgnorePattern *pattern = ignore_patterns_match(path, path_len,
                                                           is_dir,
                                                           cecup.ignore_patterns,
                                                           cecup.ignore_count);
            if (pattern) {
                data->matched_patterns[index] = pattern->str;
                data->matched_patterns_lens[index] = (int16)pattern->len;
            }
        }
        hash_insert_fs_map(data->map, path, (uint32)path_len, index);
    }

    fts_close(fts_handle);
    return file_count;
}

static void *
work_traverse_fs_thread(void *user_data) {
    TraversalData *data = user_data;
    data->file_count = work_traverse_fs(data);
    return NULL;
}

static void
work_cleanup(void) {
    hash_destroy_fs_map(cecup.src_map);
    cecup.src_map = NULL;
    hash_destroy_fs_map(cecup.dst_map);
    cecup.dst_map = NULL;

    hash_destroy_fs_map(cecup.src_inode_map);
    cecup.src_inode_map = NULL;
    hash_destroy_fs_map(cecup.dst_inode_map);
    cecup.dst_inode_map = NULL;

    {
        int32 capacity_src;
        int32 capacity_dst;

        capacity_src = cecup.traversal_src.ncapacity;
        capacity_dst = cecup.traversal_dst.ncapacity;

        for (int32 i = 0; i < cecup.traversal_src.nfiles; i += 1) {
            XFREE(cecup.traversal_src.paths[i],
                  cecup.traversal_src.paths_lens[i] + 1);
            if (S_ISLNK(cecup.traversal_src.stats[i].st_mode)) {
                XFREE(cecup.traversal_src.link_targets[i],
                      cecup.traversal_src.link_targets_lens[i] + 1);
            }
        }

        XFREE(cecup.traversal_src.stats,
              capacity_src*SIZEOF(*(cecup.traversal_src.stats)));
        XFREE(cecup.traversal_src.matched_patterns,
              capacity_src*SIZEOF(*(cecup.traversal_src.matched_patterns)));
        XFREE(cecup.traversal_src.link_targets,
              capacity_src*SIZEOF(*(cecup.traversal_src.link_targets)));
        XFREE(cecup.traversal_src.paths,
              capacity_src*SIZEOF(*(cecup.traversal_src.paths)));
        XFREE(cecup.traversal_src.paths_lens,
              capacity_src*SIZEOF(*(cecup.traversal_src.paths_lens)));
        XFREE(cecup.traversal_src.link_targets_lens,
              capacity_src*SIZEOF(*(cecup.traversal_src.link_targets_lens)));
        XFREE(cecup.traversal_src.matched_patterns_lens,
              capacity_src*SIZEOF(*(cecup.traversal_src.matched_patterns_lens)));
        memset64(&cecup.traversal_src, 0, SIZEOF(cecup.traversal_src));

        for (int32 i = 0; i < cecup.traversal_dst.nfiles; i += 1) {
            XFREE(cecup.traversal_dst.paths[i],
                  cecup.traversal_dst.paths_lens[i] + 1);
            if (S_ISLNK(cecup.traversal_dst.stats[i].st_mode)) {
                XFREE(cecup.traversal_dst.link_targets[i],
                      cecup.traversal_dst.link_targets_lens[i] + 1);
            }
        }

        XFREE(cecup.traversal_dst.stats,
              capacity_dst*SIZEOF(*(cecup.traversal_dst.stats)));
        XFREE(cecup.traversal_dst.matched_patterns,
              capacity_dst*SIZEOF(*(cecup.traversal_dst.matched_patterns)));
        XFREE(cecup.traversal_dst.link_targets,
              capacity_dst*SIZEOF(*(cecup.traversal_dst.link_targets)));
        XFREE(cecup.traversal_dst.paths,
              capacity_dst*SIZEOF(*(cecup.traversal_dst.paths)));
        XFREE(cecup.traversal_dst.paths_lens,
              capacity_dst*SIZEOF(*(cecup.traversal_dst.paths_lens)));
        XFREE(cecup.traversal_dst.link_targets_lens,
              capacity_dst*SIZEOF(*(cecup.traversal_dst.link_targets_lens)));
        XFREE(cecup.traversal_dst.matched_patterns_lens,
              capacity_dst*SIZEOF(*(cecup.traversal_dst.matched_patterns_lens)));
        memset64(&cecup.traversal_dst, 0, SIZEOF(cecup.traversal_dst));

        XFREE(cecup.transfers,
              cecup.transfers_capacity*SIZEOF(*cecup.transfers));
        cecup.transfers = NULL;
        cecup.ntransfers = 0;
        cecup.transfers_capacity = 0;
    }
    return;
}

static void __attribute__((noreturn))
work_preview_cancel_and_reset(void) {
    work_cleanup();
    work_finalize();
    g_thread_exit(NULL);
}

static void *
work_preview(void *user_data) {
    ThreadData *thread_data = user_data;
    int64 nfiles_total = 0;
    int64 nfiles_processed = 0;
    bool same_fs = true;
    struct timespec t0_work;
    struct timespec t1_work;

    clock_gettime(CLOCK_MONOTONIC_RAW, &t0_work);
    work_cleanup();

    {
        struct stat stat_src;
        struct stat stat_dst;

        if (stat(cecup.src_base, &stat_src) < 0) {
            LOG_ERROR("Error getting directory info from %s: %s.\n",
                      cecup.src_base, strerror(errno));
            work_preview_cancel_and_reset();
        }
        if (stat(cecup.dst_base, &stat_dst) < 0) {
            LOG_ERROR("Error getting directory info from %s: %s.\n",
                      cecup.dst_base, strerror(errno));
            work_preview_cancel_and_reset();
        }

        same_fs = (stat_src.st_dev == stat_dst.st_dev);
    }

    if (thread_data->check_different_fs && same_fs) {
        LOG_ERROR(
            _("Safety stop: Original and backup are on the same storage "
              "device.\n"
              "Check if the backup device is connected.\n"
              "To force backup on a folder in the same device, uncheck"
              " option \"Protect same drive sync\".\n"));

        work_preview_cancel_and_reset();
    }

    ignore_patterns_load();

    cecup.src_map = hash_create_fs_map(1024);
    cecup.dst_map = hash_create_fs_map(1024);
    cecup.src_inode_map = hash_create_fs_map(1024);
    cecup.dst_inode_map = hash_create_fs_map(1024);

    cecup.traversal_src.base_path = cecup.src_base;
    cecup.traversal_src.base_path_len = cecup.src_base_len;
    cecup.traversal_src.map = cecup.src_map;
    cecup.traversal_src.inode_map = cecup.src_inode_map;

    cecup.traversal_dst.base_path = cecup.dst_base;
    cecup.traversal_dst.base_path_len = cecup.dst_base_len;
    cecup.traversal_dst.map = cecup.dst_map;
    cecup.traversal_dst.inode_map = cecup.dst_inode_map;

    LOG(_("Traversing file systems...\n"));
    if (!same_fs) {
        GThread *t1;
        GThread *t2;

        t2 = g_thread_new("traversal_dst",
                          work_traverse_fs_thread, &cecup.traversal_dst);
        t1 = g_thread_new("traversal_src",
                          work_traverse_fs_thread, &cecup.traversal_src);

        g_thread_join(t1);
        g_thread_join(t2);
    } else {
        work_traverse_fs_thread(&cecup.traversal_src);
        work_traverse_fs_thread(&cecup.traversal_dst);
    }

    if (cecup.stop_working) {
        LOG_ERROR(_("Stop requested.\n"));
        work_preview_cancel_and_reset();
    }

    LOG(_("File system traversal finished.\n"));

    nfiles_total = cecup.traversal_src.file_count + cecup.traversal_dst.file_count;
    LOG(_("Found %lld files to analyse...\n"), (llong)nfiles_total);

    for (uint32 i = 0; i < cecup.src_map->capacity; i += 1) {
        Bucket_fs_map *bucket_src = &cecup.src_map->array[i];
        int32 src_idx;
        int32 *dst_idx_ptr;
        struct stat *stat_src;
        char *matched_pattern_src;
        char *link_target_src;
        bool is_symlink;
        bool is_hardlink;
        bool is_dir;
        enum CecupAction action = 0;
        enum CecupAction src_action;
        enum CecupAction dst_action;
        enum CecupReason reason;
        char *dst_path = NULL;
        int64 dst_size = 0;
        int64 dst_mtime = 0;
        int64 src_size = 0;
        int32 path_len;
        int32 link_target_len;
        int32 matched_pattern_len;

        if ((int64)bucket_src->key <= 0) {
            continue;
        }

        src_idx = bucket_src->value;
        stat_src = &cecup.traversal_src.stats[src_idx];
        matched_pattern_src = cecup.traversal_src.matched_patterns[src_idx];
        link_target_src = cecup.traversal_src.link_targets[src_idx];
        link_target_len = cecup.traversal_src.link_targets_lens[src_idx];
        matched_pattern_len = cecup.traversal_src.matched_patterns_lens[src_idx];
        path_len = cecup.traversal_src.paths_lens[src_idx];

        is_symlink = S_ISLNK(stat_src->st_mode);
        is_hardlink = S_ISREG(stat_src->st_mode) && link_target_src;
        is_dir = S_ISDIR(stat_src->st_mode);

        if ((dst_idx_ptr
             = hash_lookup_fs_map(cecup.dst_map, bucket_src->key, (uint32)path_len))) {
            int32 dst_idx = *dst_idx_ptr;
            struct stat *stat_dst = &cecup.traversal_dst.stats[dst_idx];
            char *link_target_dst = cecup.traversal_dst.link_targets[dst_idx];

            dst_path = bucket_src->key;
            dst_size = stat_dst->st_size;
            dst_mtime = stat_dst->st_mtime;
            reason = 0;

            if (matched_pattern_src) {
                action = ACTION_IGNORE;
                reason |= REASON_IGNORED;
            } else {
                bool equal = false;
                bool attributes_differ = false;

                if (is_symlink) {
                    reason |= REASON_SYMLINK;
                    if (S_ISLNK(stat_dst->st_mode)
                        && link_target_src
                        && link_target_dst
                        && !strcmp(link_target_src, link_target_dst)) {
                        equal = true;
                    }
                } else {
                    if (is_hardlink) {
                        reason |= REASON_HARDLINK;
                    }

                    if (!is_dir && (stat_src->st_size != stat_dst->st_size)) {
                        reason |= REASON_SIZE;
                        attributes_differ = true;
                    }
                    if (stat_src->st_mtime != stat_dst->st_mtime) {
                        reason |= REASON_MTIME;
                        attributes_differ = true;
                    }
                    if (is_dir && (stat_src->st_ctime > stat_dst->st_ctime)) {
                        reason |= REASON_CTIME;
                        attributes_differ = true;
                    }
                    if (stat_src->st_uid != stat_dst->st_uid) {
                        reason |= REASON_OWNER;
                        attributes_differ = true;
                    }
                    if (stat_src->st_gid != stat_dst->st_gid) {
                        reason |= REASON_GROUP;
                        attributes_differ = true;
                    }
                    if ((stat_src->st_mode & 07777)
                         != (stat_dst->st_mode & 07777)) {
                        reason |= REASON_PERM;
                        attributes_differ = true;
                    }

                    do {
                        if (is_hardlink) {
                            if (!S_ISREG(stat_dst->st_mode)) {
                                LOG("Other side is not a regular file.\n");
                                equal = false;
                                break;
                            }
                            if (link_target_dst == NULL) {
                                LOG("Other side does not have a link .\n");
                                equal = false;
                                break;
                            }
                            if (strcmp(link_target_src, link_target_dst)) {
                                LOG("Other side target is not the same.\n");
                                LOG("%s != %s", link_target_src, link_target_dst);
                                equal = false;
                                break;
                            }
                            equal = true;
                            break;
                        } else {
                            if (!attributes_differ) {
                                if (action == ACTION_HARDLINK) {
                                    error("attributes_differ for %s hardlink\n",
                                          bucket_src->key);
                                }
                                equal = true;
                            }
                        }
                    } while (0);
                }

                if (equal) {
                    action = ACTION_EQUAL;
                    reason |= REASON_EQUAL;
                } else {
                    if (is_hardlink) {
                        action = ACTION_HARDLINK;
                    } else if (is_symlink) {
                        action = ACTION_SYMLINK;
                    } else {
                        action = ACTION_UPDATE;
                    }
                }
            }
        } else {
            reason = 0;
            if (matched_pattern_src) {
                action = ACTION_IGNORE;
                reason |= REASON_IGNORED;
            } else {
                reason |= REASON_NEW;
                if (is_hardlink) {
                    action = ACTION_HARDLINK;
                    reason |= REASON_HARDLINK;
                } else if (is_symlink) {
                    action = ACTION_SYMLINK;
                    reason |= REASON_SYMLINK;
                } else {
                    action = ACTION_NEW;
                }
            }
        }

        src_action = action;
        dst_action = action;

        if (action == ACTION_IGNORE) {
            src_action = ACTION_IGNORE;
            if (dst_path != NULL) {
                if (thread_data->delete_excluded) {
                    dst_action = ACTION_DELETE;
                } else {
                    dst_action = ACTION_IGNORE;
                }
            } else {
                dst_action = ACTION_IGNORE;
            }
        } else if (action == ACTION_DELETE) {
            src_action = ACTION_IGNORE;
            dst_action = ACTION_DELETE;
        }

        if ((action != ACTION_EQUAL) && (action != ACTION_IGNORE)) {
            if (cecup.ntransfers >= (cecup.transfers_capacity - 1)) {
                if (cecup.transfers_capacity == 0) {
                    cecup.transfers_capacity = 256;
                } else {
                    cecup.transfers_capacity *= 2;
                }
                cecup.transfers = xrealloc(cecup.transfers,
                                           cecup.transfers_capacity*SIZEOF(*cecup.transfers));
            }
            if (action == ACTION_HARDLINK) {
                cecup.transfers[cecup.ntransfers] = link_target_src;
                cecup.ntransfers += 1;
            }
            cecup.transfers[cecup.ntransfers] = bucket_src->key;
            cecup.ntransfers += 1;
        }

        if (is_dir) {
            src_size = -1;
        } else {
            src_size = stat_src->st_size;
        }

        work_add_row(src_action, dst_action, reason, S_ISDIR(stat_src->st_mode),
                     bucket_src->key, dst_path, path_len,
                     link_target_src, link_target_len,
                     matched_pattern_src, matched_pattern_len,
                     src_size, stat_src->st_mtime,
                     dst_size, dst_mtime);

        nfiles_processed += 1;
        if ((nfiles_processed % 1000) == 0) {
            ipc_send_progress(DATA_TYPE_PROGRESS_PREVIEW,
                              (double)nfiles_processed
                              / (double)nfiles_total);
        }
    }

    for (uint32 i = 0; i < cecup.dst_map->capacity; i += 1) {
        Bucket_fs_map *bucket_dst = &cecup.dst_map->array[i];
        int32 dst_idx;
        struct stat *stat_dst;
        char *matched_pattern_dst;
        char *link_target_dst;
        int32 link_target_len;
        int32 matched_pattern_len;
        enum CecupAction action = ACTION_DELETE;
        enum CecupReason reason = 0;
        enum CecupAction src_action;
        enum CecupAction dst_action;
        int32 path_len;

        if ((int64)bucket_dst->key <= 0) {
            continue;
        }

        dst_idx = bucket_dst->value;
        path_len = cecup.traversal_dst.paths_lens[dst_idx];

        if (hash_lookup_fs_map(cecup.src_map,
                               bucket_dst->key, (uint32)path_len) == NULL) {
            stat_dst = &cecup.traversal_dst.stats[dst_idx];
            matched_pattern_dst = cecup.traversal_dst.matched_patterns[dst_idx];
            link_target_dst = cecup.traversal_dst.link_targets[dst_idx];
            link_target_len = cecup.traversal_dst.link_targets_lens[dst_idx];
            matched_pattern_len = cecup.traversal_dst.matched_patterns_lens[dst_idx];

            if (matched_pattern_dst) {
                if (!thread_data->delete_excluded) {
                    action = ACTION_IGNORE;
                }
                reason |= REASON_IGNORED;
            }

            reason |= REASON_MISSING;

            src_action = action;
            dst_action = action;

            if (action == ACTION_IGNORE) {
                src_action = ACTION_IGNORE;
                if (thread_data->delete_excluded) {
                    dst_action = ACTION_DELETE;
                } else {
                    dst_action = ACTION_IGNORE;
                }
            } else if (action == ACTION_DELETE) {
                src_action = ACTION_IGNORE;
                dst_action = ACTION_DELETE;
            }

            work_add_row(src_action, dst_action, reason, S_ISDIR(stat_dst->st_mode),
                         NULL, bucket_dst->key, path_len,
                         link_target_dst, link_target_len,
                         matched_pattern_dst, matched_pattern_len,
                         0, 0,
                         stat_dst->st_size, stat_dst->st_mtime);
        }

        nfiles_processed += 1;
        if ((nfiles_processed % 1000) == 0) {
            ipc_send_progress(DATA_TYPE_PROGRESS_PREVIEW,
                              (double)nfiles_processed
                              / (double)nfiles_total);
        }
    }

    clock_gettime(CLOCK_MONOTONIC_RAW, &t1_work);
    PRINT_TIMINGS(nfiles_total, t0_work, t1_work);
    work_finalize();
    g_thread_exit(NULL);
}

static bool
work_rsync_run(char *files_from_filename, bool checksum) {
    char *rsync_args[64];
    char buf_error[MAX_PATH_LENGTH*2];
    char buf_output[MAX_PATH_LENGTH*2];
    char cmd[MAX_PATH_LENGTH*2];
    char dst_base_with_slash[MAX_PATH_LENGTH];
    char src_base_with_slash[MAX_PATH_LENGTH];
    int32 pipe_stderr[2];
    int32 pipe_stdout[2];
    int32 rsync_args_len = 0;
    int64 buf_output_pos = 0;
    pid_t child_pid;
    struct pollfd pipes[2];

    SNPRINTF(src_base_with_slash, "%s/", cecup.src_base);
    SNPRINTF(dst_base_with_slash, "%s/", cecup.dst_base);

    rsync_args_len = 0;
    rsync_args[rsync_args_len++] = "rsync";
    rsync_args[rsync_args_len++] = "--verbose";
    rsync_args[rsync_args_len++] = "--update";
    rsync_args[rsync_args_len++] = "--dirs";
    rsync_args[rsync_args_len++] = "--partial";
    rsync_args[rsync_args_len++] = "--progress";
    rsync_args[rsync_args_len++] = "--info=progress2";
    rsync_args[rsync_args_len++] = "--links";
    rsync_args[rsync_args_len++] = "--hard-links";
    if (checksum) {
        rsync_args[rsync_args_len++] = "--checksum";
    }
    rsync_args[rsync_args_len++] = "--perms";
    rsync_args[rsync_args_len++] = "--times";
    rsync_args[rsync_args_len++] = "--owner";
    rsync_args[rsync_args_len++] = "--group";
    rsync_args[rsync_args_len++] = "--itemize-changes";
    rsync_args[rsync_args_len++] = "--files-from";
    rsync_args[rsync_args_len++] = files_from_filename;
    rsync_args[rsync_args_len++] = "--iconv=.,.";

    rsync_args[rsync_args_len++] = src_base_with_slash;
    rsync_args[rsync_args_len++] = dst_base_with_slash;
    rsync_args[rsync_args_len++] = NULL;

    LOG(_("Running sync...\n"));
    STRING_FROM_ARRAY(cmd, " ", rsync_args, rsync_args_len);
    LOG_CMD("%s\n", cmd);

    xpipe(pipe_stdout);
    xpipe(pipe_stderr);

    switch (child_pid = fork()) {
    case -1:
        error("Error forking: %s.\n", strerror(errno));
        fatal(EXIT_FAILURE);
    case 0:
        if (setpgid(0, 0) < 0) {
            error("Error setpgid: %s.\n", strerror(errno));
            fatal(EXIT_FAILURE);
        }
        putenv("LC_ALL=C.UTF-8");

        XCLOSE(&pipe_stderr[0]);
        XCLOSE(&pipe_stdout[0]);

        xdup2(pipe_stdout[1], STDOUT_FILENO);
        xdup2(pipe_stderr[1], STDERR_FILENO);

        XCLOSE(&pipe_stderr[1]);
        XCLOSE(&pipe_stdout[1]);

        execvp(rsync_args[0], rsync_args);
        error("Error executing\n%s\n%s.\n", cmd, strerror(errno));
        _exit(EXIT_FAILURE);
    default:
        cecup.child_pid = child_pid;
        XCLOSE(&pipe_stderr[1]);
        XCLOSE(&pipe_stdout[1]);
        break;
    }

    pipes[0].fd = pipe_stdout[0];
    pipes[1].fd = pipe_stderr[0];
    pipes[0].events = POLLIN;
    pipes[1].events = POLLIN;

    do {
        int64 r;
        char *eol;

        pipes[0].revents = 0;
        pipes[1].revents = 0;

        switch (poll(pipes, 2, -1)) {
        case -1:
            if (errno != EINTR) {
                error("Error in poll: %s.\n", strerror(errno));
                fatal(EXIT_FAILURE);
            }
            continue;
        case 0:
            continue;
        default:
            break;
        }

        if (pipes[0].revents & POLLERR) {
            pipes[0].fd = -1;
            goto read_error_pipe;
        }
        if (pipes[0].revents & POLLHUP) {
            pipes[0].fd = -1;
        }
        if (!(pipes[0].revents & POLLIN)) {
            goto read_error_pipe;
        }

        r = read64(pipe_stdout[0], buf_output + buf_output_pos,
                   SIZEOF(buf_output) - buf_output_pos - 1);
        if (r <= 0) {
            if (r < 0) {
                LOG_ERROR("Error reading stdout pipe: %s.\n", strerror(errno));
                pipes[0].fd = -1;
            }
            goto read_error_pipe;
        }
        buf_output_pos += r;

        while ((eol = memchr64(buf_output, '\n', buf_output_pos))
               || (eol = memchr64(buf_output, '\r', buf_output_pos))) {
            int64 line_len;
            int64 remaining;
            char *path;
            bool only_space = true;
            char *p = buf_output;

            line_len = (int64)(eol - buf_output);
            *eol = '\0';

            while (*p) {
                if (!isspace(*p)) {
                    only_space = false;
                    break;
                }
                p += 1;
            }
            if (!only_space) {
                LOG("%s\n", buf_output);
            }

            if ((path = work_check_itemize_line(buf_output))) {
                int32 path_len;
                char *sep;
                Message *msg_update;

                while (*path == ' ') {
                    path += 1;
                }
                path_len = (int32)(eol - path);
                if ((sep = memmem64(path, path_len,
                                    RSYNC_HARDLINK_NOTATION,
                                    strlen32(RSYNC_HARDLINK_NOTATION)))) {
                    *sep = '\0';
                    path_len = (int32)(sep - path);
                } else if ((sep = memmem64(path, path_len,
                                           RSYNC_SYMLINK_NOTATION,
                                           strlen32(RSYNC_SYMLINK_NOTATION)))) {
                    *sep = '\0';
                    path_len = (int32)(sep - path);
                }

                if (path_len == 1) {
                    if (path[0] == '.') {
                        path = "./";
                        path_len = 2;
                    }
                }

                msg_update = xmalloc(SIZEOF(*msg_update));
                memset64(msg_update, 0, SIZEOF(*msg_update));
                msg_update->type = DATA_TYPE_ROW_TRANSFER;
                msg_update->path_len = path_len;
                msg_update->src_path = xmalloc(path_len + 1);
                memcpy64(msg_update->src_path, path, path_len + 1);
                g_idle_add(update_ui_handler, msg_update);
            }

            remaining = buf_output_pos - (line_len + 1);
            if (remaining > 0) {
                memmove64(buf_output, eol + 1, remaining);
            }
            buf_output_pos = remaining;
            if (buf_output_pos <= 0) {
                break;
            }
        }

    read_error_pipe:
        if (pipes[1].revents & POLLERR) {
            pipes[1].fd = -1;
            continue;
        }
        if (pipes[1].revents & POLLHUP) {
            pipes[1].fd = -1;
        }
        if (!(pipes[1].revents & POLLIN)) {
            continue;
        }

        r = read64(pipe_stderr[0], buf_error, SIZEOF(buf_error) - 1);
        if (r <= 0) {
            if (r < 0) {
                LOG_ERROR("Error reading stderr pipe: %s.\n", strerror(errno));
                pipes[1].fd = -1;
            }
            continue;
        }
        buf_error[r] = '\0';
        LOG_ERROR("%s", buf_error);

    } while ((pipes[0].fd >= 0) || (pipes[1].fd >= 0));

    if (waitpid(child_pid, NULL, 0) < 0) {
        LOG_ERROR(_("Error waiting for child: %s.\n"), strerror(errno));
        LOG_ERROR(_("Killing the child with SIGKILL..."));
        xkill(child_pid, SIGKILL);
        return false;
    }

    cecup.child_pid = 0;
    XCLOSE(&pipe_stderr[0]);
    XCLOSE(&pipe_stdout[0]);
    return true;
}

static void *
work_rsync(void *user_data) {
    ThreadData *thread_data = user_data;
    TaskList *tasks = thread_data->tasks;
    bool has_transfers = false;
    char files_from_filename[] = "/tmp/cecup_XXXXXX";
    int files_from_fd;

    if (tasks == NULL) {
        if (cecup.ntransfers <= 0) {
            LOG_ERROR("There are no operations to make.\n");
            work_finalize();
            XFREE(thread_data, SIZEOF(*thread_data));
            return NULL;
        }
        tasks = xmalloc(sizeof(*tasks));
        memset64(tasks, 0, sizeof(*tasks));
    }

    for (int32 i = 0; i < tasks->count; i += 1) {
        Task *task = tasks->items[i];
        char full_path[MAX_PATH_LENGTH];
        pid_t child_rm;
        int child_status;
        bool removed;

        removed = false;

        if (task->action != ACTION_DELETE) {
            has_transfers = true;
            continue;
        }

        if (cecup.stop_working) {
            LOG_ERROR(_("Stop requested.\n"));
            free_task_list(tasks);
            work_finalize();
            XFREE(thread_data, SIZEOF(*thread_data));
            return NULL;
        }

        if (task->side == L) {
            SNPRINTF(full_path, "%s/%s", cecup.src_base, task->path);
        } else {
            SNPRINTF(full_path, "%s/%s", cecup.dst_base, task->path);
        }

        switch (child_rm = fork()) {
        case -1:
            error("Error forking: %s.\n", strerror(errno));
            fatal(EXIT_FAILURE);
        case 0: {
            char cmd_rm[MAX_PATH_LENGTH];
            char *args_rm[] = {
                "rm",
                "-rvf",
                full_path,
                NULL,
            };

            STRING_FROM_ARRAY(cmd_rm, " ", args_rm, LENGTH(args_rm));

            execvp(args_rm[0], args_rm);
            error("Error executing\n%s\n%s.\n", cmd_rm, strerror(errno));
            _exit(EXIT_FAILURE);
        }
        default:
            cecup.child_pid = child_rm;
            if (waitpid(child_rm, &child_status, 0) < 0) {
                LOG_ERROR("Error waiting for child: %s.\n", strerror(errno));
            } else if (WIFEXITED(child_status)) {
                removed = !WEXITSTATUS(child_status);
            }
            cecup.child_pid = 0;
            break;
        }

        if (removed) {
            Message *msg_rm;

            LOG("Removed %s...\n", full_path);

            msg_rm = xmalloc(SIZEOF(*msg_rm));
            memset64(msg_rm, 0, SIZEOF(*msg_rm));
            msg_rm->type = DATA_TYPE_ROW_REMOVE;
            msg_rm->side = task->side;
            msg_rm->path_len = task->path_len;
            msg_rm->src_path = xmalloc(msg_rm->path_len + 1);
            memcpy64(msg_rm->src_path, task->path, msg_rm->path_len + 1);
            g_idle_add(update_ui_handler, msg_rm);
        }
    }

    if (!has_transfers) {
        work_finalize();
        free_task_list(tasks);
        XFREE(thread_data, SIZEOF(*thread_data));
        return NULL;
    }

    if ((files_from_fd = mkstemp(files_from_filename)) < 0) {
        error("Error in mkstemp: %s.\n", strerror(errno));
        fatal(EXIT_FAILURE);
    }

    for (int32 i = 0; i < cecup.ntransfers; i += 1) {
        char *file;
        int64 w;
        int64 written;
        int64 left;

        file = cecup.transfers[i];
        written = 0;
        left = strlen32(file);

        while ((w = write64(files_from_fd, &file[written], left)) > 0) {
            written += w;
            left -= w;
            if (left <= 0) {
                break;
            }
        }
        if (w < 0) {
            error("Error writing to %s: %s.\n",
                  files_from_filename, strerror(errno));
            fatal(EXIT_FAILURE);
        }

        write64(files_from_fd, "\n", 1);
    }

    for (int32 i = 0; i < tasks->count; i += 1) {
        Task *task = tasks->items[i];

        if (task->action == ACTION_HARDLINK) {
            write64(files_from_fd, task->link_target, task->link_target_len);
            write64(files_from_fd, "\n", 1);
        }
        write64(files_from_fd, task->path, task->path_len);
        write64(files_from_fd, "\n", 1);
    }

    XCLOSE(&files_from_fd);

    do {
        if (work_rsync_run(files_from_filename, false)) {
            if (cecup.stop_working) {
                LOG_ERROR(_("Stop requested.\n"));
                break;
            }
            work_rsync_run(files_from_filename, true);
        }
    } while (0);
    if (!DEBUGGING) {
        xunlink(files_from_filename);
    }

    free_task_list(tasks);
    if (tasks->count == 0) {
        work_cleanup();
    }
    work_finalize();
    XFREE(thread_data, SIZEOF(*thread_data));
    return NULL;
}

#if TESTING_work
#include "assert.c"
#include <string.h>
#include <stdio.h>
#include "aux.c"

int
main(void) {
    (void)work_rsync;
    (void)work_preview;
    exit(EXIT_SUCCESS);
}

#endif

#endif
