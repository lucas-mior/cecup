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

#include <ftw.h>
#include <gtk/gtk.h>
#include <ctype.h>
#include <sys/wait.h>
#include <dirent.h>
#include <poll.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <fnmatch.h>

#include "cecup.h"
#include "util.c"
#include "ipc.c"

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_work 1
#elif !defined(TESTING_work)
#define TESTING_work 0
#endif

static Message *add_row_batch_messages[BATCH_SIZE];
static int32 add_row_batch_count = 0;
static __thread int64 nftw_file_count = 0;

#define HASH_VALUE_TYPE int32
#define HASH_VALUE_FORMATTER "%d"
#define HASH_PADDING_TYPE uint32
#define HASH_DUPLICATE_KEYS 1
#define HASH_TYPE fs_map
#include "hash.c"

typedef struct FixFsThreadData {
    char *base_path;
    int32 base_path_len;
    int64 file_count;

    struct Hash_fs_map *map;
    struct Hash_fs_map *inode_map;

    char **ignore_patterns;
    int32 ignore_count;

    int32 array_capacity;
    int32 array_count;

    struct stat *stat_array;
    char **matched_pattern_array;
    char **link_target_array;
    char **relative_path_array;
} FixFsThreadData;

static __thread FixFsThreadData *nftw_current_data = NULL;

static void
work_flush_add_rows(void) {
    MessageBatch *batch;
    int64 messages_size;

    if (add_row_batch_count == 0) {
        return;
    }

    messages_size = add_row_batch_count*SIZEOF(Message *);
    batch = xmalloc(SIZEOF(*batch) + messages_size);
    batch->type = DATA_TYPE_ADD_ROW;
    batch->count = add_row_batch_count;
    memcpy64(batch->messages, add_row_batch_messages, messages_size);

    g_idle_add(update_ui_handler, batch);
    add_row_batch_count = 0;
    return;
}

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
work_finalize(ThreadData *thread_data) {
    Message *message = xmalloc(SIZEOF(*message));
    memset64(message, 0, SIZEOF(*message));
    message->type = DATA_TYPE_ENABLE_BUTTONS;

    work_flush_add_rows();

    ipc_send_progress(DATA_TYPE_PROGRESS_PREVIEW, 1.0);

    if (thread_data) {
        if (thread_data->filtered && thread_data->relative_new) {
            int32 focus_length = strlen32(thread_data->relative_new);

            message->focus_len = focus_length;
            message->path_to_focus = xmalloc(focus_length + 1);
            memcpy64(message->path_to_focus, thread_data->relative_new,
                     focus_length + 1);
        }

        if (thread_data->relative_new) {
            XFREE(thread_data->relative_new);
        }
        if (thread_data->relative_old) {
            XFREE(thread_data->relative_old);
        }
        XFREE(thread_data);
    }

    g_idle_add(update_ui_handler, message);
    return;
}

static void
work_add_row(enum CecupAction action, enum CecupReason reason,
             char *src_path, char *dst_path,
             char *link_target, char *ignore_pattern,
             int32 path_len,
             int64 src_size_raw, int64 src_mtime_raw,
             int64 dst_size_raw, int64 dst_mtime_raw,
             bool delete_excluded, bool is_dir) {
    int32 target_len;
    int32 pattern_len;
    Message *message = xmalloc(SIZEOF(*message));
    memset64(message, 0, SIZEOF(*message));

    message->type = DATA_TYPE_ADD_ROW;
    message->action = action;
    message->reason = reason;
    message->path_len = path_len;
    message->src_size = src_size_raw;
    message->src_mtime = src_mtime_raw;
    message->dst_size = dst_size_raw;
    message->dst_mtime = dst_mtime_raw;
    message->delete_excluded = delete_excluded;
    message->is_dir = is_dir;

    if (src_path) {
        message->src_path = xmalloc(path_len + 1);
        memcpy64(message->src_path, src_path, path_len + 1);
    }

    if (dst_path) {
        message->dst_path = xmalloc(path_len + 1);
        memcpy64(message->dst_path, dst_path, path_len + 1);
    }

    if (link_target) {
        target_len = strlen32(link_target);
        message->link_target_len = target_len;
        message->link_target = xmalloc(target_len + 1);
        memcpy64(message->link_target, link_target, target_len + 1);
    }

    if (ignore_pattern) {
        pattern_len = strlen32(ignore_pattern);
        message->ignore_pattern_len = pattern_len;
        message->ignore_pattern = xmalloc(pattern_len + 1);
        memcpy64(message->ignore_pattern, ignore_pattern, pattern_len + 1);
    }

    add_row_batch_messages[add_row_batch_count] = message;
    add_row_batch_count += 1;

    if (add_row_batch_count >= BATCH_SIZE) {
        work_flush_add_rows();
    }
    return;
}

static void
work_load_ignore_patterns(char ***patterns, int32 *count) {
    FILE *file;
    char line_buffer[MAX_PATH_LENGTH];
    int32 length;

    *patterns = NULL;
    *count = 0;

    if ((file = fopen(cecup.ignore_path, "r")) == NULL) {
        return;
    }

    while (fgets(line_buffer, SIZEOF(line_buffer), file)) {
        length = strlen32(line_buffer);

        if (length > 0 && line_buffer[length - 1] == '\n') {
            line_buffer[length - 1] = '\0';
            length -= 1;
        }

        if (length == 0) {
            continue;
        }

        if (line_buffer[0] == '#') {
            continue;
        }

        *patterns = xrealloc(*patterns, (*count + 1) * SIZEOF(char *));
        (*patterns)[*count] = xstrdup(line_buffer);
        *count += 1;
    }

    fclose(file);
    return;
}

static char *
work_path_matches_ignore(char *relative_path, bool is_dir,
                         char **patterns, int32 count) {
    if (patterns == NULL) {
        return NULL;
    }

    for (int32 i = 0; i < count; i += 1) {
        char *pattern = patterns[i];
        char pattern_adapt_buffer[MAX_PATH_LENGTH];
        char *pattern_final;

        int32 pattern_len;
        bool dir_only = false;
        bool has_slash = false;
        char path_copy[MAX_PATH_LENGTH];
        int32 path_len;
        bool matched = false;

        if (pattern == NULL) {
            continue;
        }

        if ((pattern_len  = strlen32(pattern)) <= 0) {
            continue;
        }

        if (pattern_len >= MAX_PATH_LENGTH) {
            continue;
        }

        memcpy64(pattern_adapt_buffer, pattern, pattern_len + 1);

        if (pattern_adapt_buffer[pattern_len - 1] == '/') {
            dir_only = true;
            pattern_adapt_buffer[pattern_len - 1] = '\0';
            pattern_len -= 1;
        }

        if (pattern_len == 0) {
            continue;
        }

        pattern_final = pattern_adapt_buffer;

        if (pattern_adapt_buffer[0] == '/') {
            has_slash = true;
            pattern_final += 1;
        } else {
            if (strchr(pattern_adapt_buffer, '/') != NULL) {
                has_slash = true;
            }
        }

        if (has_slash) {
            path_len = strlen32(relative_path);
            memcpy64(path_copy, relative_path, path_len + 1);

            if (fnmatch(pattern_final, path_copy, FNM_PATHNAME) == 0) {
                if (!dir_only) {
                    matched = true;
                } else {
                    if (is_dir) {
                        matched = true;
                    }
                }
            }

            if (!matched) {
                for (int32 j = 0; j < path_len; j += 1) {
                    if (path_copy[j] == '/') {
                        path_copy[j] = '\0';
                        if (fnmatch(pattern_final, path_copy, FNM_PATHNAME) == 0) {
                            matched = true;
                            break;
                        }
                        path_copy[j] = '/';
                    }
                }
            }

            if (matched) {
                return pattern;
            }
        } else {
            char *saveptr;
            char *comp;

            memcpy64(path_copy, relative_path, strlen32(relative_path) + 1);

            if ((comp = strtok_r(path_copy, "/", &saveptr))) {
                bool keep_running = true;

                while (keep_running) {
                    char *next;
                    bool is_leaf;
                    bool comp_is_dir;

                    next = strtok_r(NULL, "/", &saveptr);

                    if (next == NULL) {
                        is_leaf = true;
                    } else {
                        is_leaf = false;
                    }

                    if (is_leaf) {
                        comp_is_dir = is_dir;
                    } else {
                        comp_is_dir = true;
                    }

                    if (!dir_only) {
                        if (fnmatch(pattern_final, comp, 0) == 0) {
                            matched = true;
                            break;
                        }
                    } else {
                        if (comp_is_dir) {
                            if (fnmatch(pattern_final, comp, 0) == 0) {
                                matched = true;
                                break;
                            }
                        }
                    }

                    comp = next;
                    if (comp == NULL) {
                        keep_running = false;
                    }
                }
            }

            if (matched) {
                return pattern;
            }
        }
    }

    return NULL;
}

static int
work_fix_fs_cb(const char *fpath,
               const struct stat *sb, int typeflag, struct FTW *ftwbuf) {
    char *d_name;
    int64 name_len;
    int32 old_full_len;
    bool changed;
    bool renaming_problematic;
    char *relative_path;
    int32 relative_len;
    bool is_dir;
    int32 index;
    FixFsThreadData *data;

    if (cecup.stop_working) {
        return 1;
    }

    data = nftw_current_data;
    d_name = (char *)fpath + ftwbuf->base;
    name_len = strlen32(d_name);
    old_full_len = strlen32((char *)fpath);

    if (old_full_len >= (MAX_PATH_LENGTH / 2)) {
        IPC_SEND_LOG_ERROR(_("Error: file path is too long:\n"));
        IPC_SEND_LOG_ERROR("%s\n", fpath);
        IPC_SEND_LOG_ERROR(_("Please fix your file system.\n"));
        cecup.stop_working = true;
        return 1;
    }

    if (name_len > 0) {
        if (isspace(d_name[0])) {
            IPC_SEND_LOG_ERROR(_("Error: there is a space in the start of the fileneme:\n"));
            IPC_SEND_LOG_ERROR("'%s'\n", fpath);
            IPC_SEND_LOG_ERROR(_("Please fix your file system.\n"));
            cecup.stop_working = true;
            return 1;
        }

        if (isspace(d_name[name_len - 1])) {
            IPC_SEND_LOG_ERROR(_("Error: there is space in the end of the fileneme:\n"));
            IPC_SEND_LOG_ERROR("'%s'\n", fpath);
            IPC_SEND_LOG_ERROR(_("Please fix your file system.\n"));
            cecup.stop_working = true;
            return 1;
        }
    }

    changed = false;
    renaming_problematic = false;

    if (renaming_problematic) {
        char new_name[MAX_PATH_LENGTH];
        char new_full[MAX_PATH_LENGTH];
        int32 j = 0;
        int32 k = 0;

        while (k < name_len) {
            char *earliest_match = NULL;
            int32 replacement_index = -1;

            for (int32 ri = 0; ri < (int32)LENGTH(replacements); ri += 1) {
                char *search = replacements[ri].problem;
                int64 search_len = strlen32(search);
                char *match;

                if ((match = memmem64(&d_name[k], name_len - k,
                                      search, search_len))) {
                    if (earliest_match == NULL || match < earliest_match) {
                        earliest_match = match;
                        replacement_index = ri;
                    }
                }
            }

            if (earliest_match) {
                int64 prefix_len = earliest_match - &d_name[k];
                char *replace_str = replacements[replacement_index].rename;
                int64 replace_len = strlen32(replace_str);

                if (prefix_len > 0) {
                    memcpy64(&new_name[j], &d_name[k], prefix_len);
                    j += (int32)prefix_len;
                    k += (int32)prefix_len;
                }

                memcpy64(&new_name[j], replace_str, replace_len);
                j += (int32)replace_len;
                k += (int32)strlen32(replacements[replacement_index].problem);
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
            int32 base_len = ftwbuf->base;

            memcpy64(new_full, (char *)fpath, base_len);
            memcpy64(new_full + base_len, new_name, j + 1);

            if (renameat2(AT_FDCWD, fpath,
                          AT_FDCWD, new_full,
                          RENAME_NOREPLACE) < 0) {
                IPC_SEND_LOG_ERROR(_("Error renaming %s to %s: %s\n"),
                                   fpath, new_full, strerror(errno));
            } else {
                IPC_SEND_LOG(_("Fixed: %s -> %s\n"), d_name, new_name);
            }
        }
    }

    if (typeflag != FTW_D && typeflag != FTW_DP) {
        nftw_file_count += 1;
    }

    relative_path = (char *)fpath + data->base_path_len;
    if (relative_path[0] == '/') {
        relative_path += 1;
    }
    relative_len = strlen32(relative_path);
    is_dir = (typeflag == FTW_D || typeflag == FTW_DP);

    if (data->array_count >= data->array_capacity) {
        if (data->array_capacity == 0) {
            data->array_capacity = 1024;
        } else {
            data->array_capacity *= 2;
        }
        data->stat_array = xrealloc(data->stat_array,
                                    data->array_capacity*SIZEOF(struct stat));
        data->matched_pattern_array = xrealloc(data->matched_pattern_array,
                                    data->array_capacity*SIZEOF(char *));
        data->link_target_array = xrealloc(data->link_target_array,
                                           data->array_capacity*SIZEOF(char *));
        data->relative_path_array = xrealloc(data->relative_path_array,
                                             data->array_capacity*SIZEOF(char *));
    }

    index = data->array_count;
    data->array_count += 1;

    memset64(&data->stat_array[index], 0, SIZEOF(struct stat));
    memcpy64(&data->stat_array[index], (void *)sb, SIZEOF(struct stat));

    if (relative_len == 0) {
        data->relative_path_array[index] = xstrdup("./");
    } else {
        data->relative_path_array[index] = xstrdup(relative_path);
    }
    data->link_target_array[index] = NULL;

    if (typeflag == FTW_SL) {
        char target[MAX_PATH_LENGTH];
        int64 len;

        len = (int64)readlink(fpath, target, SIZEOF(target) - 1);
        if (len != -1) {
            target[len] = '\0';
            data->link_target_array[index] = xstrdup(target);
        }
    } else if (typeflag == FTW_F && sb->st_nlink > 1) {
        char inode_str[64];
        int32 *first_idx_ptr;

        SNPRINTF(inode_str, "%llu", (unsigned long long)sb->st_ino);
        first_idx_ptr = hash_lookup2_fs_map(data->inode_map, inode_str);
        if (first_idx_ptr) {
            data->link_target_array[index] = xstrdup(data->relative_path_array[*first_idx_ptr]);
        } else {
            hash_insert2_fs_map(data->inode_map, inode_str, index);
        }
    }

    data->matched_pattern_array[index] = work_path_matches_ignore(relative_path, is_dir,
                                                                  data->ignore_patterns,
                                                                  data->ignore_count);
    if (relative_len == 0) {
        hash_insert2_fs_map(data->map, "./", index);
    } else {
        hash_insert2_fs_map(data->map, relative_path, index);
    }

    return 0;
}

static int64
work_fix_fs_recursive(FixFsThreadData *data) {
    if (cecup.stop_working) {
        return 0;
    }

    nftw_file_count = 0;
    nftw_current_data = data;

    if (nftw(data->base_path, work_fix_fs_cb, 64, FTW_PHYS | FTW_DEPTH) != 0) {
        if (cecup.stop_working == false) {
            error(_("Error walking directory %s: %s.\n"), data->base_path,
                  strerror(errno));
        }
    }

    return nftw_file_count;
}

static void *
work_fix_fs_thread_fn(void *user_data) {
    FixFsThreadData *data = user_data;
    data->file_count = work_fix_fs_recursive(data);
    return NULL;
}

static void
work_destroy_fs_map(struct Hash_fs_map *map) {
    hash_destroy_fs_map(map);
    return;
}

static void *
work_rsync(void *user_data) {
    ThreadData *thread_data = user_data;
    int64 nfiles_total = 0;
    int64 nfiles_processed = 0;

    int32 pipe_stdout[2];
    int32 pipe_stderr[2];
    struct pollfd pipes[2];
    pid_t child_pid;

    int64 buf_output_pos = 0;
    char *eol;
    char buf_output[MAX_PATH_LENGTH*2];
    char buf_error[MAX_PATH_LENGTH*2];

    char src_base_with_slash[MAX_PATH_LENGTH];
    char dst_base_with_slash[MAX_PATH_LENGTH];
    char *rsync_args[64];
    int32 a = 0;
    char cmd[MAX_PATH_LENGTH*2];

    char **transfers = NULL;
    int32 ntransfers = 0;
    int32 transfers_capacity = 0;
    char files_from_filename[] = "/tmp/cecup_XXXXXX";
    struct timespec t0_work;
    struct timespec t1_work;

    bool same_fs = true;
    char **ignore_patterns = NULL;
    int32 ignore_count = 0;

    struct Hash_fs_map *src_map = hash_create_fs_map(1024);
    struct Hash_fs_map *dst_map = hash_create_fs_map(1024);
    struct Hash_fs_map *src_inode_map = hash_create_fs_map(1024);
    struct Hash_fs_map *dst_inode_map = hash_create_fs_map(1024);

    FixFsThreadData src_fix;
    FixFsThreadData dst_fix;

    clock_gettime(CLOCK_MONOTONIC_RAW, &t0_work);

    memset64(&src_fix, 0, SIZEOF(src_fix));
    memset64(&dst_fix, 0, SIZEOF(dst_fix));

    {
        struct stat stat_src;
        struct stat stat_dst;

        if (stat(cecup.src_base, &stat_src) < 0) {
            IPC_SEND_LOG_ERROR("Error getting directory info from %s: %s.\n",
                               cecup.src_base, strerror(errno));
            goto cleanup_maps;
        }
        if (stat(cecup.dst_base, &stat_dst) < 0) {
            IPC_SEND_LOG_ERROR("Error getting directory info from %s: %s.\n",
                               cecup.dst_base, strerror(errno));
            goto cleanup_maps;
        }

        same_fs = (stat_src.st_dev == stat_dst.st_dev);
    }

    if (thread_data->check_different_fs && same_fs) {
        Message *message = xmalloc(SIZEOF(*message));
        memset64(message, 0, SIZEOF(*message));

        message->type = DATA_TYPE_CLEAR_TREES;
        g_idle_add_full(G_PRIORITY_HIGH_IDLE, update_ui_handler, message, NULL);

        IPC_SEND_LOG_ERROR(
            _("Safety stop: Original and backup are on the same storage "
              "device.\n"
              "Check if the backup device is connected.\n"
              "To force backup on a folder in the same device, uncheck"
              " option \"Protect same drive sync\".\n"));

        work_finalize(thread_data);
        goto cleanup_maps;
    }

    work_load_ignore_patterns(&ignore_patterns, &ignore_count);

    src_fix.base_path = cecup.src_base;
    src_fix.base_path_len = cecup.src_base_len;
    src_fix.map = src_map;
    src_fix.inode_map = src_inode_map;
    src_fix.ignore_patterns = ignore_patterns;
    src_fix.ignore_count = ignore_count;

    dst_fix.base_path = cecup.dst_base;
    dst_fix.base_path_len = cecup.dst_base_len;
    dst_fix.map = dst_map;
    dst_fix.inode_map = dst_inode_map;
    dst_fix.ignore_patterns = ignore_patterns;
    dst_fix.ignore_count = ignore_count;

    IPC_SEND_LOG(_("Traversing file systems...\n"));
    if (!same_fs) {
        GThread *t1 = g_thread_new("fix_src",
                                   work_fix_fs_thread_fn, &src_fix);
        GThread *t2 = g_thread_new("fix_dst",
                                   work_fix_fs_thread_fn, &dst_fix);
        g_thread_join(t1);
        g_thread_join(t2);
    } else {
        work_fix_fs_thread_fn(&src_fix);
        work_fix_fs_thread_fn(&dst_fix);
    }

    if (cecup.stop_working) {
        IPC_SEND_LOG_ERROR(_("Stop requested.\n"));
        work_finalize(thread_data);
        goto cleanup_maps;
    }

    IPC_SEND_LOG(_("File system traversal finished.\n"));

    nfiles_total = MAX(src_fix.file_count, dst_fix.file_count);
    IPC_SEND_LOG(_("Found %lld files to analyse...\n"),
                 (llong)nfiles_total);

    if (thread_data->is_preview) {
        Message *message = xmalloc(SIZEOF(*message));
        memset64(message, 0, SIZEOF(*message));

        message->type = DATA_TYPE_CLEAR_TREES;
        g_idle_add_full(G_PRIORITY_HIGH_IDLE, update_ui_handler, message, NULL);

        for (uint32 i = 0; i < src_map->capacity; i += 1) {
            Bucket_fs_map *bucket_src = &src_map->array[i];
            int32 src_idx;
            int32 *dst_idx_ptr;
            struct stat *stat_src;
            char *matched_pattern_src;
            bool ignored_src;
            char *link_target_src;
            bool is_symlink;
            bool is_hardlink;
            bool is_dir;
            enum CecupAction action;
            enum CecupReason reason;
            char *src_path = NULL;
            char *dst_path = NULL;
            int64 dst_size = 0;
            int64 dst_mtime = 0;

            if ((int64)bucket_src->key <= 0) {
                continue;
            }

            src_idx = bucket_src->value;
            stat_src = &src_fix.stat_array[src_idx];
            matched_pattern_src = src_fix.matched_pattern_array[src_idx];
            ignored_src = matched_pattern_src;
            link_target_src = src_fix.link_target_array[src_idx];
            src_path = bucket_src->key;

            is_symlink = S_ISLNK(stat_src->st_mode);
            is_hardlink = (S_ISREG(stat_src->st_mode) && link_target_src != NULL);
            is_dir = S_ISDIR(stat_src->st_mode);

            if ((dst_idx_ptr = hash_lookup2_fs_map(dst_map, bucket_src->key))) {
                int32 dst_idx = *dst_idx_ptr;
                struct stat *stat_dst = &dst_fix.stat_array[dst_idx];
                char *link_target_dst = dst_fix.link_target_array[dst_idx];

                dst_path = src_path;
                dst_size = stat_dst->st_size;
                dst_mtime = stat_dst->st_mtime;
                reason = 0;

                if (ignored_src) {
                    action = ACTION_IGNORE;
                    reason |= REASON_IGNORED;
                } else {
                    bool equal;
                    bool attributes_differ;

                    equal = false;
                    attributes_differ = false;

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
                        if (stat_src->st_uid != stat_dst->st_uid) {
                            reason |= REASON_OWNER;
                            attributes_differ = true;
                        }
                        if (stat_src->st_gid != stat_dst->st_gid) {
                            reason |= REASON_GROUP;
                            attributes_differ = true;
                        }
                        if ((stat_src->st_mode & 07777) != (stat_dst->st_mode & 07777)) {
                            reason |= REASON_PERM;
                            attributes_differ = true;
                        }

                        if (is_hardlink) {
                            if (S_ISREG(stat_dst->st_mode)
                                && link_target_dst
                                && !strcmp(link_target_src, link_target_dst)
                                && !attributes_differ) {
                                equal = true;
                            }
                        } else {
                            if (!attributes_differ) {
                                equal = true;
                            }
                        }
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
                if (ignored_src) {
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

            if ((action != ACTION_EQUAL) && (action != ACTION_IGNORE)) {
                if (ntransfers >= transfers_capacity) {
                    if (transfers_capacity == 0) {
                        transfers_capacity = 256;
                    } else {
                        transfers_capacity *= 2;
                    }
                    transfers = xrealloc(transfers,
                                         transfers_capacity*SIZEOF(*transfers));
                }
                transfers[ntransfers] = xstrdup(bucket_src->key);
                ntransfers += 1;
            }

            work_add_row(action, reason,
                         bucket_src->key, dst_path,
                         link_target_src, matched_pattern_src,
                         strlen32(bucket_src->key),
                         stat_src->st_size, stat_src->st_mtime,
                         dst_size, dst_mtime,
                         thread_data->delete_excluded,
                         S_ISDIR(stat_src->st_mode));

            nfiles_processed += 1;
            if ((nfiles_processed % 1000) == 0) {
                ipc_send_progress(DATA_TYPE_PROGRESS_PREVIEW,
                                  (double)nfiles_processed
                                  / (double)nfiles_total);
            }
        }

        for (uint32 i = 0; i < dst_map->capacity; i += 1) {
            Bucket_fs_map *bucket_dst = &dst_map->array[i];
            int32 dst_idx;
            struct stat *stat_dst;
            char *matched_pattern_dst;
            bool ignored_dst;
            char *link_target_dst;
            enum CecupAction action = ACTION_DELETE;
            enum CecupReason reason = 0;

            if ((int64)bucket_dst->key <= 0) {
                continue;
            }

            if (hash_lookup2_fs_map(src_map, bucket_dst->key) == NULL) {
                dst_idx = bucket_dst->value;
                stat_dst = &dst_fix.stat_array[dst_idx];
                matched_pattern_dst = dst_fix.matched_pattern_array[dst_idx];
                ignored_dst = matched_pattern_dst;
                link_target_dst = dst_fix.link_target_array[dst_idx];

                if (ignored_dst) {
                    if (!thread_data->delete_excluded) {
                        action = ACTION_IGNORE;
                    }
                    reason |= REASON_IGNORED;
                }

                reason |= REASON_MISSING;

                work_add_row(action, reason,
                             NULL, bucket_dst->key,
                             link_target_dst, matched_pattern_dst,
                             strlen32(bucket_dst->key),
                             0, 0,
                             stat_dst->st_size, stat_dst->st_mtime,
                             thread_data->delete_excluded,
                             S_ISDIR(stat_dst->st_mode));
            }
            nfiles_processed += 1;
            if ((nfiles_processed % 1000) == 0) {
                ipc_send_progress(DATA_TYPE_PROGRESS_PREVIEW,
                                  (double)nfiles_processed
                                  / (double)nfiles_total);
            }
        }
    }

    clock_gettime(CLOCK_MONOTONIC_RAW, &t1_work);
    PRINT_TIMINGS(nfiles_total, t0_work, t1_work);

    if (thread_data->is_preview || (ntransfers <= 0)) {
        work_finalize(thread_data);
        goto cleanup_maps;
    }

    {
        int files_from_fd;
        if ((files_from_fd = mkstemp(files_from_filename)) < 0) {
            error("Error in mkstemp: %s.\n", strerror(errno));
            fatal(EXIT_FAILURE);
        }
        for (int32 i = 0; i < ntransfers; i += 1) {
            char *file = transfers[i];
            int64 w;
            int64 written = 0;
            int64 left = strlen32(file);

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
        XCLOSE(&files_from_fd);
    }

    SNPRINTF(src_base_with_slash, "%s/", cecup.src_base);
    SNPRINTF(dst_base_with_slash, "%s/", cecup.dst_base);

    a = 0;
    rsync_args[a++] = "rsync";
    rsync_args[a++] = "--verbose";
    rsync_args[a++] = "--dirs";
    rsync_args[a++] = "--partial";
    rsync_args[a++] = "--progress";
    rsync_args[a++] = "--info=progress2";
    rsync_args[a++] = "--checksum";
    rsync_args[a++] = "--perms";
    rsync_args[a++] = "--times";
    rsync_args[a++] = "--owner";
    rsync_args[a++] = "--group";
    rsync_args[a++] = "--files-from";
    rsync_args[a++] = files_from_filename;
    rsync_args[a++] = "--iconv=.,.";
    rsync_args[a++] = src_base_with_slash;
    rsync_args[a++] = dst_base_with_slash;
    rsync_args[a++] = NULL;

    IPC_SEND_LOG(_("Verifying and syncing with checksum...\n"));
    STRING_FROM_ARRAY(cmd, " ", rsync_args, a);
    IPC_SEND_LOG_CMD("%s\n", cmd);

    xpipe(pipe_stdout);
    xpipe(pipe_stderr);

    switch (child_pid = fork()) {
    case -1:
        error("Error forking: %s.\n", strerror(errno));
        fatal(EXIT_FAILURE);
    case 0:
        setpgid(0, 0);
        putenv("LC_ALL=C.UTF-8");

        XCLOSE(&pipe_stderr[0]);
        XCLOSE(&pipe_stdout[0]);

        xdup2(pipe_stdout[1], STDOUT_FILENO);
        xdup2(pipe_stderr[1], STDERR_FILENO);

        XCLOSE(&pipe_stderr[1]);
        XCLOSE(&pipe_stdout[1]);

        execvp("rsync", rsync_args);
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
                IPC_SEND_LOG_ERROR("Error reading stdout pipe: %s.\n",
                                   strerror(errno));
                pipes[0].fd = -1;
            }
            goto read_error_pipe;
        }
        buf_output_pos += (int64)r;

        while ((eol = memchr64(buf_output, '\n', buf_output_pos))
                || (eol = memchr64(buf_output, '\r', buf_output_pos))) {
            int64 line_len = (int64)(eol - buf_output);
            int64 remaining;
            char *filename;

            *eol = '\0';
            if ((filename = work_check_itemize_line(buf_output))) {
                int32 path_len;
                char *sep;
                Message *msg = xmalloc(SIZEOF(*msg));

                memset64(msg, 0, SIZEOF(*msg));
                while (*filename == ' ') {
                    filename += 1;
                }
                path_len = (int32)(eol - filename);
                if ((sep = memmem64(filename, path_len, RSYNC_HARDLINK_NOTATION, strlen32(RSYNC_HARDLINK_NOTATION)))) {
                    *sep = '\0';
                    path_len = (int32)(sep - filename);
                } else if ((sep = memmem64(filename, path_len, RSYNC_SYMLINK_NOTATION, strlen32(RSYNC_SYMLINK_NOTATION)))) {
                    *sep = '\0';
                    path_len = (int32)(sep - filename);
                }

                if (path_len == 1) {
                    if (filename[0] == '.') {
                        filename = "./";
                        path_len = 2;
                    }
                }

                msg->path_len = path_len;
                msg->src_path = xmalloc(path_len + 1);
                memcpy64(msg->src_path, filename, path_len + 1);
                msg->type = DATA_TYPE_REMOVE_ROW;
                g_idle_add(update_ui_handler, msg);
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
                IPC_SEND_LOG_ERROR("Error reading stderr pipe: %s.\n",
                                   strerror(errno));
                pipes[1].fd = -1;
            }
            continue;
        }
        buf_error[r] = '\0';
        IPC_SEND_LOG_ERROR("%s", buf_error);

    } while ((pipes[0].fd >= 0) || (pipes[1].fd >= 0));

    if (waitpid(child_pid, NULL, 0) < 0) {
        IPC_SEND_LOG_ERROR("Error waiting for rsync: %s.\n", strerror(errno));
    }
    xunlink(files_from_filename);
    cecup.child_pid = 0;
    XCLOSE(&pipe_stderr[0]);
    XCLOSE(&pipe_stdout[0]);

    for (int32 i = 0; i < ntransfers; i += 1) {
        XFREE(transfers[i]);
    }
    XFREE(transfers);

    work_finalize(thread_data);

cleanup_maps:
    for (int32 i = 0; i < ignore_count; i += 1) {
        XFREE(ignore_patterns[i]);
    }
    if (ignore_patterns) {
        XFREE(ignore_patterns);
    }

    work_destroy_fs_map(src_map);
    work_destroy_fs_map(dst_map);

    if (src_inode_map) {
        hash_destroy_fs_map(src_inode_map);
    }
    if (dst_inode_map) {
        hash_destroy_fs_map(dst_inode_map);
    }

    if (src_fix.stat_array) {
        XFREE(src_fix.stat_array);
    }
    if (src_fix.matched_pattern_array) {
        XFREE(src_fix.matched_pattern_array);
    }
    if (src_fix.link_target_array) {
        for (int32 i = 0; i < src_fix.array_count; i += 1) {
            if (src_fix.link_target_array[i]) {
                XFREE(src_fix.link_target_array[i]);
            }
            if (src_fix.relative_path_array[i]) {
                XFREE(src_fix.relative_path_array[i]);
            }
        }
        XFREE(src_fix.link_target_array);
        XFREE(src_fix.relative_path_array);
    }

    if (dst_fix.stat_array) {
        XFREE(dst_fix.stat_array);
    }
    if (dst_fix.matched_pattern_array) {
        XFREE(dst_fix.matched_pattern_array);
    }
    if (dst_fix.link_target_array) {
        for (int32 i = 0; i < dst_fix.array_count; i += 1) {
            if (dst_fix.link_target_array[i]) {
                XFREE(dst_fix.link_target_array[i]);
            }
            if (dst_fix.relative_path_array[i]) {
                XFREE(dst_fix.relative_path_array[i]);
            }
        }
        XFREE(dst_fix.link_target_array);
        XFREE(dst_fix.relative_path_array);
    }

    g_thread_exit(NULL);
}

static void *
work_rsync_bulk(void *user_data) {
    TaskList *tasks = user_data;
    bool has_transfers = false;
    int32 pipe_stdout[2];
    int32 pipe_stderr[2];
    struct pollfd pipes[2];
    pid_t child_pid;
    char dst_base_with_slash[MAX_PATH_LENGTH];
    char *rsync_args[32];
    int32 a = 0;
    int32 buf_output_pos = 0;
    char files_from_filename[] = "/tmp/cecup_XXXXXX";
    int files_from_fd;
    char cmd[MAX_PATH_LENGTH*2];

    for (int32 i = 0; i < tasks->count; i += 1) {
        Task *task = tasks->items[i];
        char full_path[MAX_PATH_LENGTH];
        pid_t child_rm;
        int child_status;
        bool removed = false;

        if (task->action != ACTION_DELETE) {
            has_transfers = true;
            continue;
        }

        if (strcmp(task->path, "./") == 0) {
            Message *message = xmalloc(SIZEOF(*message));
            memset64(message, 0, SIZEOF(*message));

            message->path_len = task->path_len;
            message->src_path = xmalloc(task->path_len + 1);
            memcpy64(message->src_path, task->path, task->path_len + 1);

            message->type = DATA_TYPE_REMOVE_ROW;
            g_idle_add(update_ui_handler, message);
            continue;
        }

        if (cecup.stop_working) {
            IPC_SEND_LOG_ERROR(_("Stop requested.\n"));
            free_task_list(tasks);
            work_finalize(NULL);
            g_thread_exit(NULL);
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
                "-rf",
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
                IPC_SEND_LOG_ERROR("Error waiting for child: %s.\n",
                                   strerror(errno));
            } else if (WIFEXITED(child_status)) {
                removed = !WEXITSTATUS(child_status);
            }
            cecup.child_pid = 0;
            break;
        }

        if (removed) {
            Message *message = xmalloc(SIZEOF(*message));
            memset64(message, 0, SIZEOF(*message));

            message->path_len = task->path_len;
            message->src_path = xmalloc(task->path_len + 1);
            memcpy64(message->src_path, task->path, task->path_len + 1);

            message->type = DATA_TYPE_REMOVE_ROW;
            g_idle_add(update_ui_handler, message);
        }
    }

    if (!has_transfers) {
        work_finalize(NULL);
        free_task_list(tasks);
        g_thread_exit(NULL);
    }

    xpipe(pipe_stdout);
    xpipe(pipe_stderr);

    SNPRINTF(dst_base_with_slash, "%s/", cecup.dst_base);

    if ((files_from_fd = mkstemp(files_from_filename)) < 0) {
        error("Error in mkstemp: %s.\n", strerror(errno));
        fatal(EXIT_FAILURE);
    }

    for (int32 i = 0; i < tasks->count; i += 1) {
        Task *task = tasks->items[i];
        switch (task->action) {
        case ACTION_DELETE:
        case ACTION_DELETED:
        case ACTION_IGNORE:
        case ACTION_EQUAL:
            continue;
        case ACTION_HARDLINK:
            write64(files_from_fd, task->link_target, task->link_target_len);
            write64(files_from_fd, "\n", 1);
            __attribute__((fallthrough));
        case ACTION_NEW:
        case ACTION_UPDATE:
        case ACTION_SYMLINK:
        case ACTION_LAST:
        default:
            write64(files_from_fd, task->path, task->path_len);
            write64(files_from_fd, "\n", 1);
        }
    }
    XCLOSE(&files_from_fd);

    rsync_args[a++] = "rsync";
    rsync_args[a++] = "--verbose";
    rsync_args[a++] = "--update";
    rsync_args[a++] = "--checksum";
    rsync_args[a++] = "--dirs";
    rsync_args[a++] = "--partial";
    rsync_args[a++] = "--progress";
    rsync_args[a++] = "--info=progress2";
    rsync_args[a++] = "--links";
    rsync_args[a++] = "--hard-links";
    rsync_args[a++] = "--perms";
    rsync_args[a++] = "--times";
    rsync_args[a++] = "--owner";
    rsync_args[a++] = "--group";
    rsync_args[a++] = "--files-from";
    rsync_args[a++] = files_from_filename;
    rsync_args[a++] = "--iconv=.,.";
    rsync_args[a++] = cecup.src_base;
    rsync_args[a++] = dst_base_with_slash;
    rsync_args[a++] = NULL;

    STRING_FROM_ARRAY(cmd, " ", rsync_args, a);
    IPC_SEND_LOG_CMD("%s\n", cmd);

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
        char buf_output_bulk[MAX_PATH_LENGTH*2];
        char buf_error_bulk[MAX_PATH_LENGTH*2];

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

        r = read64(pipe_stdout[0], buf_output_bulk + buf_output_pos,
                   SIZEOF(buf_output_bulk) - 1 - buf_output_pos);
        if (r <= 0) {
            if (r < 0) {
                IPC_SEND_LOG_ERROR("Error reading stdout pipe: %s.\n",
                                   strerror(errno));
                pipes[0].fd = -1;
            }
            goto read_error_pipe;
        }
        buf_output_pos += (int32)r;

        while ((eol = memchr64(buf_output_bulk, '\n', buf_output_pos))
               || (eol = memchr64(buf_output_bulk, '\r', buf_output_pos))) {
            int32 line_len = (int32)(eol - buf_output_bulk);
            int32 remaining;
            char *filename;

            *eol = '\0';

            IPC_SEND_LOG("%s\n", buf_output_bulk);

            if ((filename = work_check_itemize_line(buf_output_bulk))) {
                int32 path_len;
                char *sep;
                Message *message = xmalloc(SIZEOF(*message));
                memset64(message, 0, SIZEOF(*message));

                while (*filename == ' ') {
                    filename += 1;
                }

                path_len = (int32)(eol - filename);

                if ((sep = memmem64(filename, path_len,
                                    RSYNC_HARDLINK_NOTATION,
                                    strlen32(RSYNC_HARDLINK_NOTATION)))) {
                    *sep = '\0';
                    path_len = (int32)(sep - filename);
                } else if ((sep = memmem64(filename, path_len,
                                           RSYNC_SYMLINK_NOTATION,
                                           strlen32(RSYNC_SYMLINK_NOTATION)))) {
                    *sep = '\0';
                    path_len = (int32)(sep - filename);
                }

                if (path_len == 1) {
                    if (filename[0] == '.') {
                        filename = "./";
                        path_len = 2;
                    }
                }

                message->path_len = path_len;
                message->src_path = xmalloc(path_len + 1);
                memcpy64(message->src_path, filename, path_len + 1);

                message->type = DATA_TYPE_REMOVE_ROW;
                g_idle_add(update_ui_handler, message);
            }

            remaining = buf_output_pos - (line_len + 1);
            if (remaining > 0) {
                memmove64(buf_output_bulk, eol + 1, remaining);
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

        r = read64(pipe_stderr[0], buf_error_bulk, SIZEOF(buf_error_bulk) - 1);
        if (r <= 0) {
            if (r < 0) {
                IPC_SEND_LOG_ERROR("Error reading stderr pipe: %s.\n",
                                   strerror(errno));
                pipes[1].fd = -1;
            }
            continue;
        }
        buf_error_bulk[r] = '\0';
        IPC_SEND_LOG_ERROR("%s", buf_error_bulk);

    } while ((pipes[0].fd >= 0) || (pipes[1].fd >= 0));

    if (waitpid(child_pid, NULL, 0) < 0) {
        IPC_SEND_LOG_ERROR("Error waiting for child: %s.\n", strerror(errno));
    }
    xunlink(files_from_filename);
    cecup.child_pid = 0;

    XCLOSE(&pipe_stdout[0]);
    XCLOSE(&pipe_stderr[0]);

    work_finalize(NULL);
    free_task_list(tasks);
    if (cecup.stop_working) {
        IPC_SEND_LOG_ERROR(_("Stop requested.\n"));
    }
    g_thread_exit(NULL);
}

#if TESTING_work
#include "assert.c"
#include <string.h>
#include <stdio.h>
#include "aux.c"

int
main(void) {
    char *pattern;
    char *patterns[3];

    patterns[0] = "*.c";
    pattern = work_path_matches_ignore("main.c", false, patterns, 1);
    ASSERT_EQUAL(pattern, "*.c");

    patterns[0] = "build/";
    pattern = work_path_matches_ignore("build", true, patterns, 1);
    ASSERT_EQUAL(pattern, "build/");

    patterns[0] = "build/";
    pattern = work_path_matches_ignore("build", false, patterns, 1);
    ASSERT_NULL(pattern);

    patterns[0] = "obj";
    pattern = work_path_matches_ignore("src/obj/main.o", false, patterns, 1);
    ASSERT_EQUAL(pattern, "obj");

    patterns[0] = "/src";
    pattern = work_path_matches_ignore("src/main.c", false, patterns, 1);
    ASSERT_EQUAL(pattern, "/src");

    patterns[0] = "/src";
    pattern = work_path_matches_ignore("lib/src/main.c", false, patterns, 1);
    ASSERT_NULL(pattern);

    patterns[0] = "foo/bar";
    pattern = work_path_matches_ignore("foo/bar/baz.c", false, patterns, 1);
    ASSERT_EQUAL(pattern, "foo/bar");

    patterns[0] = "*.h";
    patterns[1] = "build/";
    patterns[2] = "*.o";
    pattern = work_path_matches_ignore("src/main.o", false, patterns, 3);
    ASSERT_EQUAL(pattern, "*.o");

    exit(EXIT_SUCCESS);
}

#endif

#endif
