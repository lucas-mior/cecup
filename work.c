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
#include <signal.h>
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

enum RsyncCharAction {
    RSYNC_CHAR_SEND = '<',
    RSYNC_CHAR_RECEIVE = '>',
    RSYNC_CHAR_CHANGE = 'c',
    RSYNC_CHAR_HARDLINK = 'h',
    RSYNC_CHAR_NO_UPDATE = '.',
    RSYNC_CHAR_MESSAGE = '*',
};

enum RsyncCharType {
    RSYNC_CHAR_FILE = 'f',
    RSYNC_CHAR_DIR = 'd',
    RSYNC_CHAR_SYMLINK = 'L',
    RSYNC_CHAR_DEVICE = 'D',
    RSYNC_CHAR_SPECIAL = 'S',
};

enum RsyncCharAttribute {
    RSYNC_CHAR_CHECKSUM = 'c',
    RSYNC_CHAR_SIZE = 's',
    RSYNC_CHAR_TIME = 't',
    RSYNC_CHAR_PERM = 'p',
    RSYNC_CHAR_OWNER = 'o',
    RSYNC_CHAR_GROUP = 'g',
    RSYNC_CHAR_ACL = 'a',
    RSYNC_CHAR_XATTR = 'x'
};

#define RSYNC_HARDLINK_NOTATION " => "
#define RSYNC_SYMLINK_NOTATION " -> "
#define RSYNC_UNIVERSAL_ARGS "--verbose --update --recursive" \
                             " --partial --progress --info=progress2" \
                             " --links --hard-links --itemize-changes" \
                             " --perms --times --owner --group"
#define MAX_COMMAND_LENGTH (MAX_PATH_LENGTH*2 + strlen32(RSYNC_UNIVERSAL_ARGS)*2)
#define BATCH_SIZE 256

static int64
work_count_files_recursive(char *base_path, char *relative_path) {
    DIR *dir;
    struct dirent *entry;
    char full_path[MAX_PATH_LENGTH];
    int64 count;

    if (cecup.cancel_sync) {
        return 0;
    }

    count = 0;
    if (relative_path) {
        SNPRINTF(full_path, "%s/%s", base_path, relative_path);
    } else {
        SNPRINTF(full_path, "%s", base_path);
    }

    if ((dir = opendir(full_path)) == NULL) {
        ipc_send_log_error("Error opening directory %s: %s.\n", full_path,
                           strerror(errno));
        return 0;
    }

    errno = 0;
    while ((entry = readdir(dir))) {
        char *name;
        char sub_rel[MAX_PATH_LENGTH];
        struct stat st;

        if (cecup.cancel_sync) {
            break;
        }

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
            ipc_send_log_error("Error lstat %s: %s.\n", full_path,
                               strerror(errno));
        }
        errno = 0;
    }

    if (errno) {
        ipc_send_log_error("Error reading directory entry in %s: %s.\n",
                           full_path, strerror(errno));
    }

    if (closedir(dir) < 0) {
        ipc_send_log_error("Error closing directory %s: %s.\n", full_path,
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

    if (cecup.cancel_sync) {
        return;
    }

    if (relative_path) {
        SNPRINTF(full_path, "%s/%s", base_path, relative_path);
    } else {
        SNPRINTF(full_path, "%s", base_path);
    }

    if ((dir = opendir(full_path)) == NULL) {
        error("Error opening directory %s: %s.\n", full_path, strerror(errno));
        error("Warning: Problematic file names will not be renamed.\n");
        error("This is only a problem if you have problematic filenames.\n");
        error("Problematic filenames are the ones that contain the strings:\n");
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
            if (relative_path[0]) {
                SNPRINTF(new_full, "%s/%s/%s", base_path, relative_path,
                         new_name);
            } else {
                SNPRINTF(new_full, "%s/%s", base_path, new_name);
            }

            if (access(new_full, F_OK) == 0) {
                ipc_send_log_error("Skip rename: %s already exists.\n",
                                   new_name);
            } else if (rename(old_full, new_full) == 0) {
                ipc_send_log("Fixed: %s -> %s\n", d_name, new_name);
                if (S_ISDIR(st.st_mode)) {
                    if (relative_path) {
                        SNPRINTF(sub_rel, "%s/%s", relative_path, new_name);
                    } else {
                        SNPRINTF(sub_rel, "%s", new_name);
                    }
                }
            } else {
                ipc_send_log_error("Error renaming %s to %s: %s\n", old_full,
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

    ipc_send_log("Checking for problematic names in the original folder...\n");
    work_fix_fs_recursive(cecup.src_base, NULL);
    ipc_send_log("Checking for problematic names in the backup folder...\n");
    work_fix_fs_recursive(cecup.dst_base, NULL);
    ipc_send_log("Name correction finished.\n");

    g_mutex_lock(&cecup.ui_arena_mutex);
    message = xarena_push(cecup.ui_arena, ALIGN16(SIZEOF(Message)));
    memset64(message, 0, SIZEOF(Message));
    g_mutex_unlock(&cecup.ui_arena_mutex);

    message->type = DATA_TYPE_ENABLE_BUTTONS;
    g_idle_add(update_ui_handler, message);

    g_mutex_lock(&cecup.ui_arena_mutex);
    arena_pop(cecup.ui_arena, thread_data);
    g_mutex_unlock(&cecup.ui_arena_mutex);
    return NULL;
}

static void *
work_rsync(void *user_data) {
    ThreadData *thread_data = user_data;
    int64 total_files_preview = 0;
    int64 processed_files_preview = 0;

    int32 pipe_stdout[2] = {-1, -1};
    int32 pipe_stderr[2] = {-1, -1};
    struct pollfd pipes[2];
    pid_t child_pid;

    char buffer_output[MAX_PATH_LENGTH*2];
    int32 buffer_output_pos = 0;
    char buffer_error[MAX_PATH_LENGTH*2];
    int32 buffer_error_pos = 0;

    char src_dir[MAX_PATH_LENGTH];
    char dst_dir[MAX_PATH_LENGTH];
    char *rsync_args[32];
    int32 a = 0;
    char cmd[MAX_PATH_LENGTH*2];

    char **checksum_files = NULL;
    int32 checksum_count = 0;
    int32 checksum_capacity = 0;

    if (thread_data->check_different_fs) {
        struct stat stat_src;
        struct stat stat_dst;

        if (stat(cecup.src_base, &stat_src) < 0) {
            ipc_send_log_error("Error checking %s: %s.\n", cecup.src_base,
                               strerror(errno));
            goto finalize;
        }
        if (stat(cecup.dst_base, &stat_dst) < 0) {
            ipc_send_log_error("Error checking %s: %s.\n", cecup.dst_base,
                               strerror(errno));
            goto finalize;
        }

        if (stat_src.st_dev == stat_dst.st_dev) {
            Message *message;
            ipc_send_log_error(
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

    if (thread_data->is_preview) {
        Message *message;

        g_mutex_lock(&cecup.ui_arena_mutex);
        message = xarena_push(cecup.ui_arena, ALIGN16(SIZEOF(Message)));
        memset64(message, 0, SIZEOF(Message));
        g_mutex_unlock(&cecup.ui_arena_mutex);

        message->type = DATA_TYPE_CLEAR_TREES;
        g_idle_add(update_ui_handler, message);

        ipc_send_log("Counting files to prepare analysis...\n");
        total_files_preview = work_count_files_recursive(cecup.src_base, NULL);
        ipc_send_log("Found %lld files to analyse...\n",
                     (llong)total_files_preview);
    }

    if (cecup.cancel_sync) {
        goto finalize;
    }

    if (pipe(pipe_stdout) < 0) {
        error("Error creating pipe for stdout: %s.\n", strerror(errno));
        fatal(EXIT_FAILURE);
    }
    if (pipe(pipe_stderr) < 0) {
        error("Error creating pipe for stderr: %s.\n", strerror(errno));
        fatal(EXIT_FAILURE);
    }

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
    if (access(cecup.ignore_path, F_OK) != -1) {
        rsync_args[a++] = "--exclude-from";
        rsync_args[a++] = cecup.ignore_path;
    }

    SNPRINTF(src_dir, "%s/", cecup.src_base);
    SNPRINTF(dst_dir, "%s/", cecup.dst_base);
    rsync_args[a++] = src_dir;
    rsync_args[a++] = dst_dir;
    rsync_args[a++] = NULL;

    STRING_FROM_ARRAY(cmd, " ", rsync_args, a);
    ipc_send_log("+ %s\n", cmd);

    switch (child_pid = fork()) {
    case -1:
        error("Error forking: %s.\n", strerror(errno));
        exit(EXIT_FAILURE);
    case 0:
        if (setpgid(0, 0) < 0) {
            error("Error setpgid: %s.\n", strerror(errno));
            fatal(EXIT_FAILURE);
        }
        putenv("LC_ALL=C");
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
        XCLOSE(&pipe_stdout[1]);
        XCLOSE(&pipe_stderr[1]);

        execvp(rsync_args[0], rsync_args);
        error("Error executing\n%s\n%s.", cmd, strerror(errno));
        _exit(EXIT_FAILURE);
    default:
        break;
    }

    XCLOSE(&pipe_stdout[1]);
    XCLOSE(&pipe_stderr[1]);

    do {
        int64 r;
        char *eol;

        pipes[0].fd = pipe_stdout[0];
        pipes[0].events = POLLIN;
        pipes[1].fd = pipe_stderr[0];
        pipes[1].events = POLLIN;

        if (cecup.cancel_sync) {
            if (kill(-child_pid, SIGTERM) < 0) {
                ipc_send_log_error("Error killing process group: %s.\n",
                                   strerror(errno));
            }
            ipc_send_log_error("Operation stopped by user.\n");
            break;
        }

        switch (poll(pipes, 2, 100)) {
        case -1:
            if (errno != EINTR) {
                ipc_send_log_error("Error in poll: %s.\n", strerror(errno));
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

        r = read64(pipe_stdout[0], buffer_output + buffer_output_pos,
                   SIZEOF(buffer_output) - buffer_output_pos - 1);
        if (r <= 0) {
            if (r < 0) {
                ipc_send_log_error("Error reading stdout pipe: %s.\n",
                                   strerror(errno));
                pipes[0].fd = -1;
            }
            goto read_error_pipe;
        }
        buffer_output_pos += (int32)r;

        while (
            buffer_output_pos > 0
            && ((eol = memchr64(buffer_output, '\n', buffer_output_pos))
                || (eol = memchr64(buffer_output, '\r', buffer_output_pos)))) {
            char *space_pos;
            char *link_target;
            char full_src_path_val[MAX_PATH_LENGTH];
            int64 size_path_val = 0;
            int64 mtime_path_val = 0;
            int32 line_len = (int32)(eol - buffer_output);
            int32 remaining;

            *eol = '\0';

            {
                char *percent_pos;
                if ((percent_pos = strstr(buffer_output, "%"))) {
                    char *start_digit = percent_pos;
                    while (start_digit > buffer_output
                           && isdigit(*(start_digit - 1))) {
                        start_digit -= 1;
                    }
                    ipc_send_progress(DATA_TYPE_PROGRESS_RSYNC,
                                      atof(start_digit) / 100.0);
                }
            }

            if (thread_data->is_preview == 0) {
                ipc_send_log("%s.\n", buffer_output);
            } else if (buffer_output[0] == RSYNC_CHAR_MESSAGE
                       && strncmp(buffer_output + 1, "deleting", 8) == 0) {
                char *relative_path = buffer_output + 10;
                char full_src[MAX_PATH_LENGTH];
                char full_dst[MAX_PATH_LENGTH];
                struct stat stat_src_local;
                struct stat stat_dst_local;
                int64 size_val;
                int64 time_val;
                enum CecupReason deletion_reason;

                while (isspace(*relative_path)) {
                    relative_path += 1;
                }

                SNPRINTF(full_src, "%s/%s", cecup.src_base, relative_path);
                SNPRINTF(full_dst, "%s/%s", cecup.dst_base, relative_path);

                if (lstat(full_dst, &stat_dst_local) == 0) {
                    size_val = stat_dst_local.st_size;
                    time_val = (int64)stat_dst_local.st_mtime;
                } else {
                    size_val = 0;
                    time_val = 0;
                }

                if (lstat(full_src, &stat_src_local) == 0) {
                    deletion_reason = UI_REASON_IGNORED;
                } else {
                    deletion_reason = UI_REASON_MISSING;
                }

                ipc_send_tree(SIDE_RIGHT, UI_ACTION_DELETE, deletion_reason,
                              relative_path, NULL, NULL, size_val, time_val);
            } else if (strncmp(buffer_output, "[sender] hiding file ", 21)
                       == 0) {
                char *hiding_filename = buffer_output + 21;
                char *reason_sep;
                char *ignore_pattern = NULL;
                struct stat st_hiding;

                if ((reason_sep
                     = strstr(hiding_filename, " because of pattern "))) {
                    *reason_sep = '\0';
                    ignore_pattern = reason_sep + 20;

                    SNPRINTF(full_src_path_val, "%s/%s", cecup.src_base,
                             hiding_filename);

                    if (lstat(full_src_path_val, &st_hiding) == 0) {
                        size_path_val = st_hiding.st_size;
                        mtime_path_val = (int64)st_hiding.st_mtime;
                    }

                    // Note: NEVER delete lines with // clang-format
                    // clang-format off
                    ipc_send_tree(SIDE_LEFT,
                                  UI_ACTION_IGNORE, UI_REASON_IGNORED,
                                  hiding_filename, NULL, ignore_pattern,
                                  size_path_val, mtime_path_val);
                    // clang-format on
                }
            } else if ((space_pos = strchr(buffer_output, ' '))) {
                char type_char = buffer_output[0];
                if ((type_char == RSYNC_CHAR_RECEIVE)
                    || (type_char == RSYNC_CHAR_NO_UPDATE)
                    || (type_char == RSYNC_CHAR_HARDLINK)
                    || (type_char == RSYNC_CHAR_CHANGE)
                    || (type_char == RSYNC_CHAR_SYMLINK)) {

                    char *relative_path_entry = space_pos + 1;
                    enum CecupAction cecup_action = UI_ACTION_UPDATE;
                    struct stat st_path_val;

                    while (isspace(*relative_path_entry)) {
                        relative_path_entry += 1;
                    }

                    link_target = NULL;
                    if (type_char == RSYNC_CHAR_HARDLINK) {
                        char *sep;
                        cecup_action = UI_ACTION_HARDLINK;

                        if ((sep = strstr(relative_path_entry,
                                          RSYNC_HARDLINK_NOTATION))) {
                            *sep = '\0';
                            link_target
                                = sep + strlen32(RSYNC_HARDLINK_NOTATION);
                        }
                    } else if (type_char == RSYNC_CHAR_SYMLINK
                               || buffer_output[1] == RSYNC_CHAR_SYMLINK) {
                        char *sep;
                        cecup_action = UI_ACTION_SYMLINK;

                        if ((sep = strstr(relative_path_entry,
                                          RSYNC_SYMLINK_NOTATION))) {
                            *sep = '\0';
                            link_target
                                = sep + strlen32(RSYNC_SYMLINK_NOTATION);
                        }
                    } else if (buffer_output[2] == '+') {
                        cecup_action = UI_ACTION_NEW;
                    }

                    if (thread_data->is_preview == 0 && line_len > 11
                        && buffer_output[1] == RSYNC_CHAR_FILE
                        && (type_char == RSYNC_CHAR_RECEIVE
                            || type_char == RSYNC_CHAR_CHANGE
                            || type_char == RSYNC_CHAR_HARDLINK)) {

                        if (checksum_count >= checksum_capacity) {
                            checksum_capacity = (checksum_capacity == 0)
                                                    ? 256
                                                    : checksum_capacity*2;
                            checksum_files
                                = xrealloc(checksum_files,
                                           checksum_capacity*SIZEOF(char *));
                        }
                        checksum_files[checksum_count]
                            = xstrdup(relative_path_entry);
                        checksum_count += 1;
                    }

                    SNPRINTF(full_src_path_val, "%s/%s", cecup.src_base,
                             relative_path_entry);

                    if (lstat(full_src_path_val, &st_path_val) < 0) {
                        ipc_send_log_error("Error lstat %s: %s.\n",
                                           full_src_path_val, strerror(errno));
                    } else {
                        size_path_val = st_path_val.st_size;
                        mtime_path_val = (int64)st_path_val.st_mtime;
                    }

                    ipc_send_tree(SIDE_LEFT, cecup_action,
                                  (enum CecupReason)cecup_action,
                                  relative_path_entry, link_target, NULL,
                                  size_path_val, mtime_path_val);

                    processed_files_preview += 1;
                    if (total_files_preview > 0) {
                        ipc_send_progress(DATA_TYPE_PROGRESS_PREVIEW,
                                          (double)processed_files_preview
                                              / (double)total_files_preview);
                    }
                }
            }

            remaining = buffer_output_pos - (line_len + 1);
            if (remaining > 0) {
                memmove64(buffer_output, eol + 1, remaining);
            }
            buffer_output_pos = remaining;
        }

        if (buffer_output_pos >= (int32)SIZEOF(buffer_output) - 1) {
            buffer_output[buffer_output_pos] = '\0';
            ipc_send_log("%s.\n", buffer_output);
            buffer_output_pos = 0;
        }

    read_error_pipe:
        if (pipes[1].revents & (POLLHUP | POLLERR)) {
            pipes[1].fd = -1;
            continue;
        }

        if (!(pipes[1].revents & POLLIN)) {
            continue;
        }

        r = read64(pipe_stderr[0], buffer_error + buffer_error_pos,
                   SIZEOF(buffer_error) - buffer_error_pos - 1);
        if (r <= 0) {
            if (r < 0) {
                ipc_send_log_error("Error reading stderr pipe: %s.\n",
                                   strerror(errno));
                pipes[1].fd = -1;
            }
            continue;
        }
        buffer_error_pos += (int32)r;

        while (buffer_error_pos > 0
               && ((eol = memchr64(buffer_error, '\n', buffer_error_pos))
                   || (eol = memchr64(buffer_error, '\r', buffer_error_pos)))) {
            int32 line_len = (int32)(eol - buffer_error);
            int32 remaining;
            *eol = '\0';

            if (buffer_error[0] != '\0') {
                ipc_send_log_error("%s\n", buffer_error);
            }

            remaining = buffer_error_pos - (line_len + 1);
            if (remaining > 0) {
                memmove64(buffer_error, eol + 1, remaining);
            }
            buffer_error_pos = remaining;
        }

        if (buffer_error_pos >= (int32)SIZEOF(buffer_error) - 1) {
            buffer_error[buffer_error_pos] = '\0';
            ipc_send_log_error("%s\n", buffer_error);
            buffer_error_pos = 0;
        }
    } while ((pipes[0].fd >= 0) || (pipes[1].fd >= 0));

    if (waitpid(child_pid, NULL, 0) < 0) {
        ipc_send_log_error("Error waiting for child: %s.\n", strerror(errno));
    }

    XCLOSE(&pipe_stdout[0]);
    XCLOSE(&pipe_stderr[0]);

    if (checksum_count > 0 && cecup.cancel_sync == false) {
        int32 pipe_stdin[2] = {-1, -1};
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
        rsync_args[a++] = "--files-from=-";
        rsync_args[a++] = src_dir;
        rsync_args[a++] = dst_dir;
        rsync_args[a++] = NULL;

        ipc_send_log("Verifying transfers with checksum...\n");

        if (pipe(pipe_stdout) < 0) {
            error("Error creating pipe for stdout: %s.\n", strerror(errno));
            fatal(EXIT_FAILURE);
        }
        if (pipe(pipe_stderr) < 0) {
            error("Error creating pipe for stderr: %s.\n", strerror(errno));
            fatal(EXIT_FAILURE);
        }
        if (pipe(pipe_stdin) < 0) {
            error("Error creating pipe for stderr: %s.\n", strerror(errno));
            fatal(EXIT_FAILURE);
        }

        switch (child_pid = fork()) {
        case -1:
            ipc_send_log_error("Error forking for checksum: %s.\n",
                               strerror(errno));
            break;
        case 0:
            setpgid(0, 0);
            putenv("LC_ALL=C");

            XCLOSE(&pipe_stdin[1]);
            XCLOSE(&pipe_stdout[0]);
            XCLOSE(&pipe_stderr[0]);

            dup2(pipe_stdin[0], STDIN_FILENO);
            dup2(pipe_stdout[1], STDOUT_FILENO);
            dup2(pipe_stderr[1], STDERR_FILENO);

            XCLOSE(&pipe_stdin[0]);
            XCLOSE(&pipe_stdout[1]);
            XCLOSE(&pipe_stderr[1]);

            execvp("rsync", rsync_args);
            _exit(EXIT_FAILURE);
        default:
            break;
        }
        XCLOSE(&pipe_stdin[0]);
        XCLOSE(&pipe_stdout[1]);
        XCLOSE(&pipe_stderr[1]);
        for (int32 i = 0; i < checksum_count; i += 1) {
            write64(pipe_stdin[1], checksum_files[i],
                    strlen32(checksum_files[i]));
            write64(pipe_stdin[1], "\n", 1);
        }
        XCLOSE(&pipe_stdin[1]);

        do {
            int64 r;
            pipes[0].fd = pipe_stdout[0];
            pipes[0].events = POLLIN;
            pipes[1].fd = pipe_stderr[0];
            pipes[1].events = POLLIN;

            if (poll(pipes, 2, 100) <= 0) {
                continue;
            }
            if (pipes[0].revents & POLLIN) {
                r = read64(pipe_stdout[0], buffer_output,
                           SIZEOF(buffer_output) - 1);
                if (r > 0) {
                    buffer_output[r] = '\0';
                    ipc_send_log("%s", buffer_output);
                } else {
                    pipes[0].fd = -1;
                }
            }
            if (pipes[1].revents & POLLIN) {
                r = read64(pipe_stderr[0], buffer_error,
                           SIZEOF(buffer_error) - 1);
                if (r > 0) {
                    buffer_error[r] = '\0';
                    ipc_send_log_error("%s", buffer_error);
                } else {
                    pipes[1].fd = -1;
                }
            }
        } while ((pipes[0].fd >= 0) || (pipes[1].fd >= 0));
        if (waitpid(child_pid, NULL, 0) < 0) {
            ipc_send_log_error("Error waiting for rsync: %s.\n",
                               strerror(errno));
        }
        XCLOSE(&pipe_stdout[0]);
        XCLOSE(&pipe_stderr[0]);
    }

    for (int32 i = 0; i < checksum_count; i += 1) {
        free(checksum_files[i]);
    }
    free(checksum_files);

    if (!cecup.cancel_sync) {
        ipc_send_log("Analysis complete. Review the list and click Apply.\n");
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

    if (cecup.cancel_sync == false) {
        for (int32 i = 0; i < tasks->count; i += 1) {
            Message *task = tasks->items[i];
            char full_dst_path[MAX_PATH_LENGTH];
            pid_t child_rm;

            if (task->action != UI_ACTION_DELETE) {
                has_transfers = true;
                continue;
            }

            SNPRINTF(full_dst_path, "%s/%s", cecup.dst_base, task->filepath);
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
                waitpid(child_rm, NULL, 0);
                break;
            }

            if (cecup.cancel_sync == false) {
                Message *message;
                int32 path_len;

                g_mutex_lock(&cecup.ui_arena_mutex);
                message = xarena_push(cecup.ui_arena, ALIGN16(SIZEOF(Message)));
                memset64(message, 0, SIZEOF(Message));

                path_len = message->filepath_len;
                message->filepath_len = path_len;
                message->filepath
                    = xarena_push(cecup.ui_arena, ALIGN16(path_len + 1));
                memcpy64(message->filepath, message->filepath, path_len + 1);
                g_mutex_unlock(&cecup.ui_arena_mutex);

                message->type = DATA_TYPE_REMOVE_TREE_ROW;
                g_idle_add(update_ui_handler, message);
            }
        }
    }

    if (has_transfers && (cecup.cancel_sync == false)) {
        int32 pipe_stdout[2] = {-1, -1};
        int32 pipe_stderr[2] = {-1, -1};
        int32 pipe_stdin[2] = {-1, -1};
        struct pollfd pipes[2];
        pid_t child_pid;
        char dst_directory[MAX_PATH_LENGTH];
        char *rsync_args[32];
        int32 a = 0;
        char buffer_output[MAX_PATH_LENGTH*2];
        char buffer_error[MAX_PATH_LENGTH*2];
        int32 buffer_output_pos = 0;
        int32 buffer_error_pos = 0;

        if (pipe(pipe_stdout) < 0) {
            error("Error creating pipe for stdout: %s.\n", strerror(errno));
            fatal(EXIT_FAILURE);
        }
        if (pipe(pipe_stderr) < 0) {
            error("Error creating pipe for stderr: %s.\n", strerror(errno));
            fatal(EXIT_FAILURE);
        }
        if (pipe(pipe_stdin) < 0) {
            error("Error creating pipe for stdin: %s.\n", strerror(errno));
            fatal(EXIT_FAILURE);
        }

        SNPRINTF(dst_directory, "%s/", cecup.dst_base);

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
        rsync_args[a++] = "--files-from=-";
        rsync_args[a++] = cecup.src_base;
        rsync_args[a++] = dst_directory;
        rsync_args[a++] = NULL;

        {
            char cmd[MAX_PATH_LENGTH*2];
            STRING_FROM_ARRAY(cmd, " ", rsync_args, a);
            ipc_send_log("+ %s\n", cmd);
        }

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

            XCLOSE(&pipe_stdout[0]);
            XCLOSE(&pipe_stderr[0]);
            XCLOSE(&pipe_stdin[1]);

            if (dup2(pipe_stdin[0], STDIN_FILENO) < 0) {
                error("Error duplicating stdin: %s.\n", strerror(errno));
                fatal(EXIT_FAILURE);
            }
            if (dup2(pipe_stdout[1], STDOUT_FILENO) < 0) {
                error("Error duplicating stdout: %s.\n", strerror(errno));
                fatal(EXIT_FAILURE);
            }
            if (dup2(pipe_stderr[1], STDERR_FILENO) < 0) {
                error("Error duplicating stderr: %s.\n", strerror(errno));
                fatal(EXIT_FAILURE);
            }

            XCLOSE(&pipe_stdin[0]);
            XCLOSE(&pipe_stdout[1]);
            XCLOSE(&pipe_stderr[1]);

            execvp("rsync", rsync_args);
            error("Error: execvp failed: %s.\n", strerror(errno));
            _exit(EXIT_FAILURE);
        default:
            break;
        }

        XCLOSE(&pipe_stdout[1]);
        XCLOSE(&pipe_stderr[1]);
        XCLOSE(&pipe_stdin[0]);

        for (int32 i = 0; i < tasks->count; i += 1) {
            Message *task = tasks->items[i];
            switch (task->action) {
            case UI_ACTION_DELETE:
            case UI_ACTION_DELETED:
            case UI_ACTION_IGNORE:
            case UI_ACTION_EQUAL:
                continue;
            case UI_ACTION_HARDLINK:
                // rsync, when using the --files-from mode,
                // only transfers hard links
                // if the target is also included in the --files-from list
                write64(pipe_stdin[1], task->link_target,
                        task->link_target_len);
                write64(pipe_stdin[1], "\n", 1);
                __attribute__((fallthrough));
            case UI_ACTION_NEW:
            case UI_ACTION_UPDATE:
            case UI_ACTION_SYMLINK:
            case NUM_UI_ACTIONS:
            default:
                write64(pipe_stdin[1], task->filepath, task->filepath_len);
                write64(pipe_stdin[1], "\n", 1);
            }
        }
        XCLOSE(&pipe_stdin[1]);

        do {
            int64 r;
            char *eol;

            pipes[0].fd = pipe_stdout[0];
            pipes[0].events = POLLIN;
            pipes[1].fd = pipe_stderr[0];
            pipes[1].events = POLLIN;

            if (cecup.cancel_sync) {
                if (kill(-child_pid, SIGTERM) < 0) {
                    ipc_send_log_error("Error killing process group: %s.\n",
                                       strerror(errno));
                }
                ipc_send_log_error("Operation stopped by user.\n");
                break;
            }

            switch (poll(pipes, 2, 100)) {
            case -1:
                if (errno != EINTR) {
                    ipc_send_log_error("Error in poll: %s.\n", strerror(errno));
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

            r = read64(pipe_stdout[0], buffer_output + buffer_output_pos,
                       SIZEOF(buffer_output) - buffer_output_pos - 1);
            if (r <= 0) {
                if (r < 0) {
                    ipc_send_log_error("Error reading stdout pipe: %s.\n",
                                       strerror(errno));
                    pipes[0].fd = -1;
                }
                goto read_error_pipe;
            }
            buffer_output_pos += (int32)r;

            while (buffer_output_pos > 0
                   && ((eol = memchr64(buffer_output, '\n', buffer_output_pos))
                       || (eol = memchr64(buffer_output, '\r',
                                          buffer_output_pos)))) {
                int32 line_len = (int32)(eol - buffer_output);
                int32 remaining;
                *eol = '\0';

                ipc_send_log("%s.\n", buffer_output);

                if ((line_len > 12) && (buffer_output[11] == ' ')) {
                    char *filename = buffer_output + 12;
                    char *sep;
                    Message *message;
                    int32 path_len = strlen32(filename);

                    if ((sep = strstr(filename, RSYNC_HARDLINK_NOTATION))) {
                        *sep = '\0';
                    } else if ((sep
                                = strstr(filename, RSYNC_SYMLINK_NOTATION))) {
                        *sep = '\0';
                    }

                    g_mutex_lock(&cecup.ui_arena_mutex);
                    message
                        = xarena_push(cecup.ui_arena, ALIGN16(SIZEOF(Message)));
                    memset64(message, 0, SIZEOF(Message));

                    message->filepath_len = path_len;
                    message->filepath
                        = xarena_push(cecup.ui_arena, ALIGN16(path_len + 1));
                    memcpy64(message->filepath, filename, path_len + 1);
                    g_mutex_unlock(&cecup.ui_arena_mutex);

                    message->type = DATA_TYPE_REMOVE_TREE_ROW;
                    g_idle_add(update_ui_handler, message);
                }

                remaining = buffer_output_pos - (line_len + 1);
                if (remaining > 0) {
                    memmove64(buffer_output, eol + 1, remaining);
                }
                buffer_output_pos = remaining;
            }

        read_error_pipe:
            if (pipes[1].revents & (POLLHUP | POLLERR)) {
                pipes[1].fd = -1;
                continue;
            }

            if (!(pipes[1].revents & POLLIN)) {
                continue;
            }

            r = read64(pipe_stderr[0], buffer_error + buffer_error_pos,
                       SIZEOF(buffer_error) - buffer_error_pos - 1);
            if (r <= 0) {
                if (r < 0) {
                    ipc_send_log_error("Error reading stderr pipe: %s.\n",
                                       strerror(errno));
                    pipes[1].fd = -1;
                }
                continue;
            }
            buffer_error_pos += (int32)r;

            while (buffer_error_pos > 0
                   && ((eol = memchr64(buffer_error, '\n', buffer_error_pos))
                       || (eol
                           = memchr64(buffer_error, '\r', buffer_error_pos)))) {
                int32 line_len = (int32)(eol - buffer_error);
                int32 remaining;
                *eol = '\0';

                ipc_send_log_error("%s\n", buffer_error);

                remaining = buffer_error_pos - (line_len + 1);
                if (remaining > 0) {
                    memmove64(buffer_error, eol + 1, remaining);
                }
                buffer_error_pos = remaining;
            }

        } while ((pipes[0].fd >= 0) || (pipes[1].fd >= 0));

        if (waitpid(child_pid, NULL, 0) < 0) {
            ipc_send_log_error("Error waiting for child: %s.\n",
                               strerror(errno));
        }

        XCLOSE(&pipe_stdout[0]);
        XCLOSE(&pipe_stderr[0]);
    }

    {
        Message *message;

        g_mutex_lock(&cecup.ui_arena_mutex);
        message = xarena_push(cecup.ui_arena, ALIGN16(SIZEOF(Message)));
        memset64(message, 0, SIZEOF(Message));
        g_mutex_unlock(&cecup.ui_arena_mutex);

        if (cecup.cancel_sync) {
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
