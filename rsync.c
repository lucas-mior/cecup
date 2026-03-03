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

#if !defined(RSYNC_C)
#define RSYNC_C

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

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_rsync 1
#elif !defined(TESTING_rsync)
#define TESTING_rsync 0
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
#define MAX_COMMAND_LENGTH (MAX_PATH_LENGTH*2 + strlen64(RSYNC_UNIVERSAL_ARGS)*2)
#define SIDE_LEFT 0
#define SIDE_RIGHT 1
#define BATCH_SIZE 256

static void
dispatch_log(char *format, ...) {
    UIUpdateData *data;
    va_list va_args;
    char buffer[8192];
    int64 n;

    va_start(va_args, format);
    n = vsnprintf(buffer, SIZEOF(buffer), format, va_args);
    va_end(va_args);

    if ((n <= 0) || (n >= SIZEOF(buffer))) {
        error("Error in vsnprintf(%s) (n = %lld)\n", format, (llong)n);
        exit(EXIT_FAILURE);
    }

    g_mutex_lock(&cecup.ui_arena_mutex);
    data = xarena_push(cecup.ui_arena, ALIGN16(SIZEOF(UIUpdateData)));
    memset64(data, 0, SIZEOF(UIUpdateData));

    data->message_len = n;
    data->message = xarena_push(cecup.ui_arena, ALIGN16(n + 1));
    memcpy64(data->message, buffer, n + 1);
    g_mutex_unlock(&cecup.ui_arena_mutex);

    data->type = DATA_TYPE_LOG;
    g_idle_add(update_ui_handler, data);
    return;
}

static void
dispatch_log_error(char *format, ...) {
    UIUpdateData *data;
    va_list va_args;
    char buffer[8192];
    int64 n;

    va_start(va_args, format);
    n = vsnprintf(buffer, SIZEOF(buffer), format, va_args);
    va_end(va_args);

    if ((n <= 0) || (n >= SIZEOF(buffer))) {
        error("Error in vsnprintf(%s) (n = %lld)\n", format, (llong)n);
        exit(EXIT_FAILURE);
    }

    g_mutex_lock(&cecup.ui_arena_mutex);
    data = xarena_push(cecup.ui_arena, ALIGN16(SIZEOF(UIUpdateData)));
    memset64(data, 0, SIZEOF(UIUpdateData));

    data->message_len = n;
    data->message = xarena_push(cecup.ui_arena, ALIGN16(n + 1));
    memcpy64(data->message, buffer, n + 1);
    g_mutex_unlock(&cecup.ui_arena_mutex);

    data->type = DATA_TYPE_LOG_ERROR;
    g_idle_add(update_ui_handler, data);
    return;
}

static void
dispatch_progress(enum DataType type, double fraction) {
    UIUpdateData *data;
    static double last_fractions[4] = {0.0, 0.0, 0.0, 0.0};
    int32 index = 0;

    if (type == DATA_TYPE_PROGRESS_RSYNC) {
        index = 1;
    } else if (type == DATA_TYPE_PROGRESS_PREVIEW) {
        index = 3;
    }

    if ((fraction < 1.0) && ((fraction - last_fractions[index]) < 0.001)
        && ((fraction - last_fractions[index]) > -0.001)) {
        return;
    }
    last_fractions[index] = fraction;

    g_mutex_lock(&cecup.ui_arena_mutex);
    data = xarena_push(cecup.ui_arena, ALIGN16(SIZEOF(UIUpdateData)));
    memset64(data, 0, SIZEOF(UIUpdateData));
    g_mutex_unlock(&cecup.ui_arena_mutex);

    data->type = type;
    data->fraction = fraction;
    g_idle_add(update_ui_handler, data);
    return;
}

// Note: NEVER delete lines with // clang-format
// clang-format off
static void
dispatch_tree(int32 side,
              enum CecupAction action, enum CecupReason reason,
              char *path, char *link_target,
              int64 size, int64 mtime) {
    // clang-format on
    UIUpdateData *data;
    int64 target_len;

    g_mutex_lock(&cecup.ui_arena_mutex);
    data = xarena_push(cecup.ui_arena, ALIGN16(SIZEOF(UIUpdateData)));
    memset64(data, 0, SIZEOF(UIUpdateData));

    data->filepath_length = strlen64(path);
    g_mutex_lock(&cecup.row_arena_mutex);
    data->filepath
        = xarena_push(cecup.row_arena, ALIGN16(data->filepath_length + 1));
    memcpy64(data->filepath, path, data->filepath_length + 1);

    if (link_target) {
        target_len = strlen64(link_target);
        data->link_target_len = target_len;
        data->link_target
            = xarena_push(cecup.row_arena, ALIGN16(target_len + 1));
        memcpy64(data->link_target, link_target, target_len + 1);
    }
    g_mutex_unlock(&cecup.row_arena_mutex);
    g_mutex_unlock(&cecup.ui_arena_mutex);

    data->type = DATA_TYPE_TREE_ROW;
    data->side = side;
    data->action = action;
    data->reason = reason;
    data->size = size;
    data->mtime = mtime;
    g_idle_add(update_ui_handler, data);
    return;
}

static int64
count_files_recursive(char *base_path, char *relative_path) {
    DIR *dir;
    struct dirent *entry;
    char full_path[MAX_PATH_LENGTH];
    int64 count;

    if (cecup.cancel_sync) {
        return 0;
    }

    count = 0;
    if (relative_path[0] == '\0') {
        SNPRINTF(full_path, "%s", base_path);
    } else {
        SNPRINTF(full_path, "%s/%s", base_path, relative_path);
    }

    if ((dir = opendir(full_path)) == NULL) {
        dispatch_log_error("Error opening directory %s: %s.\n", full_path,
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

        if (relative_path[0] != '\0') {
            SNPRINTF(sub_rel, "%s/%s", relative_path, name);
        } else {
            SNPRINTF(sub_rel, "%s", name);
        }

        SNPRINTF(full_path, "%s/%s", base_path, sub_rel);
        if (lstat(full_path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                count += count_files_recursive(base_path, sub_rel);
            } else {
                count += 1;
            }
        } else {
            dispatch_log_error("Error lstat %s: %s.\n", full_path,
                               strerror(errno));
        }
        errno = 0;
    }

    if (errno) {
        dispatch_log_error("Error reading directory entry in %s: %s.\n",
                           full_path, strerror(errno));
    }

    if (closedir(dir) < 0) {
        dispatch_log_error("Error closing directory %s: %s.\n", full_path,
                           strerror(errno));
    }
    return count;
}

static void
fix_fs_recursive(char *base_path, char *relative_path) {
    DIR *dir;
    struct dirent *entry;
    char full_path[MAX_PATH_LENGTH];
    char **name_list;
    int32 count = 0;
    int32 capacity = 1024;

    char *replacements[][2] = {
        {"=>", "_equal_arrow_in_filename_"},
        {"->", "_dash_arrow_in_filename_"},
        {"\\", "_backslash_in_filename_"},
        {"\n", "_newline_in_filename_"},
        {"\"", "_double_quote_in_filename_"},
        {"\'", "_single_quote_in_filename_"},
        {"<", "_less_than_in_filename_"},
        {">", "_greater_than_in_filename_"},
        {":", "_colon_in_filename_"},
        {"|", "_pipe_in_filename_"},
        {"?", "_question_mark_in_filename_"},
        {"*", "_asterisk_in_filename_"},
    };

    if (cecup.cancel_sync) {
        return;
    }

    if (relative_path[0] == '\0') {
        SNPRINTF(full_path, "%s", base_path);
    } else {
        SNPRINTF(full_path, "%s/%s", base_path, relative_path);
    }

    if ((dir = opendir(full_path)) == NULL) {
        error("Error opening directory %s: %s.\n", full_path, strerror(errno));
        fatal(EXIT_FAILURE);
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
        int64 j = 0;
        int64 k = 0;
        int64 name_len = strlen64(d_name);

        if (relative_path[0] != '\0') {
            SNPRINTF(sub_rel, "%s/%s", relative_path, d_name);
        } else {
            SNPRINTF(sub_rel, "%s", d_name);
        }

        SNPRINTF(old_full, "%s/%s", base_path, sub_rel);
        if (lstat(old_full, &st) != 0) {
            free(d_name);
            continue;
        }

        while (k < name_len) {
            char *earliest_match = NULL;
            int32 replacement_index = -1;

            for (int32 r = 0; r < LENGTH(replacements); r += 1) {
                char *search = replacements[r][0];
                int64 search_len = strlen64(search);
                char *match;

                if ((match = memmem(&d_name[k], (size_t)(name_len - k), search,
                                    (size_t)search_len))) {
                    if (earliest_match == NULL || match < earliest_match) {
                        earliest_match = match;
                        replacement_index = r;
                    }
                }
            }

            if (earliest_match) {
                int64 prefix_len = (int64)(earliest_match - &d_name[k]);
                char *replace_str = replacements[replacement_index][1];
                int64 replace_len = strlen64(replace_str);

                if (prefix_len > 0) {
                    memcpy64(&new_name[j], &d_name[k], prefix_len);
                    j += prefix_len;
                    k += prefix_len;
                }

                memcpy64(&new_name[j], replace_str, replace_len);

                j += replace_len;
                k += strlen64(replacements[replacement_index][0]);
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
            if (relative_path[0] != '\0') {
                SNPRINTF(new_full, "%s/%s/%s", base_path, relative_path,
                         new_name);
            } else {
                SNPRINTF(new_full, "%s/%s", base_path, new_name);
            }

            if (access(new_full, F_OK) == 0) {
                dispatch_log_error("Skip rename: %s already exists.\n",
                                   new_name);
            } else if (rename(old_full, new_full) == 0) {
                dispatch_log("Fixed: %s -> %s\n", d_name, new_name);
                if (S_ISDIR(st.st_mode)) {
                    if (relative_path[0] != '\0') {
                        SNPRINTF(sub_rel, "%s/%s", relative_path, new_name);
                    } else {
                        SNPRINTF(sub_rel, "%s", new_name);
                    }
                }
            } else {
                dispatch_log_error("Failed to rename %s: %s\n", d_name,
                                   strerror(errno));
            }
        }

        if (S_ISDIR(st.st_mode)) {
            fix_fs_recursive(base_path, sub_rel);
        }
        free(d_name);
    }

    free(name_list);
    return;
}

static void *
fix_fs_worker(void *user_data) {
    ThreadData *thread_data = user_data;
    UIUpdateData *ready;

    dispatch_log("Checking for problematic names in the original folder...\n");
    fix_fs_recursive(thread_data->src_path, "");
    dispatch_log("Checking for problematic names in the backup folder...\n");
    fix_fs_recursive(thread_data->dst_path, "");
    dispatch_log("Name correction finished.\n");

    g_mutex_lock(&cecup.ui_arena_mutex);
    ready = xarena_push(cecup.ui_arena, ALIGN16(SIZEOF(UIUpdateData)));
    memset64(ready, 0, SIZEOF(UIUpdateData));
    g_mutex_unlock(&cecup.ui_arena_mutex);

    ready->type = DATA_TYPE_ENABLE_BUTTONS;
    g_idle_add(update_ui_handler, ready);

    g_mutex_lock(&cecup.ui_arena_mutex);
    arena_pop(cecup.ui_arena, thread_data);
    g_mutex_unlock(&cecup.ui_arena_mutex);
    return NULL;
}

static void *
bulk_sync_worker(void *user_data) {
    GPtrArray *tasks = user_data;
    UIUpdateData *ready;

    for (int32 i = 0; i < (int32)tasks->len; i += 1) {
        UIUpdateData *ui_update_data;
        int32 pipe_output[2];
        int32 pipe_error[2];
        pid_t child_pid;
        struct pollfd pipes[2];

        char buffer_output[8192];
        int32 buffer_output_pos = 0;
        char buffer_error[8192];
        int32 buffer_error_pos = 0;

        if ((ui_update_data = (UIUpdateData *)g_ptr_array_index(tasks, i))) {
            if (cecup.cancel_sync) {
                g_mutex_lock(&cecup.ui_arena_mutex);
                if (ui_update_data->filepath) {
                    arena_pop(cecup.ui_arena, ui_update_data->filepath);
                }
                if (ui_update_data->src_base) {
                    arena_pop(cecup.ui_arena, ui_update_data->src_base);
                }
                if (ui_update_data->dst_base) {
                    arena_pop(cecup.ui_arena, ui_update_data->dst_base);
                }
                if (ui_update_data->term_cmd) {
                    arena_pop(cecup.ui_arena, ui_update_data->term_cmd);
                }
                if (ui_update_data->diff_tool) {
                    arena_pop(cecup.ui_arena, ui_update_data->diff_tool);
                }
                if (ui_update_data->link_target) {
                    arena_pop(cecup.ui_arena, ui_update_data->link_target);
                }
                arena_pop(cecup.ui_arena, ui_update_data);
                g_mutex_unlock(&cecup.ui_arena_mutex);
                continue;
            }

            if (pipe(pipe_output) < 0) {
                error("Error creating pipe for stdout: %s.\n", strerror(errno));
                fatal(EXIT_FAILURE);
            }

            if (pipe(pipe_error) < 0) {
                error("Error creating pipe for stderr: %s.\n", strerror(errno));
                fatal(EXIT_FAILURE);
            }

            switch (child_pid = fork()) {
            case -1:
                error("Error forking: %s.\n", strerror(errno));
                fatal(EXIT_FAILURE);
            case 0: {
                char full_dst[MAX_PATH_LENGTH];
                char relative_source[MAX_PATH_LENGTH];
                char dst_dir[MAX_PATH_LENGTH];
                char *args[32];
                int32 a = 0;

                if (setpgid(0, 0) < 0) {
                    fprintf(stderr, "Error setpgid: %s.\n", strerror(errno));
                    exit(EXIT_FAILURE);
                }
                putenv("LC_ALL=C");
                XCLOSE(&pipe_output[0]);
                XCLOSE(&pipe_error[0]);
                if (dup2(pipe_output[1], STDOUT_FILENO) < 0) {
                    fprintf(stderr, "Error dup2 stdout: %s.\n",
                            strerror(errno));
                    exit(EXIT_FAILURE);
                }
                if (dup2(pipe_error[1], STDERR_FILENO) < 0) {
                    fprintf(stderr, "Error dup2 stderr: %s.\n",
                            strerror(errno));
                    exit(EXIT_FAILURE);
                }
                XCLOSE(&pipe_output[1]);
                XCLOSE(&pipe_error[1]);

                if (ui_update_data->action == UI_ACTION_DELETE) {
                    SNPRINTF(full_dst, "%s/%s", ui_update_data->dst_base,
                             ui_update_data->filepath);
                    args[a++] = "rm";
                    args[a++] = "-rfv";
                    args[a++] = full_dst;
                    args[a++] = NULL;
                    execvp(args[0], args);
                } else {
                    char cmd[MAX_PATH_LENGTH*2];
                    SNPRINTF(relative_source, "%s/./%s",
                             ui_update_data->src_base,
                             ui_update_data->filepath);
                    SNPRINTF(dst_dir, "%s/", ui_update_data->dst_base);

                    args[a++] = "rsync";
                    args[a++] = "--verbose";
                    args[a++] = "--update";
                    args[a++] = "--recursive";
                    args[a++] = "--partial";
                    args[a++] = "--progress";
                    args[a++] = "--info=progress2";
                    args[a++] = "--links";
                    args[a++] = "--hard-links";
                    args[a++] = "--itemize-changes";
                    args[a++] = "--perms";
                    args[a++] = "--times";
                    args[a++] = "--owner";
                    args[a++] = "--group";
                    args[a++] = "--relative";
                    args[a++] = relative_source;
                    args[a++] = dst_dir;
                    args[a++] = NULL;
                    STRING_FROM_ARRAY(cmd, " ", args, a);
                    dispatch_log("+ %s\n", cmd);
                    execvp(args[0], args);
                }
                fprintf(stderr, "Error: execvp failed: %s.\n", strerror(errno));
                exit(EXIT_FAILURE);
            }
            default:
                break;
            }

            XCLOSE(&pipe_output[1]);
            XCLOSE(&pipe_error[1]);

            pipes[0].fd = pipe_output[0];
            pipes[0].events = POLLIN;
            pipes[1].fd = pipe_error[0];
            pipes[1].events = POLLIN;

            while (true) {
                int64 r;
                char *eol;

                if (cecup.cancel_sync) {
                    if (kill(-child_pid, SIGTERM) < 0) {
                        dispatch_log_error("Error kill process group: %s.\n",
                                           strerror(errno));
                    }
                    dispatch_log_error("Process cancelled: %s\n",
                                       ui_update_data->filepath);
                    break;
                }

                switch (poll(pipes, 2, 100)) {
                case -1:
                    if (errno != EINTR) {
                        dispatch_log_error("Error in poll: %s.\n",
                                           strerror(errno));
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

                r = read64(pipe_output[0], buffer_output + buffer_output_pos,
                           SIZEOF(buffer_output) - buffer_output_pos - 1);
                if (r <= 0) {
                    if (r < 0) {
                        dispatch_log_error("Error reading stdout pipe: %s.\n",
                                           strerror(errno));
                    }
                    pipes[0].fd = -1;
                    goto read_error_pipe;
                }
                buffer_output_pos += (int32)r;

                while (
                    buffer_output_pos > 0
                    && ((eol = memchr64(buffer_output, '\n', buffer_output_pos))
                        || (eol = memchr64(buffer_output, '\r',
                                           buffer_output_pos)))) {
                    int32 line_len = (int32)(eol - buffer_output);
                    int32 remaining;
                    *eol = '\0';

                    if (buffer_output[0] != '\0') {
                        dispatch_log("%s.\n", buffer_output);
                    }

                    remaining = buffer_output_pos - (line_len + 1);
                    if (remaining > 0) {
                        memmove64(buffer_output, eol + 1, remaining);
                    }
                    buffer_output_pos = remaining;
                }

                if (buffer_output_pos >= (int32)SIZEOF(buffer_output) - 1) {
                    buffer_output[buffer_output_pos] = '\0';
                    dispatch_log("%s.\n", buffer_output);
                    buffer_output_pos = 0;
                }

            read_error_pipe:
                if (pipes[1].revents & (POLLHUP | POLLERR)) {
                    pipes[1].fd = -1;
                    goto check_pipes_or_break;
                }
                if (!(pipes[1].revents & POLLIN)) {
                    goto check_pipes_or_break;
                }

                r = read64(pipe_error[0], buffer_error + buffer_error_pos,
                           SIZEOF(buffer_error) - buffer_error_pos - 1);
                if (r <= 0) {
                    if (r < 0) {
                        dispatch_log_error("Error reading stderr pipe: %s.\n",
                                           strerror(errno));
                    }
                    pipes[1].fd = -1;
                    goto check_pipes_or_break;
                }
                buffer_error_pos += (int32)r;

                while (
                    buffer_error_pos > 0
                    && ((eol = memchr64(buffer_error, '\n', buffer_error_pos))
                        || (eol = memchr64(buffer_error, '\r',
                                           buffer_error_pos)))) {
                    int32 line_len = (int32)(eol - buffer_error);
                    int32 remaining;
                    *eol = '\0';

                    if (buffer_error[0] != '\0') {
                        dispatch_log_error(buffer_error);
                    }

                    remaining = buffer_error_pos - (line_len + 1);
                    if (remaining > 0) {
                        memmove64(buffer_error, eol + 1, remaining);
                    }
                    buffer_error_pos = remaining;
                }

                if (buffer_error_pos >= (int32)SIZEOF(buffer_error) - 1) {
                    buffer_error[buffer_error_pos] = '\0';
                    dispatch_log_error(buffer_error);
                    buffer_error_pos = 0;
                }

            check_pipes_or_break:
                if ((pipes[0].fd < 0) && (pipes[1].fd < 0)) {
                    break;
                }
            }

            XCLOSE(&pipe_output[0]);
            XCLOSE(&pipe_error[0]);
            if (waitpid(child_pid, NULL, 0) < 0) {
                dispatch_log_error("Error waiting for child: %s.\n",
                                   strerror(errno));
            }

            if (!cecup.cancel_sync) {
                int64 path_len;
                UIUpdateData *remove_data;

                g_mutex_lock(&cecup.ui_arena_mutex);
                remove_data = xarena_push(cecup.ui_arena,
                                          ALIGN16(SIZEOF(UIUpdateData)));
                memset64(remove_data, 0, SIZEOF(UIUpdateData));

                path_len = ui_update_data->filepath_length;
                remove_data->filepath_length = path_len;
                remove_data->filepath
                    = xarena_push(cecup.ui_arena, ALIGN16(path_len + 1));
                memcpy64(remove_data->filepath, ui_update_data->filepath,
                         path_len + 1);
                g_mutex_unlock(&cecup.ui_arena_mutex);

                remove_data->type = DATA_TYPE_REMOVE_TREE_ROW;
                g_idle_add(update_ui_handler, remove_data);
            }

            g_mutex_lock(&cecup.ui_arena_mutex);
            if (ui_update_data->filepath) {
                arena_pop(cecup.ui_arena, ui_update_data->filepath);
            }
            if (ui_update_data->src_base) {
                arena_pop(cecup.ui_arena, ui_update_data->src_base);
            }
            if (ui_update_data->dst_base) {
                arena_pop(cecup.ui_arena, ui_update_data->dst_base);
            }
            if (ui_update_data->term_cmd) {
                arena_pop(cecup.ui_arena, ui_update_data->term_cmd);
            }
            if (ui_update_data->diff_tool) {
                arena_pop(cecup.ui_arena, ui_update_data->diff_tool);
            }
            if (ui_update_data->link_target) {
                arena_pop(cecup.ui_arena, ui_update_data->link_target);
            }
            arena_pop(cecup.ui_arena, ui_update_data);
            g_mutex_unlock(&cecup.ui_arena_mutex);
        }
    }

    g_mutex_lock(&cecup.ui_arena_mutex);
    ready = xarena_push(cecup.ui_arena, ALIGN16(SIZEOF(UIUpdateData)));
    memset64(ready, 0, SIZEOF(UIUpdateData));
    g_mutex_unlock(&cecup.ui_arena_mutex);

    ready->type = DATA_TYPE_ENABLE_BUTTONS;
    g_idle_add(update_ui_handler, ready);

    g_ptr_array_unref(tasks);
    return NULL;
}

static void *
sync_worker(void *user_data) {
    ThreadData *thread_data = user_data;
    int64 total_files_preview = 0;
    int64 processed_files_preview = 0;

    int32 pipe_output[2] = {-1, -1};
    int32 pipe_error[2] = {-1, -1};
    pid_t child_pid = -1;

    char buffer_output[8192];
    int32 buffer_output_pos = 0;
    char buffer_error[8192];
    int32 buffer_error_pos = 0;

    if (thread_data->check_different_fs) {
        struct stat stat_src;
        struct stat stat_dst;

        if (stat(thread_data->src_path, &stat_src) == 0
            && stat(thread_data->dst_path, &stat_dst) == 0) {
            if (stat_src.st_dev == stat_dst.st_dev) {
                UIUpdateData *ui_update_data;
                dispatch_log_error(
                    "Safety stop: Original and Backup are on the same disk "
                    "partition. Change settings to ignore this.\n");

                g_mutex_lock(&cecup.ui_arena_mutex);
                ui_update_data = xarena_push(cecup.ui_arena,
                                             ALIGN16(SIZEOF(UIUpdateData)));
                memset64(ui_update_data, 0, SIZEOF(UIUpdateData));
                g_mutex_unlock(&cecup.ui_arena_mutex);

                ui_update_data->type = DATA_TYPE_CLEAR_TREES;
                g_idle_add(update_ui_handler, ui_update_data);

                goto finalize;
            }
        } else {
            dispatch_log_error("Error checking filesystems: %s.\n",
                               strerror(errno));
            goto finalize;
        }
    }

    if (thread_data->is_preview) {
        UIUpdateData *clear;

        g_mutex_lock(&cecup.ui_arena_mutex);
        clear = xarena_push(cecup.ui_arena, ALIGN16(SIZEOF(UIUpdateData)));
        memset64(clear, 0, SIZEOF(UIUpdateData));
        g_mutex_unlock(&cecup.ui_arena_mutex);

        clear->type = DATA_TYPE_CLEAR_TREES;
        g_idle_add(update_ui_handler, clear);

        dispatch_log("Counting files to prepare analysis...\n");
        total_files_preview = count_files_recursive(thread_data->src_path, "");
        dispatch_log("Found %lld files to analyse...\n",
                     (llong)total_files_preview);
    }

    if (cecup.cancel_sync) {
        goto finalize;
    }

    if (pipe(pipe_output) < 0) {
        error("Error creating pipe for stdout: %s.\n", strerror(errno));
        fatal(EXIT_FAILURE);
    }

    if (pipe(pipe_error) < 0) {
        error("Error creating pipe for stderr: %s.\n", strerror(errno));
        fatal(EXIT_FAILURE);
    }

    switch (child_pid = fork()) {
    case -1:
        error("Error forking: %s.\n", strerror(errno));
        exit(EXIT_FAILURE);
    case 0: {
        char src_dir[MAX_PATH_LENGTH];
        char dst_dir[MAX_PATH_LENGTH];
        char *args[32];
        int32 a = 0;
        char cmd[MAX_PATH_LENGTH*2];

        if (setpgid(0, 0) < 0) {
            fprintf(stderr, "Error setpgid: %s.\n", strerror(errno));
            fatal(EXIT_FAILURE);
        }
        putenv("LC_ALL=C");
        XCLOSE(&pipe_output[0]);
        XCLOSE(&pipe_error[0]);
        if (dup2(pipe_output[1], STDOUT_FILENO) < 0) {
            fprintf(stderr, "Error dup2 stdout: %s.\n", strerror(errno));
            fatal(EXIT_FAILURE);
        }
        if (dup2(pipe_error[1], STDERR_FILENO) < 0) {
            fprintf(stderr, "Error dup2 stderr: %s.\n", strerror(errno));
            fatal(EXIT_FAILURE);
        }
        XCLOSE(&pipe_output[1]);
        XCLOSE(&pipe_error[1]);

        args[a++] = "rsync";
        args[a++] = "--verbose";
        args[a++] = "--update";
        args[a++] = "--recursive";
        args[a++] = "--partial";
        args[a++] = "--progress";
        args[a++] = "--info=progress2";
        args[a++] = "--links";
        args[a++] = "--hard-links";
        args[a++] = "--itemize-changes";
        args[a++] = "--perms";
        args[a++] = "--times";
        args[a++] = "--owner";
        args[a++] = "--group";

        if (thread_data->delete_excluded) {
            args[a++] = "--delete-excluded";
        }
        if (thread_data->delete_after) {
            args[a++] = "--delete-after";
        }
        if (thread_data->is_preview) {
            args[a++] = "--dry-run";
        }
        if (access(cecup.ignore_path, F_OK) != -1) {
            args[a++] = "--exclude-from";
            args[a++] = cecup.ignore_path;
        }

        SNPRINTF(src_dir, "%s/", thread_data->src_path);
        SNPRINTF(dst_dir, "%s/", thread_data->dst_path);
        args[a++] = src_dir;
        args[a++] = dst_dir;
        args[a++] = NULL;

        STRING_FROM_ARRAY(cmd, " ", args, a);
        dispatch_log("+ %s\n", cmd);

        execvp("rsync", args);
        fprintf(stderr, "Error: execvp failed: %s.\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    default:
        break;
    }

    XCLOSE(&pipe_output[1]);
    XCLOSE(&pipe_error[1]);

    while (1) {
        struct pollfd pipes[2];
        int64 r;
        char *eol;

        pipes[0].fd = pipe_output[0];
        pipes[0].events = POLLIN;
        pipes[1].fd = pipe_error[0];
        pipes[1].events = POLLIN;

        if (cecup.cancel_sync) {
            if (kill(-child_pid, SIGTERM) < 0) {
                dispatch_log_error("Error killing process group: %s.\n",
                                   strerror(errno));
            }
            dispatch_log_error("Operation stopped by user.\n");
            break;
        }

        switch (poll(pipes, 2, 100)) {
        case -1:
            if (errno != EINTR) {
                dispatch_log_error("Error in poll: %s.\n", strerror(errno));
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

        r = read64(pipe_output[0], buffer_output + buffer_output_pos,
                   SIZEOF(buffer_output) - buffer_output_pos - 1);
        if (r <= 0) {
            if (r < 0) {
                dispatch_log_error("Error reading stdout pipe: %s.\n",
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
            int64 sz_path_val = 0;
            int64 mt_path_val = 0;
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
                    dispatch_progress(DATA_TYPE_PROGRESS_RSYNC,
                                      atof(start_digit) / 100.0);
                }
            }

            if (thread_data->is_preview == 0) {
                dispatch_log("%s.\n", buffer_output);
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

                SNPRINTF(full_src, "%s/%s", thread_data->src_path,
                         relative_path);
                SNPRINTF(full_dst, "%s/%s", thread_data->dst_path,
                         relative_path);

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

                // Note: NEVER delete lines with // clang-format
                // clang-format off
                dispatch_tree(SIDE_RIGHT,
                              UI_ACTION_DELETE, deletion_reason,
                              relative_path, NULL,
                              size_val, time_val);
                // clang-format on
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
                                = sep + strlen64(RSYNC_HARDLINK_NOTATION);
                        }
                    } else if (type_char == RSYNC_CHAR_SYMLINK
                               || buffer_output[1] == RSYNC_CHAR_SYMLINK) {
                        char *sep;
                        cecup_action = UI_ACTION_SYMLINK;

                        if ((sep = strstr(relative_path_entry,
                                          RSYNC_SYMLINK_NOTATION))) {
                            *sep = '\0';
                            link_target
                                = sep + strlen64(RSYNC_SYMLINK_NOTATION);
                        }
                    } else if (buffer_output[2] == '+') {
                        cecup_action = UI_ACTION_NEW;
                    }

                    SNPRINTF(full_src_path_val, "%s/%s", thread_data->src_path,
                             relative_path_entry);

                    if (lstat(full_src_path_val, &st_path_val) < 0) {
                        dispatch_log_error("Error lstat %s: %s.\n",
                                           full_src_path_val, strerror(errno));
                    } else {
                        sz_path_val = st_path_val.st_size;
                        mt_path_val = (int64)st_path_val.st_mtime;
                    }

                    // Note: NEVER delete lines with // clang-format
                    // clang-format off
                    dispatch_tree(SIDE_LEFT,
                                  cecup_action, (enum CecupReason)cecup_action,
                                  relative_path_entry, link_target,
                                  sz_path_val, mt_path_val);
                    // clang-format on

                    processed_files_preview += 1;
                    if (total_files_preview > 0) {
                        dispatch_progress(DATA_TYPE_PROGRESS_PREVIEW,
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
            dispatch_log("%s.\n", buffer_output);
            buffer_output_pos = 0;
        }

    read_error_pipe:
        if (pipes[1].revents & (POLLHUP | POLLERR)) {
            pipes[1].fd = -1;
            goto check_pipes_or_break;
        }

        if (!(pipes[1].revents & POLLIN)) {
            goto check_pipes_or_break;
        }

        r = read64(pipe_error[0], buffer_error + buffer_error_pos,
                   SIZEOF(buffer_error) - buffer_error_pos - 1);
        if (r <= 0) {
            if (r < 0) {
                dispatch_log_error("Error reading stderr pipe: %s.\n",
                                   strerror(errno));
                pipes[1].fd = -1;
            }
            goto check_pipes_or_break;
        }
        buffer_error_pos += (int32)r;

        while (buffer_error_pos > 0
               && ((eol = memchr64(buffer_error, '\n', buffer_error_pos))
                   || (eol = memchr64(buffer_error, '\r', buffer_error_pos)))) {
            int32 line_len = (int32)(eol - buffer_error);
            int32 remaining;
            *eol = '\0';

            if (buffer_error[0] != '\0') {
                dispatch_log_error("%s\n", buffer_error);
            }

            remaining = buffer_error_pos - (line_len + 1);
            if (remaining > 0) {
                memmove64(buffer_error, eol + 1, remaining);
            }
            buffer_error_pos = remaining;
        }

        if (buffer_error_pos >= (int32)SIZEOF(buffer_error) - 1) {
            buffer_error[buffer_error_pos] = '\0';
            dispatch_log_error("%s\n", buffer_error);
            buffer_error_pos = 0;
        }

    check_pipes_or_break:
        if ((pipes[0].fd < 0) && (pipes[1].fd < 0)) {
            break;
        }
    }

    if (child_pid != -1) {
        if (waitpid(child_pid, NULL, 0) < 0) {
            dispatch_log_error("Error waiting for child: %s.\n",
                               strerror(errno));
        }

        if (!cecup.cancel_sync) {
            dispatch_log(
                "Analysis complete. Review the list and click Apply.\n");
        }
    }

    XCLOSE(&pipe_output[0]);
    XCLOSE(&pipe_error[0]);

finalize:
    dispatch_progress(DATA_TYPE_PROGRESS_RSYNC, 1.0);
    dispatch_progress(DATA_TYPE_PROGRESS_PREVIEW, 1.0);

    {
        UIUpdateData *ready_signal;

        g_mutex_lock(&cecup.ui_arena_mutex);
        ready_signal
            = xarena_push(cecup.ui_arena, ALIGN16(SIZEOF(UIUpdateData)));
        memset64(ready_signal, 0, SIZEOF(UIUpdateData));
        g_mutex_unlock(&cecup.ui_arena_mutex);

        ready_signal->type = DATA_TYPE_ENABLE_BUTTONS;
        g_idle_add(update_ui_handler, ready_signal);
    }

    g_mutex_lock(&cecup.ui_arena_mutex);
    arena_pop(cecup.ui_arena, thread_data);
    g_mutex_unlock(&cecup.ui_arena_mutex);
    return NULL;
}

static void *
diff_worker(void *user_data) {
    UIUpdateData *ui_update_data;
    pid_t child;
    char *path_src;
    char *path_dst;
    int64 size_dst;
    int64 size_src;

    ui_update_data = user_data;
    size_src = strlen64(ui_update_data->src_base)
               + strlen64(ui_update_data->filepath) + 2;
    size_dst = strlen64(ui_update_data->dst_base)
               + strlen64(ui_update_data->filepath) + 2;

    path_src = xmalloc(size_src);
    path_dst = xmalloc(size_dst);

    // Note: NEVER delete lines with // clang-format
    // clang-format off
    snprintf2(path_src, size_src,
              "%s/%s", ui_update_data->src_base, ui_update_data->filepath);
    snprintf2(path_dst, size_dst,
              "%s/%s", ui_update_data->dst_base, ui_update_data->filepath);

    switch (child = fork()) {
    case -1:
        error("Error forking: %s.\n", strerror(errno));
        fatal(EXIT_FAILURE);
    case 0:
        execlp(ui_update_data->term_cmd,
               ui_update_data->term_cmd,
               "-e", ui_update_data->diff_tool, path_src, path_dst,
               (char *)NULL);
        _exit(1);
    default:
        break;
    }
    // clang-format on

    free(path_src);
    free(path_dst);

    g_mutex_lock(&cecup.ui_arena_mutex);
    if (ui_update_data->filepath) {
        arena_pop(cecup.ui_arena, ui_update_data->filepath);
    }
    if (ui_update_data->src_base) {
        arena_pop(cecup.ui_arena, ui_update_data->src_base);
    }
    if (ui_update_data->dst_base) {
        arena_pop(cecup.ui_arena, ui_update_data->dst_base);
    }
    if (ui_update_data->term_cmd) {
        arena_pop(cecup.ui_arena, ui_update_data->term_cmd);
    }
    if (ui_update_data->diff_tool) {
        arena_pop(cecup.ui_arena, ui_update_data->diff_tool);
    }
    if (ui_update_data->link_target) {
        arena_pop(cecup.ui_arena, ui_update_data->link_target);
    }
    arena_pop(cecup.ui_arena, ui_update_data);
    g_mutex_unlock(&cecup.ui_arena_mutex);
    return NULL;
}

#if TESTING_rsync
#include <assert.h>
#include <string.h>
#include <stdio.h>

int
main(void) {
    ASSERT(true);
}

#endif

#endif /* RSYNC_C */
