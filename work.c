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
#include "ipc.c"

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_work 1
#elif !defined(TESTING_work)
#define TESTING_work 0
#endif

typedef struct FixFsThreadData {
    char *base_path;
    int64 file_count;
} FixFsThreadData;

#define HASH_VALUE_TYPE char*
#define HASH_VALUE_FORMATTER "%s"
#define HASH_PADDING_TYPE uint32
#define HASH_DUPLICATE_KEYS 1
#define HASH_TYPE map
#include "hash.c"

static bool
get_file_info(char *full_path, char **path, int64 *size, int64 *mtime,
              bool *is_dir) {
    struct stat stat;

    if (lstat(full_path, &stat) < 0) {
        if (errno != ENOENT) {
            error("Error in lstat(%s): %s.\n", full_path, strerror(errno));
        }
        *size = 0;
        *mtime = 0;
        *path = NULL;
        return false;
    } else {
        *size = stat.st_size;
        *mtime = (int64)stat.st_mtime;
        *is_dir = S_ISDIR(stat.st_mode);
        return true;
    }
}

static bool
check_itemize_line(char *buf_output) {
    switch (buf_output[0]) {
    case RSYNC_CHAR0_ACTION_SEND:
    case RSYNC_CHAR0_ACTION_RECEIVE:
    case RSYNC_CHAR0_ACTION_CHANGE:
    case RSYNC_CHAR0_ACTION_HARDLINK:
    case RSYNC_CHAR0_ACTION_NO_UPDATE:
        break;
    default:
        return false;
    }

    switch (buf_output[1]) {
    case RSYNC_CHAR1_TYPE_FILE:
    case RSYNC_CHAR1_TYPE_DIR:
    case RSYNC_CHAR1_TYPE_SYMLINK:
    case RSYNC_CHAR1_TYPE_DEVICE:
    case RSYNC_CHAR1_TYPE_SPECIAL:
        break;
    default:
        return false;
    }

    for (int i = 2; i < strlen32(RSYNC_ITEMIZE_PLACEHOLDERS); i += 1) {
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
            return false;
        }
    }

    if (buf_output[strlen32(RSYNC_ITEMIZE_PLACEHOLDERS)] != ' ') {
        return false;
    }

    return true;
}

static void
work_finalize(ThreadData *thread_data) {
    Message *message;

    ipc_send_progress(DATA_TYPE_PROGRESS_PREVIEW, 1.0);

    message = xmalloc(SIZEOF(*message));
    memset64(message, 0, SIZEOF(*message));

    message->type = DATA_TYPE_ENABLE_BUTTONS;

    if (thread_data) {
        if (thread_data->filtered && thread_data->relative_new) {
            int32 focus_length = strlen32(thread_data->relative_new);

            message->focus_len = focus_length;
            message->path_to_focus
                = xmalloc(focus_length + 1);
            memcpy64(message->path_to_focus, thread_data->relative_new,
                     focus_length + 1);
        }

        if (thread_data->relative_new) {
            free(thread_data->relative_new);
        }
        if (thread_data->relative_old) {
            free(thread_data->relative_old);
        }
        free(thread_data);
    }

    g_idle_add(update_ui_handler, message);
    return;
}

static bool
did_attribute_change(char *buf_output) {
    for (int32 i = 2; i < strlen32(RSYNC_ITEMIZE_PLACEHOLDERS); i += 1) {
        if ((buf_output[i] != '.') && (buf_output[i] != ' ')) {
            return true;
        }
    }
    return false;
}

static void
work_add_row(enum CecupAction action, enum CecupReason reason,
             char *src_path, char *dst_path,
             char *link_target, char *ignore_pattern,
             int64 src_size_raw, int64 src_mtime_raw,
             int64 dst_size_raw, int64 dst_mtime_raw,
             bool delete_excluded, bool is_dir,
             long *nfiles_processed, long nfiles_total) {
    CecupRow *row;
    char *final_src_path = NULL;
    char *final_dst_path = NULL;
    int32 path_len = 0;
    int32 slash = 0;

    if (src_path) {
        path_len = strlen32(src_path);

        if (is_dir) {
            slash = 1;
        }

        final_src_path = xarena_push(cecup.arena, path_len + slash + 1);
        memcpy64(final_src_path, src_path, path_len + 1);

        if (is_dir && (final_src_path[path_len - 1] != '/')) {
            final_src_path[path_len] = '/';
            final_src_path[path_len + 1] = '\0';
            path_len += 1;
        }

        if (dst_path) {
            final_dst_path = final_src_path;
        }
    } else if (dst_path) {
        path_len = strlen32(dst_path);
        if (is_dir) {
            slash = 1;
        }

        final_dst_path = xarena_push(cecup.arena, path_len + slash + 1);
        memcpy64(final_dst_path, dst_path, path_len + 1);
        if (is_dir && (final_dst_path[path_len - 1] != '/')) {
            final_dst_path[path_len] = '/';
            final_dst_path[path_len + 1] = '\0';
            path_len += 1;
        }
    } else {
        error("Error: both src_path and dst_path are NULL. "
              "(action=%u) (reason=%u)\n", action, reason);
        fatal(EXIT_FAILURE);
    }

    row = xarena_push(cecup.arena, SIZEOF(*row));
    memset64(row, 0, SIZEOF(*row));

    row->src_action = action;
    row->dst_action = action;
    row->reason = reason;

    switch (action) {
    case ACTION_IGNORE:
        row->src_action = ACTION_IGNORE;
        if (final_dst_path) {
            if (delete_excluded) {
                row->dst_action = ACTION_DELETE;
            } else {
                row->dst_action = ACTION_IGNORE;
            }
        } else {
            row->dst_action = ACTION_IGNORE;
        }
        break;
    case ACTION_DELETE:
        row->dst_action = ACTION_DELETE;
        row->src_action = ACTION_IGNORE;
        break;
    case ACTION_DELETED:
    case ACTION_EQUAL:
    case ACTION_HARDLINK:
    case ACTION_SYMLINK:
    case ACTION_NEW:
    case ACTION_UPDATE:
    default:
        break;
    }

    bytes_pretty(row->src_size_text, src_size_raw);
    bytes_pretty(row->dst_size_text, dst_size_raw);
    row->src_size_raw = src_size_raw;
    row->dst_size_raw = dst_size_raw;

    if (src_mtime_raw > 0) {
        time_t t = (time_t)src_mtime_raw;
        struct tm *tm_info = localtime(&t);
        STRFTIME(row->src_mtime_text, "%Y-%m-%d %H:%M:%S", tm_info);
        row->src_mtime_raw = src_mtime_raw;
    }

    if (dst_mtime_raw > 0) {
        time_t t = (time_t)dst_mtime_raw;
        struct tm *tm_info = localtime(&t);
        STRFTIME(row->dst_mtime_text, "%Y-%m-%d %H:%M:%S", tm_info);
        row->dst_mtime_raw = dst_mtime_raw;
    }

    if (link_target) {
        int32 target_len = strlen32(link_target);
        row->link_target_len = (int32)target_len;
        row->link_target = xarena_push(cecup.arena, target_len + 1);
        memcpy64(row->link_target, link_target, target_len + 1);
    }

    if (ignore_pattern) {
        int32 pattern_len = strlen32(ignore_pattern);
        row->ignore_pattern_len = (int32)pattern_len;
        row->ignore_pattern = xarena_push(cecup.arena, pattern_len + 1);
        memcpy64(row->ignore_pattern, ignore_pattern, pattern_len + 1);
    }

    row->src_path = final_src_path;
    row->dst_path = final_dst_path;
    row->path_len = path_len;

    if (cecup.rows_len >= cecup.rows_capacity) {
        cecup.rows_capacity *= 2;
        cecup.rows
            = xrealloc(cecup.rows, cecup.rows_capacity*SIZEOF(CecupRow *));
        cecup.rows_visible = xrealloc(cecup.rows_visible,
                                      cecup.rows_capacity*SIZEOF(CecupRow *));
    }

    cecup.rows[cecup.rows_len] = row;
    cecup.rows_len += 1;
    
    *nfiles_processed += 1;
    if (((cecup.rows_len % 100) == 0) && (nfiles_total > 0)) {
        ipc_send_progress(DATA_TYPE_PROGRESS_PREVIEW,
                          (double)*nfiles_processed
                          / (double)nfiles_total);
    }

    if ((cecup.rows_len % 10000) == 0) {
        Message *message = xmalloc(SIZEOF(*message));
        memset64(message, 0, SIZEOF(*message));

        message->type = DATA_TYPE_TREE_UPDATE;

        cecup.ui_waiting = true;
        g_idle_add(update_ui_handler, message);

        while (cecup.ui_waiting) {
            g_cond_wait(&cecup.ui_ready_cond, &cecup.arena_mutex);
        }
    }

    return;
}

static int64
work_fix_fs_recursive(char *base_path, char *relative) {
    DIR *dir;
    struct dirent *entry;
    char full_path[MAX_PATH_LENGTH];
    char **name_list;
    int32 count = 0;
    int32 capacity = 1024;
    int64 total_files = 0;

    if (relative) {
        SNPRINTF(full_path, "%s/%s", base_path, relative);
    } else {
        SNPRINTF(full_path, "%s", base_path);
    }

    if ((dir = opendir(full_path)) == NULL) {
        error(_("Error opening directory %s: %s.\n"), full_path,
              strerror(errno));
        error(_("Warning: Problematic file names will not be renamed.\n"));
        error(_("This is only a problem if you have problematic filenames.\n"));
        error(_(
            "Problematic filenames are the ones that contain the strings:\n"));
        for (int32 i = 0; i < LENGTH(replacements); i += 1) {
            error("\"%s\" ", replacements[i].problem);
        }
        error("\n");
        return 0;
    }

    name_list = xmalloc(capacity*SIZEOF(char *));

    while ((entry = readdir(dir))) {
        char *d_name = entry->d_name;

        if (d_name[0] == '.'
            && (d_name[1] == '\0' || (d_name[1] == '.' && d_name[2] == '\0'))) {
            continue;
        }

        if (count >= capacity) {
            capacity *= 2;
            name_list = xrealloc(name_list, capacity*SIZEOF(char *));
        }

        name_list[count] = xstrdup(d_name);
        count += 1;
    }

    closedir(dir);

    for (int32 i = 0; i < count; i += 1) {
        char *d_name = name_list[i];
        char sub_rel[MAX_PATH_LENGTH];
        char old_full[MAX_PATH_LENGTH];
        char new_full[MAX_PATH_LENGTH];
        char new_name[MAX_PATH_LENGTH];
        struct stat stat;
        bool changed = false;
        int64 j;
        int64 k;
        int64 name_len = strlen32(d_name);

        if (relative) {
            SNPRINTF(sub_rel, "%s/%s", relative, d_name);
        } else {
            SNPRINTF(sub_rel, "%s", d_name);
        }

        SNPRINTF(old_full, "%s/%s", base_path, sub_rel);
        if (lstat(old_full, &stat) != 0) {
            free(d_name);
            continue;
        }

        j = 0;
        k = 0;
        while (k < name_len) {
            char *earliest_match = NULL;
            int32 replacement_index = -1;

            for (int32 r = 0; r < LENGTH(replacements); r += 1) {
                char *search = replacements[r].problem;
                int64 search_len = strlen32(search);
                char *match;

                if ((match = memmem64(&d_name[k], name_len - k, search,
                                      search_len))) {
                    if (earliest_match == NULL || match < earliest_match) {
                        earliest_match = match;
                        replacement_index = r;
                    }
                }
            }

            if (earliest_match) {
                int64 prefix_len = (int64)(earliest_match - &d_name[k]);
                char *replace_str = replacements[replacement_index].rename;
                int64 replace_len = strlen32(replace_str);

                if (prefix_len > 0) {
                    memcpy64(&new_name[j], &d_name[k], prefix_len);
                    j += prefix_len;
                    k += prefix_len;
                }

                memcpy64(&new_name[j], replace_str, replace_len);

                j += replace_len;
                k += strlen32(replacements[replacement_index].problem);
                changed = true;
            } else {
                int64 remaining = name_len - k;
                memcpy64(&new_name[j], &d_name[k], remaining);
                j += remaining;
                k += remaining;
            }
        }
        new_name[j] = '\0';

        if (changed) {
            if (relative && relative[0]) {
                SNPRINTF(new_full, "%s/%s/%s", base_path, relative, new_name);
            } else {
                SNPRINTF(new_full, "%s/%s", base_path, new_name);
            }

            if (access(new_full, F_OK) == 0) {
                IPC_SEND_LOG_ERROR("Skip rename: %s already exists.\n",
                                   new_name);
            } else if (rename(old_full, new_full) == 0) {
                IPC_SEND_LOG("Fixed: %s -> %s\n", d_name, new_name);
                if (S_ISDIR(stat.st_mode)) {
                    if (relative) {
                        SNPRINTF(sub_rel, "%s/%s", relative, new_name);
                    } else {
                        SNPRINTF(sub_rel, "%s", new_name);
                    }
                }
            } else {
                IPC_SEND_LOG_ERROR("Error renaming %s to %s: %s\n", old_full,
                                   new_full, strerror(errno));
            }
        }

        if (S_ISDIR(stat.st_mode)) {
            total_files += work_fix_fs_recursive(base_path, sub_rel);
        } else {
            total_files += 1;
        }
        free(d_name);
    }

    free(name_list);
    return total_files;
}

static void *
work_fix_fs_thread_fn(void *user_data) {
    FixFsThreadData *data = user_data;
    data->file_count = work_fix_fs_recursive(data->base_path, NULL);
    return NULL;
}

static void *
work_fix_fs_worker(void *user_data) {
    ThreadData *thread_data = user_data;
    Message *message;
    struct stat stat_src;
    struct stat stat_dst;
    FixFsThreadData src_fix = {cecup.src_base, 0};
    FixFsThreadData dst_fix = {cecup.dst_base, 0};

    if (stat(cecup.src_base, &stat_src) < 0) {
        error("Error in stat(%s): %s.\n", cecup.src_base, strerror(errno));
        fatal(EXIT_FAILURE);
    }

    if (stat(cecup.dst_base, &stat_dst) < 0) {
        error("Error in stat(%s): %s.\n", cecup.dst_base, strerror(errno));
        fatal(EXIT_FAILURE);
    }

    IPC_SEND_LOG("Checking for problematic names...\n");
    if (stat_src.st_dev == stat_dst.st_dev) {
        work_fix_fs_thread_fn(&src_fix);
        work_fix_fs_thread_fn(&dst_fix);
    } else {
        GThread *t1 = g_thread_new("fix_src", work_fix_fs_thread_fn, &src_fix);
        GThread *t2 = g_thread_new("fix_dst", work_fix_fs_thread_fn, &dst_fix);
        g_thread_join(t1);
        g_thread_join(t2);
    }
    IPC_SEND_LOG("Name correction finished.\n");

    message = xmalloc(SIZEOF(*message));
    memset64(message, 0, SIZEOF(*message));

    message->type = DATA_TYPE_ENABLE_BUTTONS;
    g_idle_add(update_ui_handler, message);

    free(thread_data);
    return NULL;
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

    int32 buf_output_pos = 0;
    char src_base_with_slash[MAX_PATH_LENGTH];
    char dst_base_with_slash[MAX_PATH_LENGTH];
    char *rsync_args[64];
    int32 a = 0;
    char cmd[MAX_PATH_LENGTH*2];

    char **transfers = NULL;
    int32 transfer_count = 0;
    int32 transfers_capacity = 0;
    char files_from_filename[] = "/tmp/cecup_XXXXXX";

    char old_recursive[MAX_PATH_LENGTH];
    char new_recursive[MAX_PATH_LENGTH];

    struct stat stat_src;
    struct stat stat_dst;
    bool same_fs = true;
    struct Hash_map *show_patterns_map = hash_create_map(10);

    if (stat(cecup.src_base, &stat_src) < 0) {
        IPC_SEND_LOG_ERROR("Error in stat(%s): %s.\n",
                           cecup.src_base, strerror(errno));
        work_finalize(thread_data);
        return NULL;
    }
    if (stat(cecup.dst_base, &stat_dst) < 0) {
        IPC_SEND_LOG_ERROR("Error in stat(%s): %s.\n",
                           cecup.dst_base, strerror(errno));
        work_finalize(thread_data);
        return NULL;
    }

    same_fs = (stat_src.st_dev == stat_dst.st_dev);

    g_mutex_lock(&cecup.arena_mutex);

    if (thread_data->check_different_fs) {
        if (same_fs) {
            Message *message;
            IPC_SEND_LOG_ERROR(
                _("Safety stop: Original and backup are on the same storage "
                  "device.\n"
                  "Check if the backup device is connected.\n"
                  "To force backup on a folder in the same device, uncheck"
                  " option \"Protect same drive sync\".\n"));

            message = xmalloc(SIZEOF(*message));
            memset64(message, 0, SIZEOF(*message));

            arena_reset(cecup.arena);
            cecup.rows_len = 0;
            cecup.rows_visible_len = 0;

            message->type = DATA_TYPE_CLEAR_TREES;
            g_idle_add_full(G_PRIORITY_HIGH_IDLE, update_ui_handler, message, NULL);

            work_finalize(thread_data);
            return NULL;
        }
    }

    if (cecup.changed_dirs) {
        FixFsThreadData src_fix = {cecup.src_base, 0};
        FixFsThreadData dst_fix = {cecup.dst_base, 0};

        IPC_SEND_LOG("Checking for problematic names and counting files...\n");

        if (!same_fs) {
            GThread *t1 = g_thread_new("fix_src", work_fix_fs_thread_fn, &src_fix);
            GThread *t2 = g_thread_new("fix_dst", work_fix_fs_thread_fn, &dst_fix);
            g_thread_join(t1);
            g_thread_join(t2);
        } else {
            work_fix_fs_thread_fn(&src_fix);
            work_fix_fs_thread_fn(&dst_fix);
        }

        IPC_SEND_LOG("Name correction finished.\n");

        if (thread_data->is_preview && !thread_data->filtered) {
            Message *message;

            message = xmalloc(SIZEOF(*message));
            memset64(message, 0, SIZEOF(*message));

            message->type = DATA_TYPE_CLEAR_TREES;
            g_idle_add_full(G_PRIORITY_HIGH_IDLE, update_ui_handler, message, NULL);

            arena_reset(cecup.arena);
            cecup.rows_len = 0;
            cecup.rows_visible_len = 0;

            nfiles_total = src_fix.file_count;
            IPC_SEND_LOG("Found %lld files to analyse...\n",
                         (llong)nfiles_total);
        }
        cecup.changed_dirs = false;
    }

    xpipe(pipe_stdout);
    xpipe(pipe_stderr);

    rsync_args[a++] = "rsync";
    rsync_args[a++] = "--verbose";
    rsync_args[a++] = "--verbose";  // 2 times to show ignored files
    rsync_args[a++] = "--update";
    rsync_args[a++] = "--recursive";
    rsync_args[a++] = "--partial";
    rsync_args[a++] = "--progress";
    rsync_args[a++] = "--info=progress2";
    rsync_args[a++] = "--links";
    rsync_args[a++] = "--hard-links";
    rsync_args[a++] = "--itemize-changes";
    rsync_args[a++] = "--perms";
    rsync_args[a++] = "--times";
    rsync_args[a++] = "--owner";
    rsync_args[a++] = "--group";
    rsync_args[a++] = "--iconv=.,.";

    if (thread_data->delete_excluded) {
        rsync_args[a++] = "--delete-excluded";
    }
    if (thread_data->delete_after) {
        rsync_args[a++] = "--delete-after";
    }
    if (thread_data->is_preview) {
        rsync_args[a++] = "--dry-run";
    }

    if (thread_data->filtered) {
        char *relative_old = thread_data->relative_old;
        char *relative_new = thread_data->relative_new;
        int32 len_old = thread_data->len_old;
        int32 len_new = thread_data->len_new;

        rsync_args[a++] = "--include";
        rsync_args[a++] = relative_old;

        if (relative_old[len_old - 1] == '/') {
            SNPRINTF(old_recursive, "%s**", relative_old);
            rsync_args[a++] = "--include";
            rsync_args[a++] = old_recursive;
        }

        rsync_args[a++] = "--include";
        rsync_args[a++] = relative_new;

        if (relative_new[len_new - 1] == '/') {
            SNPRINTF(new_recursive, "%s**", relative_new);
            rsync_args[a++] = "--include";
            rsync_args[a++] = new_recursive;
        }

        // important: --include=*/ is necessary to include any sub directory
        // important: --exclude=* has to come last
        rsync_args[a++] = "--include="RSYNC_INCLUDE_DIRS;
        rsync_args[a++] = "--exclude="RSYNC_WILDCARD;
    } else {
        if (access(cecup.ignore_path, F_OK) != -1) {
            rsync_args[a++] = "--exclude-from";
            rsync_args[a++] = cecup.ignore_path;
        }
    }

    SNPRINTF(src_base_with_slash, "%s/", cecup.src_base);
    SNPRINTF(dst_base_with_slash, "%s/", cecup.dst_base);
    rsync_args[a++] = src_base_with_slash;
    rsync_args[a++] = dst_base_with_slash;
    rsync_args[a++] = NULL;

    STRING_FROM_ARRAY(cmd, " ", rsync_args, a);
    IPC_SEND_LOG_CMD("%s\n", cmd);

    switch (child_pid = fork()) {
    case -1:
        error("Error forking: %s.\n", strerror(errno));
        exit(EXIT_FAILURE);
    case 0:
        if (setpgid(0, 0) < 0) {
            error("Error setpgid: %s.\n", strerror(errno));
            fatal(EXIT_FAILURE);
        }
        putenv("LC_ALL=C.UTF-8");

        XCLOSE(&pipe_stdout[0]);
        XCLOSE(&pipe_stderr[0]);

        if (dup2(pipe_stdout[1], STDOUT_FILENO) < 0) {
            error("Error dup2 stdout: %s.\n", strerror(errno));
            fatal(EXIT_FAILURE);
        }
        if (dup2(pipe_stderr[1], STDERR_FILENO) < 0) {
            error("Error dup2 stderr: %s.\n", strerror(errno));
            fatal(EXIT_FAILURE);
        }

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
        char buf_output[MAX_PATH_LENGTH*2 + 1];
        char buf_error[MAX_PATH_LENGTH*2 + 1];

        pipes[0].revents = 0;
        pipes[1].revents = 0;

        switch (poll(pipes, 2, 100)) {
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
                   SIZEOF(buf_output) - 1 - buf_output_pos - 1);
        if (r <= 0) {
            if (r < 0) {
                IPC_SEND_LOG_ERROR("Error reading stdout pipe: %s.\n",
                                   strerror(errno));
                pipes[0].fd = -1;
            }
            goto read_error_pipe;
        }
        buf_output_pos += (int32)r;

        while (buf_output_pos > 0
               && ((eol = memchr64(buf_output, '\n', buf_output_pos))
                   || (eol = memchr64(buf_output, '\r', buf_output_pos)))) {
            char *link_target = NULL;
            char *ignore_pattern = NULL;
            char *show_pattern = NULL;
            char *interlude;
            char full_src[MAX_PATH_LENGTH];
            char full_dst[MAX_PATH_LENGTH];
            char *src_path;
            char *dst_path;
            int64 src_size = 0;
            int64 src_mtime = 0;
            int64 dst_size = 0;
            int64 dst_mtime = 0;
            int32 line_len;
            int32 remaining;
            enum CecupAction action;
            enum CecupReason reason;

            bool might_be_itemize_line;
            char action_char;
            char type_char;
            bool is_dir = false;
            bool ignore_duplicate_dir = false;

            line_len = (int32)(eol - buf_output);
            *eol = '\0';
            error("%s\n", buf_output);

            if ((src_path = begins_with(buf_output, RSYNC_SHOW_PRE_DIR))) {
                int32 left = line_len - (int32)(src_path - buf_output);
                interlude = memmem64(src_path,
                                     left,
                                     RSYNC_IGNORE_INTER,
                                     strlen32(RSYNC_IGNORE_INTER));

                left = (int32)(r - (interlude - buf_output));

                error("before:\n");
                fwrite64(buf_output, 1, line_len + 1, stderr);
                fwrite64("\n", 1, 1, stderr);
                if (*(interlude - 1) != '/') {
                    memmove64(interlude + 1, interlude, left);
                    interlude += 1;
                    *(interlude - 1) = '/';
                }
                *interlude = '\0';

                error("after:\n");
                fwrite64(buf_output, 1, line_len + 1, stderr);
                fwrite64("\n", 1, 1, stderr);

                show_pattern = interlude + strlen32(RSYNC_IGNORE_INTER);
                hash_insert2_map(show_patterns_map,
                                 src_path, xstrdup(show_pattern));

                PRINTLN(show_pattern);
                PRINTLN(src_path);
            }

            might_be_itemize_line = check_itemize_line(buf_output);
            action_char = buf_output[0];
            type_char = buf_output[1];

            if ((dst_path = begins_with(buf_output, RSYNC_MESSAGE_DELETING))) {

                while (isspace(*dst_path)) {
                    dst_path += 1;
                }
                src_path = dst_path;

                SNPRINTF(full_src, "%s/%s", cecup.src_base, src_path);
                SNPRINTF(full_dst, "%s/%s", cecup.dst_base, dst_path);

                if (get_file_info(full_src, &src_path,
                                  &src_size, &src_mtime, &is_dir)) {
                    reason = REASON_IGNORED;
                } else {
                    reason = REASON_MISSING;
                }

                get_file_info(full_dst, &dst_path,
                              &dst_size, &dst_mtime, &is_dir);

                if (thread_data->is_preview && (reason == REASON_MISSING)) {
                    // if source file exists, rsync will report it as ignored
                    // so we dont send it here to avoid the duplication
                    work_add_row(ACTION_DELETE, reason,
                                 src_path, dst_path, NULL, NULL,
                                 src_size, src_mtime, dst_size, dst_mtime,
                                 thread_data->delete_excluded, is_dir,
                                 &nfiles_processed, nfiles_total);
                }
            } else if ((src_path = begins_with(buf_output,
                                               RSYNC_IGNORE_PRE_FILE))
                        || (src_path = begins_with(buf_output,
                                                   RSYNC_IGNORE_PRE_DIR))) {
                int32 path_len = (int32)(src_path - buf_output);
                dst_path = src_path;

                interlude = memmem64(src_path,
                                     line_len - path_len,
                                     RSYNC_IGNORE_INTER,
                                     strlen32(RSYNC_IGNORE_INTER));
                *interlude = '\0';
                ignore_pattern = interlude + strlen32(RSYNC_IGNORE_INTER);

                SNPRINTF(full_src, "%s/%s", cecup.src_base, src_path);
                SNPRINTF(full_dst, "%s/%s", cecup.dst_base, dst_path);

                get_file_info(full_src, &src_path,
                              &src_size, &src_mtime, &is_dir);
                get_file_info(full_dst, &dst_path,
                              &dst_size, &dst_mtime, &is_dir);

                if (thread_data->is_preview
                    && strcmp(ignore_pattern, RSYNC_WILDCARD)) {
                    work_add_row(ACTION_IGNORE, REASON_IGNORED,
                                 src_path, dst_path, NULL, ignore_pattern,
                                 src_size, src_mtime, dst_size, dst_mtime,
                                 thread_data->delete_excluded, is_dir,
                                 &nfiles_processed, nfiles_total);
                }
            } else if (might_be_itemize_line
                       && ((action_char == RSYNC_CHAR0_ACTION_RECEIVE)
                           || (action_char == RSYNC_CHAR0_ACTION_HARDLINK)
                           || (action_char == RSYNC_CHAR0_ACTION_CHANGE))) {

                char *space_pos = strchr(buf_output, ' ');

                src_path = space_pos + 1;
                while (isspace(*src_path)) {
                    src_path += 1;
                }
                dst_path = src_path;

                if (action_char == RSYNC_CHAR0_ACTION_HARDLINK) {
                    char *sep;

                    if ((sep = strstr(src_path, RSYNC_HARDLINK_NOTATION))) {
                        link_target = sep + strlen32(RSYNC_HARDLINK_NOTATION);
                        *sep = '\0';
                    }

                    action = ACTION_HARDLINK;
                } else if (type_char == RSYNC_CHAR1_TYPE_SYMLINK) {
                    char *sep = strstr(src_path, RSYNC_SYMLINK_NOTATION);
                    link_target = sep + strlen32(RSYNC_SYMLINK_NOTATION);
                    *sep = '\0';

                    action = ACTION_SYMLINK;
                } else if (buf_output[2] == '+') {
                    action = ACTION_NEW;
                } else {
                    action = ACTION_UPDATE;
                }

                
                if (did_attribute_change(buf_output)) {
                    reason = (enum CecupReason)action;
                } else {
                    action = ACTION_EQUAL;
                    reason = REASON_EQUAL;
                }

                if ((thread_data->is_preview == 0)
                    && ((type_char == RSYNC_CHAR1_TYPE_FILE)
                        || (type_char == RSYNC_CHAR1_TYPE_SYMLINK))
                    && ((action_char == RSYNC_CHAR0_ACTION_RECEIVE)
                        || (action_char == RSYNC_CHAR0_ACTION_CHANGE)
                        || (action_char == RSYNC_CHAR0_ACTION_HARDLINK))) {

                    if (transfer_count >= transfers_capacity) {
                        if (transfers_capacity == 0) {
                            transfers_capacity = 256;
                        } else {
                            transfers_capacity *= 2;
                        }
                        transfers = xrealloc(
                            transfers, transfers_capacity*SIZEOF(*transfers));
                    }
                    transfers[transfer_count] = xstrdup(src_path);
                    transfer_count += 1;
                }

                SNPRINTF(full_src, "%s/%s", cecup.src_base, src_path);
                SNPRINTF(full_dst, "%s/%s", cecup.dst_base, dst_path);

                get_file_info(full_src, &src_path,
                              &src_size, &src_mtime, &is_dir);
                get_file_info(full_dst, &dst_path,
                              &dst_size, &dst_mtime, &is_dir);

                if (thread_data->filtered) {
                    char *path;
                    char **pattern_ptr;

                    if (src_path) {
                        path = src_path;
                    } else {
                        path = dst_path;
                    }

                    if (path) {
                        pattern_ptr = hash_lookup2_map(show_patterns_map, path);
                        if (pattern_ptr) {
                            show_pattern = *pattern_ptr;
                            ignore_duplicate_dir = !strcmp(show_pattern,
                                                           RSYNC_INCLUDE_DIRS);
                        }
                    }
                }

                if (!(thread_data->filtered
                      && (!strcmp(src_path, "./")
                          || ignore_duplicate_dir))) {
                    if (thread_data->is_preview) {
                        work_add_row(action, reason,
                                     src_path, dst_path, link_target, NULL,
                                     src_size, src_mtime, dst_size, dst_mtime,
                                     thread_data->delete_excluded, is_dir,
                                     &nfiles_processed, nfiles_total);
                    }
                }
            } else if (might_be_itemize_line) {
                char *space_pos = strchr(buf_output, ' ');

                action = ACTION_UPDATE;
                reason = REASON_UPDATE;

                if (!did_attribute_change(buf_output)) {
                    action = ACTION_EQUAL;
                    reason = REASON_EQUAL;
                }

                src_path = space_pos + 1;
                while (isspace(*src_path)) {
                    src_path += 1;
                }
                dst_path = src_path;

                if (action_char == RSYNC_CHAR0_ACTION_HARDLINK) {
                    char *sep;

                    if ((sep = strstr(src_path, RSYNC_HARDLINK_NOTATION))) {
                        *sep = '\0';
                        link_target = sep + strlen32(RSYNC_HARDLINK_NOTATION);
                    }
                } else if (type_char == RSYNC_CHAR1_TYPE_SYMLINK) {
                    char *sep;

                    sep = strstr(src_path, RSYNC_SYMLINK_NOTATION);
                    *sep = '\0';
                    link_target = sep + strlen32(RSYNC_SYMLINK_NOTATION);
                }

                SNPRINTF(full_src, "%s/%s", cecup.src_base, src_path);
                SNPRINTF(full_dst, "%s/%s", cecup.dst_base, dst_path);

                get_file_info(full_src, &src_path,
                              &src_size, &src_mtime, &is_dir);
                get_file_info(full_dst, &dst_path,
                              &dst_size, &dst_mtime, &is_dir);

                if (!(thread_data->filtered && !strcmp(src_path, "./"))) {
                    if (thread_data->is_preview) {
                        work_add_row(action, reason,
                                     src_path, dst_path, link_target, NULL,
                                     src_size, src_mtime, dst_size, dst_mtime,
                                     thread_data->delete_excluded, is_dir,
                                     &nfiles_processed, nfiles_total);
                    }
                }
            }

            remaining = buf_output_pos - (line_len + 1);
            if (remaining > 0) {
                memmove64(buf_output, eol + 1, remaining);
            }
            buf_output_pos = remaining;
        }

        if (buf_output_pos >= (int32)SIZEOF(buf_output) - 1) {
            buf_output[buf_output_pos] = '\0';
            IPC_SEND_LOG("%s\n", buf_output);
            buf_output_pos = 0;
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
    g_mutex_unlock(&cecup.arena_mutex);

    if (waitpid(child_pid, NULL, 0) < 0) {
        IPC_SEND_LOG_ERROR("Error waiting for child: %s.\n", strerror(errno));
    }
    cecup.child_pid = 0;

    XCLOSE(&pipe_stderr[0]);
    XCLOSE(&pipe_stdout[0]);

    if (transfer_count <= 0) {
        hash_destroy_map(show_patterns_map);
        work_finalize(thread_data);
        return NULL;
    }

    {
        int files_from_fd;
        if ((files_from_fd = mkstemp(files_from_filename)) < 0) {
            error("Error in mkstemp: %s.\n", strerror(errno));
            fatal(EXIT_FAILURE);
        }
        for (int32 i = 0; i < transfer_count; i += 1) {
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

    a = 0;
    rsync_args[a++] = "rsync";
    rsync_args[a++] = "--verbose";
    rsync_args[a++] = "--recursive";
    rsync_args[a++] = "--partial";
    rsync_args[a++] = "--progress";
    rsync_args[a++] = "--info=progress2";
    rsync_args[a++] = "--checksum";  // check if previous copy went corrupted
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

    IPC_SEND_LOG("Verifying transfers with checksum...\n");
    STRING_FROM_ARRAY(cmd, " ", rsync_args, a);
    IPC_SEND_LOG_CMD("%s\n", cmd);

    xpipe(pipe_stdout);
    xpipe(pipe_stderr);

    switch (child_pid = fork()) {
    case -1:
        IPC_SEND_LOG_ERROR("Error forking for checksum: %s.\n",
                           strerror(errno));
        break;
    case 0:
        setpgid(0, 0);
        putenv("LC_ALL=C.UTF-8");

        XCLOSE(&pipe_stderr[0]);
        XCLOSE(&pipe_stdout[0]);

        dup2(pipe_stdout[1], STDOUT_FILENO);
        dup2(pipe_stderr[1], STDERR_FILENO);

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
        char buf_output[MAX_PATH_LENGTH*2];
        char buf_error[MAX_PATH_LENGTH*2];

        pipes[0].revents = 0;
        pipes[1].revents = 0;

        switch (poll(pipes, 2, 100)) {
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
            goto read_error_pipe2;
        }
        if (pipes[0].revents & POLLHUP) {
            pipes[0].fd = -1;
        }
        if (!(pipes[0].revents & POLLIN)) {
            goto read_error_pipe2;
        }

        r = read64(pipe_stdout[0], buf_output, SIZEOF(buf_output) - 1);
        if (r <= 0) {
            if (r < 0) {
                IPC_SEND_LOG_ERROR("Error reading stdout pipe: %s.\n",
                                   strerror(errno));
                pipes[1].fd = -1;
            }
            goto read_error_pipe2;
        }

    read_error_pipe2:
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
    unlink(files_from_filename);
    cecup.child_pid = 0;
    XCLOSE(&pipe_stderr[0]);
    XCLOSE(&pipe_stdout[0]);

    for (int32 i = 0; i < transfer_count; i += 1) {
        free(transfers[i]);
    }
    free(transfers);

    if (thread_data->is_preview) {
        IPC_SEND_LOG("Analysis complete. Review the list and click Apply.\n");
    }

    hash_destroy_map(show_patterns_map);
    work_finalize(thread_data);
    return NULL;
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

        if (task->side == SIDE_LEFT) {
            SNPRINTF(full_path, "%s/%s", cecup.src_base, task->path);
        } else {
            SNPRINTF(full_path, "%s/%s", cecup.dst_base, task->path);
        }
        switch (child_rm = fork()) {
        case -1:
            error("Error forking: %s.\n", strerror(errno));
            break;
        case 0: {
            char cmd_rm[MAX_PATH_LENGTH];
            char *args_rm[] = {
                "rm",
                "-rf",
                full_path,
                NULL,
            };

            STRING_FROM_ARRAY(cmd_rm, " ", args_rm, LENGTH(args_rm));
            error("+ %s\n", cmd_rm);

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
            Message *message;
            int32 path_len = task->path_len;
            char *path = task->path;

            message = xmalloc(SIZEOF(*message));
            memset64(message, 0, SIZEOF(*message));

            message->path_len = path_len;
            message->src_path = xmalloc(path_len + 1);
            memcpy64(message->src_path, path, path_len + 1);

            message->type = DATA_TYPE_REMOVE_ROW;
            g_idle_add(update_ui_handler, message);
        }
    }

    if (!has_transfers) {
        work_finalize(NULL);
        free_task_list(tasks);
        return NULL;
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
            // when using the --files-from mode,
            // rsync only transfers hard links
            // if the target is also included in the --files-from list
            write64(files_from_fd, task->link_target, task->link_target_len);
            write64(files_from_fd, "\n", 1);
            __attribute__((fallthrough));
        case ACTION_NEW:
        case ACTION_UPDATE:
        case ACTION_SYMLINK:
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
    rsync_args[a++] = "--recursive";
    rsync_args[a++] = "--partial";
    rsync_args[a++] = "--progress";
    rsync_args[a++] = "--info=progress2";
    rsync_args[a++] = "--links";
    rsync_args[a++] = "--hard-links";
    rsync_args[a++] = "--itemize-changes";
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
        error("Error forking for rsync: %s.\n", strerror(errno));
        fatal(EXIT_FAILURE);
    case 0:
        if (setpgid(0, 0) < 0) {
            error("Error setpgid: %s.\n", strerror(errno));
            fatal(EXIT_FAILURE);
        }
        putenv("LC_ALL=C.UTF-8");

        XCLOSE(&pipe_stderr[0]);
        XCLOSE(&pipe_stdout[0]);

        if (dup2(pipe_stdout[1], STDOUT_FILENO) < 0) {
            error("Error duplicating stdout: %s.\n", strerror(errno));
            fatal(EXIT_FAILURE);
        }
        if (dup2(pipe_stderr[1], STDERR_FILENO) < 0) {
            error("Error duplicating stderr: %s.\n", strerror(errno));
            fatal(EXIT_FAILURE);
        }

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
        char buf_output[MAX_PATH_LENGTH*2];
        char buf_error[MAX_PATH_LENGTH*2];

        pipes[0].revents = 0;
        pipes[1].revents = 0;

        switch (poll(pipes, 2, 100)) {
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
                   SIZEOF(buf_output) - 1 - buf_output_pos);
        if (r <= 0) {
            if (r < 0) {
                IPC_SEND_LOG_ERROR("Error reading stdout pipe: %s.\n",
                                   strerror(errno));
                pipes[0].fd = -1;
            }
            goto read_error_pipe;
        }
        buf_output_pos += (int32)r;

        while (buf_output_pos > 0
               && ((eol = memchr64(buf_output, '\n', buf_output_pos))
                   || (eol = memchr64(buf_output, '\r', buf_output_pos)))) {
            int32 line_len = (int32)(eol - buf_output);
            int32 itemize_length = strlen32(RSYNC_ITEMIZE_PLACEHOLDERS);
            int32 remaining;

            *eol = '\0';

            IPC_SEND_LOG("%s\n", buf_output);

            if (check_itemize_line(buf_output)) {
                char *filename = buf_output + itemize_length + 1;
                char *sep;
                Message *message;
                int32 path_len = strlen32(filename);

                if ((sep = strstr(filename, RSYNC_HARDLINK_NOTATION))) {
                    *sep = '\0';
                } else if ((sep = strstr(filename, RSYNC_SYMLINK_NOTATION))) {
                    *sep = '\0';
                }

                message = xmalloc(SIZEOF(*message));
                memset64(message, 0, SIZEOF(*message));

                message->path_len = path_len;
                message->src_path = xmalloc(path_len + 1);
                memcpy64(message->src_path, filename, path_len + 1);

                message->type = DATA_TYPE_REMOVE_ROW;
                g_idle_add(update_ui_handler, message);
            }

            remaining = buf_output_pos - (line_len + 1);
            if (remaining > 0) {
                memmove64(buf_output, eol + 1, remaining);
            }
            buf_output_pos = remaining;
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
        IPC_SEND_LOG_ERROR("Error waiting for child: %s.\n", strerror(errno));
    }
    unlink(files_from_filename);
    cecup.child_pid = 0;

    XCLOSE(&pipe_stdout[0]);
    XCLOSE(&pipe_stderr[0]);

    work_finalize(NULL);
    free_task_list(tasks);
    return NULL;
}

#if TESTING_work
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "aux.c"
#include "on.c"

int
main(void) {
    ASSERT(true);
    exit(EXIT_SUCCESS);
}

#endif

#endif
