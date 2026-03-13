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
#include "aux.c"

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_work 1
#elif !defined(TESTING_work)
#define TESTING_work 0
#endif

static bool
did_attribute_change(char *buf_output) {
    bool attribute_changed = false;
    for (int32 i = 2; i < strlen32(RSYNC_ITEMIZE_PLACEHOLDERS); i += 1) {
        if ((buf_output[i] != '.') && (buf_output[i] != ' ')) {
            attribute_changed = true;
            break;
        }
    }
    return attribute_changed;
}

// Note: NEVER delete lines with // clang-format
// clang-format off
static void
work_send_tree(int32 side,
              enum CecupAction action, enum CecupReason reason,
              char *src_path, char *dst_path,
              char *link_target, char *ignore_pattern,
              int64 src_size, int64 src_mtime,
              int64 dst_size, int64 dst_mtime,
              bool delete_excluded) {
    // clang-format on
    CecupRow *row;
    Message *message;
    char *final_src_path = NULL;
    char *final_dst_path = NULL;
    int64 path_len = 0;
    int64 target_len;
    int64 pattern_len;
    (void)side;

    if (src_path) {
        path_len = strlen32(src_path);
        final_src_path = xarena_push(cecup.row_arena, ALIGN16(path_len + 1));
        memcpy64(final_src_path, src_path, path_len + 1);
        if (dst_path) {
            final_dst_path = final_src_path;
        }
    } else if (dst_path) {
        path_len = strlen32(dst_path);
        final_dst_path = xarena_push(cecup.row_arena, ALIGN16(path_len + 1));
        memcpy64(final_dst_path, dst_path, path_len + 1);
    } else {
        error("Error: both src_path and dst_path are NULL.\n");
        exit(EXIT_FAILURE);
    }

    row = xarena_push(cecup.row_arena, ALIGN16(SIZEOF(*row)));
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

    row->src_color = colors[row->src_action];
    row->dst_color = colors[row->dst_action];

    bytes_pretty(row->src_size_text, src_size);
    row->src_size_raw = src_size;
    bytes_pretty(row->dst_size_text, dst_size);
    row->dst_size_raw = dst_size;

    if (src_mtime > 0) {
        time_t t = (time_t)src_mtime;
        struct tm *tm_info = localtime(&t);
        STRFTIME(row->src_mtime_text, "%Y-%m-%d %H:%M:%S", tm_info);
        row->src_mtime_raw = src_mtime;
    }

    if (dst_mtime > 0) {
        time_t t = (time_t)dst_mtime;
        struct tm *tm_info = localtime(&t);
        STRFTIME(row->dst_mtime_text, "%Y-%m-%d %H:%M:%S", tm_info);
        row->dst_mtime_raw = dst_mtime;
    }

    if (link_target) {
        target_len = strlen32(link_target);
        row->link_target_len = (int32)target_len;
        row->link_target
            = xarena_push(cecup.row_arena, ALIGN16(target_len + 1));
        memcpy64(row->link_target, link_target, target_len + 1);
    }

    if (ignore_pattern) {
        pattern_len = strlen32(ignore_pattern);
        row->ignore_pattern_len = (int32)pattern_len;
        row->ignore_pattern
            = xarena_push(cecup.row_arena, ALIGN16(pattern_len + 1));
        memcpy64(row->ignore_pattern, ignore_pattern, pattern_len + 1);
    }

    row->src_path = final_src_path;
    row->dst_path = final_dst_path;
    row->src_path_len = (final_src_path) ? (int32)path_len : 0;
    row->dst_path_len = (final_dst_path) ? (int32)path_len : 0;

    if (cecup.rows_len >= cecup.rows_capacity) {
        g_mutex_lock(&cecup.row_arena_mutex);
        cecup.rows_capacity *= 2;
        cecup.rows
            = xrealloc(cecup.rows, cecup.rows_capacity*SIZEOF(CecupRow *));
        cecup.rows_visible = xrealloc(cecup.rows_visible,
                                      cecup.rows_capacity*SIZEOF(CecupRow *));
        g_mutex_unlock(&cecup.row_arena_mutex);
    }
    cecup.rows[cecup.rows_len] = row;
    cecup.rows_len += 1;

    if ((cecup.rows_len % 1000) == 0) {
        g_mutex_lock(&cecup.ui_arena_mutex);
        message = xarena_push(cecup.ui_arena, ALIGN16(SIZEOF(Message)));
        memset64(message, 0, SIZEOF(Message));
        g_mutex_unlock(&cecup.ui_arena_mutex);

        message->type = DATA_TYPE_TREE_UPDATE;
        g_idle_add(update_ui_handler, message);
    }
    return;
}

static int64
work_count_files_recursive(char *base_path, char *relative_path) {
    DIR *dir;
    struct dirent *entry;
    char full_path[MAX_PATH_LENGTH];
    int64 count;

    count = 0;
    if (relative_path) {
        SNPRINTF(full_path, "%s/%s", base_path, relative_path);
    } else {
        SNPRINTF(full_path, "%s", base_path);
    }

    if ((dir = opendir(full_path)) == NULL) {
        IPC_SEND_LOG_ERROR("Error opening directory %s: %s.\n", full_path,
                           strerror(errno));
        return 0;
    }

    errno = 0;
    while ((entry = readdir(dir))) {
        char *name;
        char sub_rel[MAX_PATH_LENGTH];
        struct stat st;

        name = entry->d_name;
        if (name[0] == '.'
            && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'))) {
            continue;
        }

        if (relative_path) {
            SNPRINTF(sub_rel, "%s/%s", relative_path, name);
        } else {
            SNPRINTF(sub_rel, "%s", name);
        }

        SNPRINTF(full_path, "%s/%s", base_path, sub_rel);
        if (lstat(full_path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                count += work_count_files_recursive(base_path, sub_rel);
            } else {
                count += 1;
            }
        } else {
            IPC_SEND_LOG_ERROR("Error lstat %s: %s.\n", full_path,
                               strerror(errno));
        }
        errno = 0;
    }

    if (errno) {
        IPC_SEND_LOG_ERROR("Error reading directory entry in %s: %s.\n",
                           full_path, strerror(errno));
    }

    if (closedir(dir) < 0) {
        IPC_SEND_LOG_ERROR("Error closing directory %s: %s.\n", full_path,
                           strerror(errno));
    }
    return count;
}

static void
work_fix_fs_recursive(char *base_path, char *relative_path) {
    DIR *dir;
    struct dirent *entry;
    char full_path[MAX_PATH_LENGTH];
    char **name_list;
    int32 count = 0;
    int32 capacity = 1024;

    if (relative_path) {
        SNPRINTF(full_path, "%s/%s", base_path, relative_path);
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
        return;
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
        struct stat st;
        bool changed = false;
        int64 j;
        int64 k;
        int64 name_len = strlen32(d_name);

        if (relative_path) {
            SNPRINTF(sub_rel, "%s/%s", relative_path, d_name);
        } else {
            SNPRINTF(sub_rel, "%s", d_name);
        }

        SNPRINTF(old_full, "%s/%s", base_path, sub_rel);
        if (lstat(old_full, &st) != 0) {
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
            if (relative_path && relative_path[0]) {
                SNPRINTF(new_full, "%s/%s/%s", base_path, relative_path,
                         new_name);
            } else {
                SNPRINTF(new_full, "%s/%s", base_path, new_name);
            }

            if (access(new_full, F_OK) == 0) {
                IPC_SEND_LOG_ERROR("Skip rename: %s already exists.\n",
                                   new_name);
            } else if (rename(old_full, new_full) == 0) {
                IPC_SEND_LOG("Fixed: %s -> %s\n", d_name, new_name);
                if (S_ISDIR(st.st_mode)) {
                    if (relative_path) {
                        SNPRINTF(sub_rel, "%s/%s", relative_path, new_name);
                    } else {
                        SNPRINTF(sub_rel, "%s", new_name);
                    }
                }
            } else {
                IPC_SEND_LOG_ERROR("Error renaming %s to %s: %s\n", old_full,
                                   new_full, strerror(errno));
            }
        }

        if (S_ISDIR(st.st_mode)) {
            work_fix_fs_recursive(base_path, sub_rel);
        }
        free(d_name);
    }

    free(name_list);
    return;
}

static void *
work_fix_fs_worker(void *user_data) {
    ThreadData *thread_data = user_data;
    Message *message;

    IPC_SEND_LOG("Checking for problematic names in the original folder...\n");
    work_fix_fs_recursive(cecup.src_base, NULL);
    IPC_SEND_LOG("Checking for problematic names in the backup folder...\n");
    work_fix_fs_recursive(cecup.dst_base, NULL);
    IPC_SEND_LOG("Name correction finished.\n");

    g_mutex_lock(&cecup.ui_arena_mutex);
    message = xarena_push(cecup.ui_arena, ALIGN16(SIZEOF(Message)));
    memset64(message, 0, SIZEOF(Message));

    message->type = DATA_TYPE_ENABLE_BUTTONS;
    g_idle_add(update_ui_handler, message);

    arena_pop(cecup.ui_arena, thread_data);
    g_mutex_unlock(&cecup.ui_arena_mutex);
    return NULL;
}

static void *
work_rsync(void *user_data) {
    ThreadData *thread_data = user_data;
    int64 total_files_preview = 0;
    int64 processed_files_preview = 0;

    int32 pipe_stdout[2];
    int32 pipe_stderr[2];
    struct pollfd pipes[2];
    pid_t child_process_id;

    char buf_output[MAX_PATH_LENGTH*2];
    int32 buf_output_pos = 0;
    char buf_error[MAX_PATH_LENGTH*2];

    char src_base_with_slash[MAX_PATH_LENGTH];
    char dst_base_with_slash[MAX_PATH_LENGTH];
    char *rsync_args[64];
    int32 a = 0;
    char cmd[MAX_PATH_LENGTH*2];

    char **checksum_files = NULL;
    int32 checksum_count = 0;
    int32 checksum_capacity = 0;
    char *files_from_filename = "/tmp/rsync";

    char old_recursive[MAX_PATH_LENGTH];
    char new_recursive[MAX_PATH_LENGTH];

    if (thread_data->check_different_fs) {
        struct stat stat_src;
        struct stat stat_dst;

        if (stat(cecup.src_base, &stat_src) < 0) {
            IPC_SEND_LOG_ERROR("Error checking %s: %s.\n", cecup.src_base,
                               strerror(errno));
            goto finalize;
        }
        if (stat(cecup.dst_base, &stat_dst) < 0) {
            IPC_SEND_LOG_ERROR("Error checking %s: %s.\n", cecup.dst_base,
                               strerror(errno));
            goto finalize;
        }

        if (stat_src.st_dev == stat_dst.st_dev) {
            Message *message;
            IPC_SEND_LOG_ERROR(
                _("Safety stop: Original and backup are on the same storage "
                  "device.\n"
                  "Check if the backup device is connected.\n"
                  "To force backup on a folder in the same device, uncheck"
                  " option \"Protect same drive sync\".\n"));

            g_mutex_lock(&cecup.ui_arena_mutex);
            message = xarena_push(cecup.ui_arena, ALIGN16(SIZEOF(Message)));
            memset64(message, 0, SIZEOF(Message));
            g_mutex_unlock(&cecup.ui_arena_mutex);

            message->type = DATA_TYPE_CLEAR_TREES;
            g_idle_add(update_ui_handler, message);

            goto finalize;
        }
    }

    if (thread_data->is_preview && !thread_data->filtered) {
        Message *message;

        g_mutex_lock(&cecup.ui_arena_mutex);
        message = xarena_push(cecup.ui_arena, ALIGN16(SIZEOF(Message)));
        memset64(message, 0, SIZEOF(Message));
        g_mutex_unlock(&cecup.ui_arena_mutex);

        message->type = DATA_TYPE_CLEAR_TREES;
        g_idle_add(update_ui_handler, message);

        IPC_SEND_LOG("Counting files to prepare analysis...\n");
        total_files_preview = work_count_files_recursive(cecup.src_base, NULL);
        IPC_SEND_LOG("Found %lld files to analyse...\n",
                     (llong)total_files_preview);
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
        int32 len_old = strlen32(thread_data->relative_old);
        int32 len_new = strlen32(thread_data->relative_new);

        rsync_args[a++] = "--include";
        rsync_args[a++] = thread_data->relative_old;

        if (thread_data->relative_old[len_old - 1] == '/') {
            SNPRINTF(old_recursive, "%s**", thread_data->relative_old);
            rsync_args[a++] = "--include";
            rsync_args[a++] = old_recursive;
        }

        rsync_args[a++] = "--include";
        rsync_args[a++] = thread_data->relative_new;

        if (thread_data->relative_new[len_new - 1] == '/') {
            SNPRINTF(new_recursive, "%s**", thread_data->relative_new);
            rsync_args[a++] = "--include";
            rsync_args[a++] = new_recursive;
        }

        // important: --exclude=* has to come last
        rsync_args[a++] = "--exclude=*";
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

    switch (child_process_id = fork()) {
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
        cecup.child_pid = child_process_id;
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

        switch (poll(pipes, 2, 100)) {
        case -1:
            if (errno != EINTR) {
                IPC_SEND_LOG_ERROR("Error in poll: %s.\n", strerror(errno));
                fatal(EXIT_FAILURE);
            }
            continue;
        case 0:
            continue;
        default:
            break;
        }

        if (pipes[0].revents & (POLLHUP | POLLERR)) {
            pipes[0].fd = -1;
            goto read_error_pipe;
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
            char *link_target;
            char full_src[MAX_PATH_LENGTH];
            char full_dst[MAX_PATH_LENGTH];
            struct stat st_src;
            struct stat st_dst;
            char *src_path;
            char *dst_path;
            int64 src_size = 0;
            int64 src_mtime = 0;
            int64 dst_size = 0;
            int64 dst_mtime = 0;
            int32 line_len = (int32)(eol - buf_output);
            int32 remaining;

            char action_char = buf_output[0];
            char type_char = buf_output[1];

            bool might_be_itemize_line = true;

            *eol = '\0';
            if (DEBUGGING) {
                error("%s\n", buf_output);
            }
            if (literal_match(buf_output, "[sender] showing")) {
                error("%s\n", buf_output);
            }

            switch (action_char) {
            case RSYNC_CHAR0_ACTION_SEND:
            case RSYNC_CHAR0_ACTION_RECEIVE:
            case RSYNC_CHAR0_ACTION_CHANGE:
            case RSYNC_CHAR0_ACTION_HARDLINK:
            case RSYNC_CHAR0_ACTION_NO_UPDATE:
                break;
            default:
                might_be_itemize_line = false;
                break;
            }

            switch (type_char) {
            case RSYNC_CHAR1_TYPE_FILE:
            case RSYNC_CHAR1_TYPE_DIR:
            case RSYNC_CHAR1_TYPE_SYMLINK:
            case RSYNC_CHAR1_TYPE_DEVICE:
            case RSYNC_CHAR1_TYPE_SPECIAL:
                break;
            default:
                might_be_itemize_line = false;
                break;
            }

            for (int i = 2; i < strlen32(RSYNC_ITEMIZE_PLACEHOLDERS); i += 1) {
                if (!might_be_itemize_line) {
                    break;
                }

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
                    might_be_itemize_line = false;
                    break;
                }
            }

            if (buf_output[strlen32(RSYNC_ITEMIZE_PLACEHOLDERS)] != ' ') {
                might_be_itemize_line = false;
            }

            {
                char *percent_pos;
                if ((percent_pos = strstr(buf_output, "%"))) {
                    char *start_digit = percent_pos;
                    while ((start_digit > buf_output)
                           && isdigit(*(start_digit - 1))) {
                        start_digit -= 1;
                    }
                    ipc_send_progress(DATA_TYPE_PROGRESS_RSYNC,
                                      atof(start_digit) / 100.0);
                }
            }

            if ((dst_path
                 = literal_match(buf_output, RSYNC_MESSAGE_DELETING))) {
                enum CecupReason reason;
                src_path = NULL;

                while (isspace(*dst_path)) {
                    dst_path += 1;
                }

                SNPRINTF(full_src, "%s/%s", cecup.src_base, dst_path);
                SNPRINTF(full_dst, "%s/%s", cecup.dst_base, dst_path);

                if (lstat(full_src, &st_src) < 0) {
                    src_size = 0;
                    src_mtime = 0;
                    reason = REASON_MISSING;
                } else {
                    src_size = st_src.st_size;
                    src_mtime = (int64)st_src.st_mtime;
                    reason = REASON_IGNORED;
                }

                if (lstat(full_dst, &st_dst) < 0) {
                    dst_size = 0;
                    dst_mtime = 0;
                } else {
                    dst_size = st_dst.st_size;
                    dst_mtime = (int64)st_dst.st_mtime;
                }

                // Note: NEVER delete lines with // clang-format
                // clang-format off
                if (thread_data->is_preview && (reason == REASON_MISSING)) {
                    // if source file exists, rsync will report it as ignored
                    // so we dont send it here to avoid the duplication
                    work_send_tree(SIDE_RIGHT,
                                   ACTION_DELETE, reason,
                                   src_path, dst_path, NULL, NULL,
                                   src_size, src_mtime, dst_size, dst_mtime,
                                   thread_data->delete_excluded);
                }
            } else if ((src_path = literal_match(buf_output,
                                                 RSYNC_IGNORE_PRE))
                       || (src_path = literal_match(buf_output,
                                                    RSYNC_IGNORE_DIR_PRE))) {
                char *reason_sep;
                char *ignore_pattern = NULL;

                if ((reason_sep = strstr(src_path, RSYNC_IGNORE_INTER))) {
                    *reason_sep = '\0';
                    ignore_pattern = reason_sep + strlen32(RSYNC_IGNORE_INTER);

                    SNPRINTF(full_src, "%s/%s", cecup.src_base,
                             src_path);
                    SNPRINTF(full_dst, "%s/%s", cecup.dst_base,
                             src_path);

                    if (lstat(full_src, &st_src) < 0) {
                        src_size = 0;
                        src_mtime = 0;
                    } else {
                        src_size = st_src.st_size;
                        src_mtime = (int64)st_src.st_mtime;
                    }

                    if (lstat(full_dst, &st_dst) < 0) {
                        dst_size = 0;
                        dst_mtime = 0;
                        dst_path = NULL;
                    } else {
                        dst_size = st_dst.st_size;
                        dst_mtime = (int64)st_dst.st_mtime;
                        dst_path = src_path;
                    }

                    // Note: NEVER delete lines with // clang-format
                    // clang-format off
                    if (thread_data->is_preview) {
                        work_send_tree(SIDE_LEFT,
                                      ACTION_IGNORE, REASON_IGNORED,
                                      src_path, dst_path, NULL, ignore_pattern,
                                      src_size, src_mtime, dst_size, dst_mtime,
                                      thread_data->delete_excluded);
                    }
                    // clang-format on
                }
            } else if (might_be_itemize_line
                       && ((action_char == RSYNC_CHAR0_ACTION_RECEIVE)
                           || (action_char == RSYNC_CHAR0_ACTION_HARDLINK)
                           || (action_char == RSYNC_CHAR0_ACTION_CHANGE))) {

                enum CecupAction action = ACTION_UPDATE;
                char *space_pos = strchr(buf_output, ' ');
                enum CecupReason reason = REASON_UPDATE;
                bool attribute_changed = false;

                src_path = space_pos + 1;
                dst_path = NULL;

                while (isspace(*src_path)) {
                    src_path += 1;
                }

                link_target = NULL;
                if (action_char == RSYNC_CHAR0_ACTION_HARDLINK) {
                    char *sep;
                    action = ACTION_HARDLINK;

                    if ((sep = strstr(src_path, RSYNC_HARDLINK_NOTATION))) {
                        *sep = '\0';
                        link_target = sep + strlen32(RSYNC_HARDLINK_NOTATION);
                    }
                } else if (type_char == RSYNC_CHAR1_TYPE_SYMLINK) {
                    char *sep;
                    action = ACTION_SYMLINK;

                    if ((sep = strstr(src_path, RSYNC_SYMLINK_NOTATION))) {
                        *sep = '\0';
                        link_target = sep + strlen32(RSYNC_SYMLINK_NOTATION);
                    }
                } else if (buf_output[2] == '+') {
                    action = ACTION_NEW;
                }

                attribute_changed = did_attribute_change(buf_output);
                if (!attribute_changed) {
                    action = ACTION_EQUAL;
                    reason = REASON_EQUAL;
                } else {
                    reason = (enum CecupReason)action;
                }

                if ((thread_data->is_preview == 0)
                    && (type_char == RSYNC_CHAR1_TYPE_FILE)
                    && ((action_char == RSYNC_CHAR0_ACTION_RECEIVE)
                        || (action_char == RSYNC_CHAR0_ACTION_CHANGE)
                        || (action_char == RSYNC_CHAR0_ACTION_HARDLINK))) {

                    if (checksum_count >= checksum_capacity) {
                        checksum_capacity = (checksum_capacity == 0)
                                                ? 256
                                                : checksum_capacity*2;
                        checksum_files = xrealloc(
                            checksum_files, checksum_capacity*SIZEOF(char *));
                    }
                    checksum_files[checksum_count] = xstrdup(src_path);
                    checksum_count += 1;
                }

                // Note: NEVER delete lines with // clang-format
                // clang-format off
                SNPRINTF(full_src,
                         "%s/%s", cecup.src_base, src_path);
                SNPRINTF(full_dst,
                         "%s/%s", cecup.dst_base, src_path);

                if (lstat(full_src, &st_src) < 0) {
                    src_size = 0;
                    src_mtime = 0;
                } else {
                    src_size = st_src.st_size;
                    src_mtime = (int64)st_src.st_mtime;
                }

                if (lstat(full_dst, &st_dst) < 0) {
                    dst_size = 0;
                    dst_mtime = 0;
                    dst_path = NULL;
                } else {
                    dst_size = st_dst.st_size;
                    dst_mtime = (int64)st_dst.st_mtime;
                    dst_path = src_path;
                }

                if (!(thread_data->filtered && !strcmp(src_path, "./"))) {
                    if (thread_data->is_preview) {
                        work_send_tree(SIDE_LEFT,
                                      action, reason,
                                      src_path, dst_path, link_target, NULL,
                                      src_size, src_mtime, dst_size, dst_mtime,
                                      thread_data->delete_excluded);
                    }

                    processed_files_preview += 1;
                    if (total_files_preview > 0) {
                        ipc_send_progress(DATA_TYPE_PROGRESS_PREVIEW,
                                          (double)processed_files_preview
                                              / (double)total_files_preview);
                    }
                }
                // clang-format on
            } else if (might_be_itemize_line) {
                enum CecupAction action = ACTION_UPDATE;
                enum CecupReason reason = REASON_UPDATE;

                bool attribute_changed = false;
                char *space_pos = strchr(buf_output, ' ');
                src_path = space_pos + 1;
                dst_path = NULL;

                // Note: NEVER delete lines with // clang-format
                // clang-format off
                attribute_changed = did_attribute_change(buf_output);
                if (!attribute_changed) {
                    action = ACTION_EQUAL;
                    reason = REASON_EQUAL;
                }

                while (isspace(*src_path)) {
                    src_path += 1;
                }

                SNPRINTF(full_src, "%s/%s", cecup.src_base, src_path);
                SNPRINTF(full_dst, "%s/%s", cecup.dst_base, src_path);

                if (lstat(full_src, &st_src) < 0) {
                    src_size = 0;
                    src_mtime = 0;
                } else {
                    src_size = st_src.st_size;
                    src_mtime = (int64)st_src.st_mtime;
                }

                if (lstat(full_dst, &st_dst) < 0) {
                    dst_size = 0;
                    dst_mtime = 0;
                    dst_path = NULL;
                } else {
                    dst_size = st_dst.st_size;
                    dst_mtime = (int64)st_dst.st_mtime;
                    dst_path = src_path;
                }

                if (!(thread_data->filtered && !strcmp(src_path, "./"))) {
                    if (thread_data->is_preview) {
                        work_send_tree(SIDE_LEFT, action, reason,
                                      src_path, dst_path, NULL, NULL,
                                      src_size, src_mtime, dst_size, dst_mtime,
                                      thread_data->delete_excluded);
                    }
                }
                // clang-format on
            }

            remaining = buf_output_pos - (line_len + 1);
            if (remaining > 0) {
                memmove64(buf_output, eol + 1, remaining);
            }
            buf_output_pos = remaining;
        }

        if (buf_output_pos >= (int32)SIZEOF(buf_output) - 1) {
            buf_output[buf_output_pos] = '\0';
            IPC_SEND_LOG("%s.\n", buf_output);
            buf_output_pos = 0;
        }

    read_error_pipe:
        if (pipes[1].revents & (POLLHUP | POLLERR)) {
            pipes[1].fd = -1;
            continue;
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

    if (waitpid(child_process_id, NULL, 0) < 0) {
        IPC_SEND_LOG_ERROR("Error waiting for child: %s.\n", strerror(errno));
    }
    cecup.child_pid = 0;

    XCLOSE(&pipe_stderr[0]);
    XCLOSE(&pipe_stdout[0]);

    if (checksum_count <= 0) {
        goto finalize;
    }
    a = 0;
    rsync_args[a++] = "rsync";
    rsync_args[a++] = "--verbose";
    rsync_args[a++] = "--recursive";
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
    rsync_args[a++] = src_base_with_slash;
    rsync_args[a++] = dst_base_with_slash;
    rsync_args[a++] = NULL;

    IPC_SEND_LOG("Verifying transfers with checksum...\n");
    STRING_FROM_ARRAY(cmd, " ", rsync_args, a);
    IPC_SEND_LOG_CMD("%s\n", cmd);

    xpipe(pipe_stdout);
    xpipe(pipe_stderr);

    switch (child_process_id = fork()) {
    case -1:
        IPC_SEND_LOG_ERROR("Error forking for checksum: %s.\n",
                           strerror(errno));
        break;
    case 0:
        setpgid(0, 0);
        putenv("LC_ALL=C");

        XCLOSE(&pipe_stderr[0]);
        XCLOSE(&pipe_stdout[0]);

        dup2(pipe_stdout[1], STDOUT_FILENO);
        dup2(pipe_stderr[1], STDERR_FILENO);

        XCLOSE(&pipe_stderr[1]);
        XCLOSE(&pipe_stdout[1]);

        execvp("rsync", rsync_args);
        _exit(EXIT_FAILURE);
    default:
        cecup.child_pid = child_process_id;
        XCLOSE(&pipe_stderr[1]);
        XCLOSE(&pipe_stdout[1]);
        break;
    }

    {
        int files_from_fd;
        if ((files_from_fd
             = open(files_from_filename, O_WRONLY | O_TRUNC | O_CREAT, 0644))
            < 0) {
            error("Error opening %s: %s.\n", files_from_filename,
                  strerror(errno));
            fatal(EXIT_FAILURE);
        }
        for (int32 i = 0; i < checksum_count; i += 1) {
            write64(files_from_fd, checksum_files[i],
                    strlen32(checksum_files[i]));
            write64(files_from_fd, "\n", 1);
        }
        XCLOSE(&files_from_fd);
    }

    pipes[0].fd = pipe_stdout[0];
    pipes[1].fd = pipe_stderr[0];
    pipes[0].events = POLLIN;
    pipes[1].events = POLLIN;

    do {
        int64 r;
        pipes[0].revents = 0;
        pipes[1].revents = 0;

        switch (poll(pipes, 2, 100)) {
        case -1:
            if (errno != EINTR) {
                IPC_SEND_LOG_ERROR("Error in poll: %s.\n", strerror(errno));
                fatal(EXIT_FAILURE);
            }
            continue;
        case 0:
            continue;
        default:
            break;
        }

        if (pipes[0].revents & (POLLHUP | POLLERR)) {
            pipes[0].fd = -1;
            goto read_error_pipe2;
        }

        if (pipes[0].revents & POLLIN) {
            r = read64(pipe_stdout[0], buf_output, SIZEOF(buf_output) - 1);
            if (r > 0) {
                buf_output[r] = '\0';
                IPC_SEND_LOG("%s", buf_output);
            } else {
                pipes[0].fd = -1;
            }
        }

    read_error_pipe2:
        if (pipes[1].revents & (POLLHUP | POLLERR)) {
            pipes[1].fd = -1;
            continue;
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

    if (waitpid(child_process_id, NULL, 0) < 0) {
        IPC_SEND_LOG_ERROR("Error waiting for rsync: %s.\n", strerror(errno));
    }
    cecup.child_pid = 0;
    XCLOSE(&pipe_stderr[0]);
    XCLOSE(&pipe_stdout[0]);

    for (int32 i = 0; i < checksum_count; i += 1) {
        free(checksum_files[i]);
    }
    free(checksum_files);

    if (thread_data->is_preview) {
        IPC_SEND_LOG("Analysis complete. Review the list and click Apply.\n");
    }

finalize:
    ipc_send_progress(DATA_TYPE_PROGRESS_RSYNC, 1.0);
    ipc_send_progress(DATA_TYPE_PROGRESS_PREVIEW, 1.0);

    {
        Message *message;

        g_mutex_lock(&cecup.ui_arena_mutex);
        message = xarena_push(cecup.ui_arena, ALIGN16(SIZEOF(Message)));
        memset64(message, 0, SIZEOF(Message));
        g_mutex_unlock(&cecup.ui_arena_mutex);

        message->type = DATA_TYPE_ENABLE_BUTTONS;
        g_idle_add(update_ui_handler, message);
    }

    g_mutex_lock(&cecup.ui_arena_mutex);
    arena_pop(cecup.ui_arena, thread_data);
    g_mutex_unlock(&cecup.ui_arena_mutex);
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
    char buf_output[MAX_PATH_LENGTH*2];
    char buf_error[MAX_PATH_LENGTH*2];
    int32 buf_output_pos = 0;
    char *files_from_filename = "/tmp/rsync.txt";
    int files_from_fd;
    char cmd[MAX_PATH_LENGTH*2];

    for (int32 i = 0; i < tasks->count; i += 1) {
        Task *task = tasks->items[i];
        char full_dst_path[MAX_PATH_LENGTH];
        pid_t child_rm;
        int child_status;
        bool removed = false;

        if (task->action != ACTION_DELETE) {
            has_transfers = true;
            continue;
        }

        SNPRINTF(full_dst_path, "%s/%s", cecup.dst_base, task->path);
        switch (child_rm = fork()) {
        case -1:
            error("Error forking for rm: %s.\n", strerror(errno));
            break;
        case 0: {
            char cmd_rm[MAX_PATH_LENGTH];
            char *args_rm[] = {
                "rm",
                "-rf",
                full_dst_path,
                NULL,
            };

            execvp(args_rm[0], args_rm);
            STRING_FROM_ARRAY(cmd_rm, " ", args_rm, LENGTH(args_rm));
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
            int32 path_len;

            g_mutex_lock(&cecup.ui_arena_mutex);
            message = xarena_push(cecup.ui_arena, ALIGN16(SIZEOF(Message)));
            memset64(message, 0, SIZEOF(Message));

            path_len = task->path_len;
            message->path_len = path_len;
            message->src_path
                = xarena_push(cecup.ui_arena, ALIGN16(path_len + 1));
            memcpy64(message->src_path, task->path, path_len + 1);
            g_mutex_unlock(&cecup.ui_arena_mutex);

            message->type = DATA_TYPE_REMOVE_TREE_ROW;
            g_idle_add(update_ui_handler, message);
        }
    }

    if (!has_transfers) {
        goto finalize;
    }

    xpipe(pipe_stdout);
    xpipe(pipe_stderr);

    SNPRINTF(dst_base_with_slash, "%s/", cecup.dst_base);

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
        putenv("LC_ALL=C");

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

    if ((files_from_fd
         = open(files_from_filename, O_WRONLY | O_TRUNC | O_CREAT, 0644))
        < 0) {
        error("Error opening %s: %s.\n", files_from_filename, strerror(errno));
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
            // rsync, when using the --files-from mode,
            // only transfers hard links
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

    pipes[0].fd = pipe_stdout[0];
    pipes[1].fd = pipe_stderr[0];
    pipes[0].events = POLLIN;
    pipes[1].events = POLLIN;

    do {
        int64 r;
        char *eol;

        pipes[0].revents = 0;
        pipes[1].revents = 0;

        switch (poll(pipes, 2, 100)) {
        case -1:
            if (errno != EINTR) {
                IPC_SEND_LOG_ERROR("Error in poll: %s.\n", strerror(errno));
                fatal(EXIT_FAILURE);
            }
            continue;
        case 0:
            continue;
        default:
            break;
        }

        if (pipes[0].revents & (POLLHUP | POLLERR)) {
            pipes[0].fd = -1;
            goto read_error_pipe;
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
            int32 remaining;
            *eol = '\0';

            IPC_SEND_LOG("%s.\n", buf_output);

            if ((line_len > 12) && (buf_output[11] == ' ')) {
                char *filename = buf_output + 12;
                char *sep;
                Message *message;
                int32 path_len = strlen32(filename);

                if ((sep = strstr(filename, RSYNC_HARDLINK_NOTATION))) {
                    *sep = '\0';
                } else if ((sep = strstr(filename, RSYNC_SYMLINK_NOTATION))) {
                    *sep = '\0';
                }

                g_mutex_lock(&cecup.ui_arena_mutex);
                message = xarena_push(cecup.ui_arena, ALIGN16(SIZEOF(Message)));
                memset64(message, 0, SIZEOF(Message));

                message->path_len = path_len;
                message->src_path
                    = xarena_push(cecup.ui_arena, ALIGN16(path_len + 1));
                memcpy64(message->src_path, filename, path_len + 1);
                g_mutex_unlock(&cecup.ui_arena_mutex);

                message->type = DATA_TYPE_REMOVE_TREE_ROW;
                g_idle_add(update_ui_handler, message);
            }

            remaining = buf_output_pos - (line_len + 1);
            if (remaining > 0) {
                memmove64(buf_output, eol + 1, remaining);
            }
            buf_output_pos = remaining;
        }

    read_error_pipe:
        if (pipes[1].revents & (POLLHUP | POLLERR)) {
            pipes[1].fd = -1;
            continue;
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
    cecup.child_pid = 0;

    XCLOSE(&pipe_stdout[0]);
    XCLOSE(&pipe_stderr[0]);

finalize:

{
    Message *message;

    g_mutex_lock(&cecup.ui_arena_mutex);
    message = xarena_push(cecup.ui_arena, ALIGN16(SIZEOF(Message)));
    memset64(message, 0, SIZEOF(Message));
    g_mutex_unlock(&cecup.ui_arena_mutex);

    if (cecup.child_pid != 0) {
        message->type = DATA_TYPE_ENABLE_BUTTONS;
    } else {
        message->type = DATA_TYPE_REGENERATE_PREVIEW;
    }

    g_idle_add(update_ui_handler, message);
}
    free_task_list(tasks);
    return NULL;
}

#if TESTING_work
#include <assert.h>
#include <string.h>
#include <stdio.h>

int
main(void) {
    ASSERT(true);
    exit(EXIT_SUCCESS);
}

#endif

#endif /* WORK_C */
