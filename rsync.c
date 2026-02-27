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

#define RSYNC_HARDLINK_NOTATION " => "
#define RSYNC_SYMLINK_NOTATION " -> "
#define RSYNC_UNIVERSAL_ARGS "--verbose --update --recursive" \
                             " --partial --progress --info=progress2" \
                             " --links --hard-links --itemize-changes" \
                             " --perms --times --owner --group"
#define MAX_COMMAND_LENGTH (MAX_PATH_LENGTH*2 + strlen(RSYNC_UNIVERSAL_ARGS)*2)

typedef struct EqualScannerData {
    char src_path[MAX_PATH_LENGTH];
    char dst_path[MAX_PATH_LENGTH];
    int32 total_files;
    int32 processed_files;
} EqualScannerData;

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
    data = xarena_push(cecup.ui_arena, SIZEOF(UIUpdateData));
    memset64(data, 0, SIZEOF(UIUpdateData));

    data->message = xarena_push(cecup.ui_arena, n + 1);
    memcpy64(data->message, buffer, n + 1);
    g_mutex_unlock(&cecup.ui_arena_mutex);

    data->type = DATA_TYPE_LOG;
    g_idle_add(update_ui_handler, data);
    return;
}

static gboolean
log_error_handler(gpointer user_data) {
    UIUpdateData *data;
    GtkTextIter end;
    GtkTextTagTable *table;
    GtkTextTag *tag;

    data = (UIUpdateData *)user_data;
    table = gtk_text_buffer_get_tag_table(cecup.log_buffer);
    tag = gtk_text_tag_table_lookup(table, "err_red");
    if (tag == NULL) {
        gtk_text_buffer_create_tag(cecup.log_buffer, "err_red", "foreground",
                                   "red", NULL);
    }

    gtk_text_buffer_get_end_iter(cecup.log_buffer, &end);
    gtk_text_buffer_insert_with_tags_by_name(
        cecup.log_buffer, &end, data->message, -1, "err_red", NULL);

    g_mutex_lock(&cecup.ui_arena_mutex);
    arena_pop(cecup.ui_arena, data->message);
    arena_pop(cecup.ui_arena, data);
    g_mutex_unlock(&cecup.ui_arena_mutex);
    return G_SOURCE_REMOVE;
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
    data = xarena_push(cecup.ui_arena, SIZEOF(UIUpdateData));
    memset64(data, 0, SIZEOF(UIUpdateData));

    data->message = xarena_push(cecup.ui_arena, n + 1);
    memcpy64(data->message, buffer, n + 1);
    g_mutex_unlock(&cecup.ui_arena_mutex);

    g_idle_add(log_error_handler, data);
    return;
}

static void
dispatch_progress(enum DataType type, double fraction) {
    UIUpdateData *data;
    static double last_fractions[4] = {0, 0, 0, 0};
    int32 index = 0;

    if (type == DATA_TYPE_PROGRESS_RSYNC) {
        index = 1;
    } else if (type == DATA_TYPE_PROGRESS_EQUAL) {
        index = 2;
    } else if (type == DATA_TYPE_PROGRESS_PREVIEW) {
        index = 3;
    }

    if (fraction < 1.0 && (fraction - last_fractions[index] < 0.001)
        && (fraction - last_fractions[index] > -0.001)) {
        return;
    }
    last_fractions[index] = fraction;

    g_mutex_lock(&cecup.ui_arena_mutex);
    data = xarena_push(cecup.ui_arena, SIZEOF(UIUpdateData));
    memset64(data, 0, SIZEOF(UIUpdateData));
    g_mutex_unlock(&cecup.ui_arena_mutex);

    data->type = type;
    data->fraction = fraction;
    g_idle_add(update_ui_handler, data);
    return;
}

static void
dispatch_tree(int32 side, enum CecupAction action, char *path,
              char *link_target, int64 size, int64 mtime,
              enum CecupReason reason) {
    UIUpdateData *data;
    int64 path_len;
    int64 target_len;

    g_mutex_lock(&cecup.ui_arena_mutex);
    data = xarena_push(cecup.ui_arena, SIZEOF(UIUpdateData));
    memset64(data, 0, SIZEOF(UIUpdateData));

    path_len = strlen64(path);
    data->filepath = xarena_push(cecup.ui_arena, path_len + 1);
    memcpy64(data->filepath, path, path_len + 1);

    if (link_target) {
        target_len = strlen64(link_target);
        data->link_target = xarena_push(cecup.ui_arena, target_len + 1);
        memcpy64(data->link_target, link_target, target_len + 1);
    }
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

static int32
count_files_recursive(char *base_path, char *relative_path) {
    DIR *dir;
    struct dirent *entry;
    char full_path[MAX_PATH_LENGTH];
    int32 count;

    count = 0;
    if (relative_path[0] == '\0') {
        SNPRINTF(full_path, "%s", base_path);
    } else {
        SNPRINTF(full_path, "%s/%s", base_path, relative_path);
    }

    if (!(dir = opendir(full_path))) {
        dispatch_log_error("Error opendir %s: %s.\n", full_path,
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
        dispatch_log_error("Error readdir in %s: %s.\n", full_path,
                           strerror(errno));
    }

    if (closedir(dir) < 0) {
        dispatch_log_error("Error closedir %s: %s.\n", full_path,
                           strerror(errno));
    }
    return count;
}

static void
find_equal_files(EqualScannerData *equal_scanner_data, char *relative_path) {
    DIR *dir;
    struct dirent *entry;
    char src_full[MAX_PATH_LENGTH];
    char dst_full[MAX_PATH_LENGTH];

    if (cecup.cancel_sync) {
        return;
    }

    if (relative_path[0] == '\0') {
        SNPRINTF(src_full, "%s", equal_scanner_data->src_path);
    } else {
        SNPRINTF(src_full, "%s/%s", equal_scanner_data->src_path,
                 relative_path);
    }

    if (!(dir = opendir(src_full))) {
        dispatch_log_error("Error opendir %s: %s.\n", src_full,
                           strerror(errno));
        return;
    }

    errno = 0;
    while ((entry = readdir(dir))) {
        struct stat stat_srt;
        struct stat stat_dst;
        char sub_rel[MAX_PATH_LENGTH];
        char *name;

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

        SNPRINTF(src_full, "%s/%s", equal_scanner_data->src_path, sub_rel);
        SNPRINTF(dst_full, "%s/%s", equal_scanner_data->dst_path, sub_rel);

        if (lstat(src_full, &stat_srt) != 0) {
            dispatch_log_error("Error lstat %s: %s.\n", src_full,
                               strerror(errno));
            continue;
        }

        if (S_ISDIR(stat_srt.st_mode)) {
            find_equal_files(equal_scanner_data, sub_rel);
            continue;
        }

        if (S_ISREG(stat_srt.st_mode) || S_ISLNK(stat_srt.st_mode)) {
            equal_scanner_data->processed_files += 1;
            if (equal_scanner_data->total_files > 0) {
                dispatch_progress(DATA_TYPE_PROGRESS_EQUAL,
                                  (double)equal_scanner_data->processed_files
                                      / equal_scanner_data->total_files);
            }

            if (lstat(dst_full, &stat_dst) == 0) {
                if (stat_srt.st_size == stat_dst.st_size
                    && stat_srt.st_mtime == stat_dst.st_mtime) {
                    dispatch_tree(0, UI_ACTION_EQUAL, sub_rel, NULL,
                                  stat_srt.st_size, (int64)stat_srt.st_mtime,
                                  UI_REASON_EQUAL);
                }
            }
        }
        errno = 0;
    }

    if (errno != 0) {
        dispatch_log_error("Error readdir in %s: %s.\n", src_full,
                           strerror(errno));
    }

    if (closedir(dir) < 0) {
        dispatch_log_error("Error closedir %s: %s.\n", src_full,
                           strerror(errno));
    }
    return;
}

static gpointer
equal_scanner_worker(gpointer user_data) {
    EqualScannerData *data;

    data = user_data;
    data->total_files = count_files_recursive(data->src_path, "");
    find_equal_files(data, "");
    dispatch_progress(DATA_TYPE_PROGRESS_EQUAL, 1.0);

    g_mutex_lock(&cecup.ui_arena_mutex);
    arena_pop(cecup.ui_arena, data);
    g_mutex_unlock(&cecup.ui_arena_mutex);
    return NULL;
}

static void
fix_fs_recursive(char *base_path, char *relative_path) {
    DIR *dir;
    struct dirent *entry;
    char full_path[MAX_PATH_LENGTH];
    char **name_list;
    int32 count = 0;
    int32 capacity = 1024;

    if (cecup.cancel_sync) {
        return;
    }

    if (relative_path[0] == '\0') {
        SNPRINTF(full_path, "%s", base_path);
    } else {
        SNPRINTF(full_path, "%s/%s", base_path, relative_path);
    }

    if (!(dir = opendir(full_path))) {
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
        int32 changed = 0;
        int64 j = 0;

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

        if (S_ISDIR(st.st_mode)) {
            fix_fs_recursive(base_path, sub_rel);
        }

        for (int64 k = 0; k < strlen64(d_name); k += 1) {
            if (j >= 250) {
                break;
            }
            if (d_name[k] == '=' && d_name[k + 1] == '>') {
                memcpy64(new_name + j, "_equal_arrow_in_filename_", 25);
                j += 25;
                k += 1;
                changed = 1;
            } else if (d_name[k] == '-' && d_name[k + 1] == '>') {
                memcpy64(new_name + j, "_symlink_arrow_in_filename_", 27);
                j += 27;
                k += 1;
                changed = 1;
            } else if (d_name[k] == '\\') {
                memcpy64(new_name + j, "_backslash_in_filename_", 23);
                j += 23;
                changed = 1;
            } else if (d_name[k] == '\n') {
                memcpy64(new_name + j, "_newline_in_filename_", 21);
                j += 21;
                changed = 1;
            } else if (d_name[k] == '<') {
                memcpy64(new_name + j, "_less_than_in_filename_", 23);
                j += 23;
                changed = 1;
            } else if (d_name[k] == '>') {
                memcpy64(new_name + j, "_greater_than_in_filename_", 26);
                j += 26;
                changed = 1;
            } else if (d_name[k] == ':') {
                memcpy64(new_name + j, "_colon_in_filename_", 19);
                j += 19;
                changed = 1;
            } else if (d_name[k] == '\"') {
                memcpy64(new_name + j, "_double_quote_in_filename_", 26);
                j += 26;
                changed = 1;
            } else if (d_name[k] == '|') {
                memcpy64(new_name + j, "_pipe_in_filename_", 18);
                j += 18;
                changed = 1;
            } else if (d_name[k] == '?') {
                memcpy64(new_name + j, "_question_mark_in_filename_", 27);
                j += 27;
                changed = 1;
            } else if (d_name[k] == '*') {
                memcpy64(new_name + j, "_asterisk_in_filename_", 22);
                j += 22;
                changed = 1;
            } else {
                new_name[j] = d_name[k];
                j += 1;
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

            if (rename(old_full, new_full) == 0) {
                dispatch_log("Fixed: %s -> %s\n", d_name, new_name);
            } else {
                dispatch_log_error("Failed to rename %s: %s\n", d_name,
                                   strerror(errno));
            }
        }
        free(d_name);
    }
    free(name_list);
    return;
}

static gpointer
fix_fs_worker(gpointer user_data) {
    ThreadData *thread_data;
    UIUpdateData *ready;

    thread_data = (ThreadData *)user_data;
    dispatch_log("Checking for problematic names in the original folder...\n");
    fix_fs_recursive(thread_data->src_path, "");
    dispatch_log("Checking for problematic names in the backup folder...\n");
    fix_fs_recursive(thread_data->dst_path, "");
    dispatch_log("Name correction finished.\n");

    g_mutex_lock(&cecup.ui_arena_mutex);
    ready = xarena_push(cecup.ui_arena, SIZEOF(UIUpdateData));
    memset64(ready, 0, SIZEOF(UIUpdateData));
    g_mutex_unlock(&cecup.ui_arena_mutex);

    ready->type = DATA_TYPE_ENABLE_BUTTONS;
    g_idle_add(update_ui_handler, ready);

    g_mutex_lock(&cecup.ui_arena_mutex);
    arena_pop(cecup.ui_arena, thread_data);
    g_mutex_unlock(&cecup.ui_arena_mutex);
    return NULL;
}

static gpointer
bulk_sync_worker(gpointer user_data) {
    GPtrArray *tasks;
    UIUpdateData *ready;

    tasks = (GPtrArray *)user_data;

    for (int32 i = 0; i < (int32)tasks->len; i += 1) {
        UIUpdateData *ud;
        char cmd[8192];
        int32 pipe_output[2];
        int32 pipe_error[2];
        pid_t child_pid;
        struct pollfd pipes[2];

        char output_line_buffer[8192];
        int32 output_line_pos = 0;
        char error_line_buffer[8192];
        int32 error_line_pos = 0;

        UIUpdateData *remove_data;

        ud = (UIUpdateData *)g_ptr_array_index(tasks, i);

        if (cecup.cancel_sync) {
            g_mutex_lock(&cecup.ui_arena_mutex);
            if (ud->filepath) {
                arena_pop(cecup.ui_arena, ud->filepath);
            }
            if (ud->src_base) {
                arena_pop(cecup.ui_arena, ud->src_base);
            }
            if (ud->dst_base) {
                arena_pop(cecup.ui_arena, ud->dst_base);
            }
            if (ud->term_cmd) {
                arena_pop(cecup.ui_arena, ud->term_cmd);
            }
            if (ud->diff_tool) {
                arena_pop(cecup.ui_arena, ud->diff_tool);
            }
            if (ud->link_target) {
                arena_pop(cecup.ui_arena, ud->link_target);
            }
            arena_pop(cecup.ui_arena, ud);
            g_mutex_unlock(&cecup.ui_arena_mutex);
            continue;
        }

        if (ud->action == UI_ACTION_DELETE) {
            char full_dst[MAX_PATH_LENGTH];
            char *escaped_dst;

            SNPRINTF(full_dst, "%s/%s", ud->dst_base, ud->filepath);
            escaped_dst = shell_escape(full_dst);
            SNPRINTF(cmd, "rm -rfv '%s'", escaped_dst);
            free(escaped_dst);
        } else {
            char *escaped_src;
            char *escaped_dst;
            char *escaped_file;

            escaped_src = shell_escape(ud->src_base);
            escaped_dst = shell_escape(ud->dst_base);
            escaped_file = shell_escape(ud->filepath);

            SNPRINTF(cmd,
                     "rsync " RSYNC_UNIVERSAL_ARGS
                     " --relative '%s/./%s' '%s/'",
                     escaped_src, escaped_file, escaped_dst);

            free(escaped_src);
            free(escaped_dst);
            free(escaped_file);
        }

        dispatch_log("+ %s\n", cmd);

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
        case 0:
            if (setpgid(0, 0) < 0) {
                fprintf(stderr, "Error setpgid: %s.\n", strerror(errno));
                exit(EXIT_FAILURE);
            }
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
            execl("/bin/sh", "sh", "-c", cmd, NULL);
            fprintf(stderr, "Error: execl failed: %s.\n", strerror(errno));
            exit(EXIT_FAILURE);
        default:
            break;
        }

        XCLOSE(&pipe_output[1]);
        XCLOSE(&pipe_error[1]);

        pipes[0].fd = pipe_output[0];
        pipes[0].events = POLLIN;
        pipes[1].fd = pipe_error[0];
        pipes[1].events = POLLIN;

        while (1) {
            if (cecup.cancel_sync) {
                if (kill(-child_pid, SIGTERM) < 0) {
                    dispatch_log_error("Error kill process group: %s.\n",
                                       strerror(errno));
                }
                dispatch_log_error("Process cancelled: %s\n", ud->filepath);
                break;
            }

            switch (poll(pipes, 2, 100)) {
            case -1:
                dispatch_log_error("Error in poll: %s.\n", strerror(errno));
                goto out;
            case 0:
                continue;
            default:
                break;
            }

            if (pipes[0].revents & POLLIN) {
                int64 r = read64(
                    pipe_output[0], output_line_buffer + output_line_pos,
                    SIZEOF(output_line_buffer) - output_line_pos - 1);
                if (r < 0) {
                    dispatch_log_error("Error reading stdout pipe: %s.\n",
                                       strerror(errno));
                    pipes[0].fd = -1;
                } else if (r > 0) {
                    char *eol;
                    output_line_pos += (int32)r;

                    while ((eol = memchr64(output_line_buffer, '\n',
                                           output_line_pos))
                           || (eol = memchr64(output_line_buffer, '\r',
                                              output_line_pos))) {
                        int32 line_len = (int32)(eol - output_line_buffer);
                        int32 remaining;
                        *eol = '\0';

                        if (output_line_buffer[0] != '\0') {
                            dispatch_log("%s.\n", output_line_buffer);
                        }

                        remaining = output_line_pos - (line_len + 1);
                        if (remaining > 0) {
                            memmove64(output_line_buffer, eol + 1, remaining);
                        }
                        output_line_pos = remaining;
                    }

                    if (output_line_pos
                        >= (int32)SIZEOF(output_line_buffer) - 1) {
                        output_line_buffer[output_line_pos] = '\0';
                        dispatch_log("%s.\n", output_line_buffer);
                        output_line_pos = 0;
                    }
                } else {
                    pipes[0].fd = -1;
                }
            } else if (pipes[0].revents & (POLLHUP | POLLERR)) {
                pipes[0].fd = -1;
            }

            if (pipes[1].revents & POLLIN) {
                int64 r
                    = read64(pipe_error[0], error_line_buffer + error_line_pos,
                             SIZEOF(error_line_buffer) - error_line_pos - 1);
                if (r < 0) {
                    dispatch_log_error("Error reading stderr pipe: %s.\n",
                                       strerror(errno));
                    pipes[1].fd = -1;
                } else if (r > 0) {
                    char *eol;
                    error_line_pos += (int32)r;

                    while ((eol
                            = memchr64(error_line_buffer, '\n', error_line_pos))
                           || (eol = memchr64(error_line_buffer, '\r',
                                              error_line_pos))) {
                        int32 line_len = (int32)(eol - error_line_buffer);
                        int32 remaining;
                        *eol = '\0';

                        if (error_line_buffer[0] != '\0') {
                            dispatch_log_error(error_line_buffer);
                        }

                        remaining = error_line_pos - (line_len + 1);
                        if (remaining > 0) {
                            memmove64(error_line_buffer, eol + 1, remaining);
                        }
                        error_line_pos = remaining;
                    }

                    if (error_line_pos
                        >= (int32)SIZEOF(error_line_buffer) - 1) {
                        error_line_buffer[error_line_pos] = '\0';
                        dispatch_log_error(error_line_buffer);
                        error_line_pos = 0;
                    }
                } else {
                    pipes[1].fd = -1;
                }
            } else if (pipes[1].revents & (POLLHUP | POLLERR)) {
                pipes[1].fd = -1;
            }

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
            g_mutex_lock(&cecup.ui_arena_mutex);
            remove_data = xarena_push(cecup.ui_arena, SIZEOF(UIUpdateData));
            memset64(remove_data, 0, SIZEOF(UIUpdateData));

            path_len = strlen64(ud->filepath);
            remove_data->filepath = xarena_push(cecup.ui_arena, path_len + 1);
            memcpy64(remove_data->filepath, ud->filepath, path_len + 1);
            g_mutex_unlock(&cecup.ui_arena_mutex);

            remove_data->type = DATA_TYPE_REMOVE_TREE_ROW;
            g_idle_add(update_ui_handler, remove_data);
        }

        g_mutex_lock(&cecup.ui_arena_mutex);
        if (ud->filepath) {
            arena_pop(cecup.ui_arena, ud->filepath);
        }
        if (ud->src_base) {
            arena_pop(cecup.ui_arena, ud->src_base);
        }
        if (ud->dst_base) {
            arena_pop(cecup.ui_arena, ud->dst_base);
        }
        if (ud->term_cmd) {
            arena_pop(cecup.ui_arena, ud->term_cmd);
        }
        if (ud->diff_tool) {
            arena_pop(cecup.ui_arena, ud->diff_tool);
        }
        if (ud->link_target) {
            arena_pop(cecup.ui_arena, ud->link_target);
        }
        arena_pop(cecup.ui_arena, ud);
        g_mutex_unlock(&cecup.ui_arena_mutex);
    }
out:

    g_mutex_lock(&cecup.ui_arena_mutex);
    ready = xarena_push(cecup.ui_arena, SIZEOF(UIUpdateData));
    memset64(ready, 0, SIZEOF(UIUpdateData));
    g_mutex_unlock(&cecup.ui_arena_mutex);

    ready->type = DATA_TYPE_ENABLE_BUTTONS;
    g_idle_add(update_ui_handler, ready);

    g_ptr_array_unref(tasks);
    return NULL;
}

static gpointer
sync_worker(gpointer user_data) {
    ThreadData *thread_data = (ThreadData *)user_data;
    GThread *scanner_thread = NULL;
    int32 total_files_preview = 0;
    int32 processed_files_preview = 0;
    char exclude_arg[MAX_PATH_LENGTH];
    char *cmd;
    char *esc_src;
    char *esc_dst;

    int32 pipe_output[2] = {-1, -1};
    int32 pipe_error[2] = {-1, -1};
    pid_t child_pid = -1;

    char output_line_buffer[8192];
    int32 output_line_pos = 0;
    char error_line_buffer[8192];
    int32 error_line_pos = 0;

    if (thread_data->check_different_fs) {
        struct stat stat_src;
        struct stat stat_dst;

        if (stat(thread_data->src_path, &stat_src) == 0
            && stat(thread_data->dst_path, &stat_dst) == 0) {
            if (stat_src.st_dev == stat_dst.st_dev) {
                dispatch_log_error(
                    "Safety stop: Original and Backup are on the same disk "
                    "partition. Change settings to ignore this.\n");
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
        clear = xarena_push(cecup.ui_arena, SIZEOF(UIUpdateData));
        memset64(clear, 0, SIZEOF(UIUpdateData));
        g_mutex_unlock(&cecup.ui_arena_mutex);

        clear->type = DATA_TYPE_CLEAR_TREES;
        g_idle_add(update_ui_handler, clear);

        dispatch_log("Counting files to prepare analysis...\n");
        total_files_preview = count_files_recursive(thread_data->src_path, "");
    }

    if (thread_data->is_preview && thread_data->scan_equal) {
        EqualScannerData *equal_scanner_data;
        g_mutex_lock(&cecup.ui_arena_mutex);
        equal_scanner_data
            = xarena_push(cecup.ui_arena, SIZEOF(EqualScannerData));
        memset64(equal_scanner_data, 0, SIZEOF(EqualScannerData));
        g_mutex_unlock(&cecup.ui_arena_mutex);

        strncpy(equal_scanner_data->src_path, thread_data->src_path,
                MAX_PATH_LENGTH - 1);
        strncpy(equal_scanner_data->dst_path, thread_data->dst_path,
                MAX_PATH_LENGTH - 1);
        scanner_thread = g_thread_new("equal_scanner", equal_scanner_worker,
                                      equal_scanner_data);
    }

    if (access(cecup.ignore_path, F_OK) != -1) {
        SNPRINTF(exclude_arg, "--exclude-from='%s'", cecup.ignore_path);
    } else {
        if (errno != ENOENT) {
            dispatch_log_error("Error access %s: %s.\n", cecup.ignore_path,
                               strerror(errno));
        }
        strcpy(exclude_arg, "");
    }

    cmd = xmalloc(MAX_COMMAND_LENGTH);
    esc_src = shell_escape(thread_data->src_path);
    esc_dst = shell_escape(thread_data->dst_path);

    snprintf2(cmd, MAX_COMMAND_LENGTH,
              "rsync " RSYNC_UNIVERSAL_ARGS " %s %s %s %s '%s/' '%s/'",
              thread_data->delete_excluded ? "--delete-excluded" : "",
              thread_data->delete_after ? "--delete-after" : "",
              thread_data->is_preview ? "--dry-run" : "", exclude_arg, esc_src,
              esc_dst);

    free(esc_src);
    free(esc_dst);

    {
        char log_cmd[8192];
        int32 i = 0;
        int32 j = 0;
        int32 line_len = 0;
        int32 last_space_index = -1;
        while (cmd[i] != '\0') {
            log_cmd[j] = cmd[i];
            if (cmd[i] == ' ') {
                last_space_index = j;
            }
            line_len += 1;
            if (line_len > 120 && last_space_index != -1) {
                int32 word_len = j - last_space_index;

                log_cmd[last_space_index] = '\n';
                for (int32 k = 0; k < word_len; k += 1) {
                    log_cmd[j + 4 - k] = log_cmd[j - k];
                }
                log_cmd[last_space_index + 1] = ' ';
                log_cmd[last_space_index + 2] = ' ';
                log_cmd[last_space_index + 3] = ' ';
                log_cmd[last_space_index + 4] = ' ';
                j += 4;
                line_len = word_len + 4;
                last_space_index = -1;
            }
            i += 1;
            j += 1;
        }
        log_cmd[j] = '\0';
        dispatch_log("+ %s\n", log_cmd);
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
    case 0:
        if (setpgid(0, 0) < 0) {
            fprintf(stderr, "Error setpgid: %s.\n", strerror(errno));
            fatal(EXIT_FAILURE);
        }
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
        execl("/bin/sh", "sh", "-c", cmd, NULL);
        fprintf(stderr, "Error: execl failed: %s.\n", strerror(errno));
        exit(EXIT_FAILURE);
    default:
        break;
    }

    free(cmd);
    XCLOSE(&pipe_output[1]);
    XCLOSE(&pipe_error[1]);

    while (true) {
        struct pollfd pipes[2];
        int32 poll_return;
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

        if ((poll_return = poll(pipes, 2, 100)) < 0) {
            if (errno != EINTR) {
                dispatch_log_error("Error in poll: %s.\n", strerror(errno));
                fatal(EXIT_FAILURE);
            }
            continue;
        } else if (poll_return == 0) {
            continue;
        }

        if (pipes[0].revents & (POLLHUP | POLLERR)) {
            pipes[0].fd = -1;
            goto read_error_pipe;
        }
        if (!(pipes[0].revents & POLLIN)) {
            goto read_error_pipe;
        }

        r = read64(pipe_output[0], output_line_buffer + output_line_pos,
                   SIZEOF(output_line_buffer) - output_line_pos - 1);
        if (r <= 0) {
            if (r < 0) {
                dispatch_log_error("Error reading stdout pipe: %s.\n",
                                   strerror(errno));
                pipes[0].fd = -1;
            }
            goto read_error_pipe;
        }
        output_line_pos += (int32)r;

        while ((eol = memchr64(output_line_buffer, '\n', output_line_pos))
               || (eol = memchr64(output_line_buffer, '\r', output_line_pos))) {
            char *percent_pos;
            char *space_pos;
            char *relative_path_entry;
            char *link_target;
            char type_char;
            enum CecupAction cecup_action;
            char full_src_path_val[MAX_PATH_LENGTH];
            struct stat st_path_val;
            int64 sz_path_val = 0;
            int64 mt_path_val = 0;
            int32 line_len = (int32)(eol - output_line_buffer);
            int32 remaining;

            *eol = '\0';

            if ((percent_pos = strstr(output_line_buffer, "%"))) {
                char *start_digit = percent_pos;
                while (start_digit > output_line_buffer
                       && isdigit(*(start_digit - 1))) {
                    start_digit -= 1;
                }
                dispatch_progress(DATA_TYPE_PROGRESS_RSYNC,
                                  atof(start_digit) / 100.0);
            }

            if (thread_data->is_preview == 0) {
                dispatch_log("%s.\n", output_line_buffer);
            } else if (strncmp(output_line_buffer, "*deleting", 9) == 0) {
                char *relative_path = output_line_buffer + 10;
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

                dispatch_tree(1, UI_ACTION_DELETE, relative_path, NULL,
                              size_val, time_val, deletion_reason);
            } else if ((space_pos = strchr(output_line_buffer, ' '))) {
                type_char = output_line_buffer[0];
                if ((type_char == '>') || (type_char == '.')
                    || (type_char == 'h') || (type_char == 'c')
                    || (type_char == 'L')) {

                    relative_path_entry = space_pos + 1;
                    while (isspace(*relative_path_entry)) {
                        relative_path_entry += 1;
                    }

                    cecup_action = UI_ACTION_UPDATE;
                    link_target = NULL;
                    if (type_char == 'h') {
                        char *sep;
                        cecup_action = UI_ACTION_HARDLINK;

                        if ((sep = strstr(relative_path_entry,
                                          RSYNC_HARDLINK_NOTATION))) {
                            *sep = '\0';
                            link_target
                                = sep + strlen64(RSYNC_HARDLINK_NOTATION);
                        }
                    } else if (type_char == 'L'
                               || output_line_buffer[1] == 'L') {
                        char *sep;
                        cecup_action = UI_ACTION_SYMLINK;

                        if ((sep = strstr(relative_path_entry,
                                          RSYNC_SYMLINK_NOTATION))) {
                            *sep = '\0';
                            link_target
                                = sep + strlen64(RSYNC_SYMLINK_NOTATION);
                        }
                    } else if (strncmp(output_line_buffer, "cd", 2) == 0
                               || strncmp(output_line_buffer, ">f+++++", 7)
                                      == 0) {
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

                    dispatch_tree(0, cecup_action, relative_path_entry,
                                  link_target, sz_path_val, mt_path_val,
                                  (enum CecupReason)cecup_action);

                    processed_files_preview += 1;
                    if (total_files_preview > 0) {
                        dispatch_progress(DATA_TYPE_PROGRESS_PREVIEW,
                                          (double)processed_files_preview
                                              / total_files_preview);
                    }
                }
            }

            remaining = output_line_pos - (line_len + 1);
            if (remaining > 0) {
                memmove64(output_line_buffer, eol + 1, remaining);
            }
            output_line_pos = remaining;
        }

        if (output_line_pos >= (int32)SIZEOF(output_line_buffer) - 1) {
            output_line_buffer[output_line_pos] = '\0';
            dispatch_log("%s.\n", output_line_buffer);
            output_line_pos = 0;
        }

    read_error_pipe:
        if (pipes[1].revents & (POLLHUP | POLLERR)) {
            pipes[1].fd = -1;
            goto check_pipes_or_break;
        }

        if (!(pipes[1].revents & POLLIN)) {
            goto check_pipes_or_break;
        }

        r = read64(pipe_error[0], error_line_buffer + error_line_pos,
                   SIZEOF(error_line_buffer) - error_line_pos - 1);
        if (r <= 0) {
            if (r < 0) {
                dispatch_log_error("Error reading stderr pipe: %s.\n",
                                   strerror(errno));
                pipes[1].fd = -1;
            }
            goto check_pipes_or_break;
        }
        error_line_pos += (int32)r;

        while ((eol = memchr64(error_line_buffer, '\n', error_line_pos))
               || (eol = memchr64(error_line_buffer, '\r', error_line_pos))) {
            int32 line_len = (int32)(eol - error_line_buffer);
            int32 remaining;
            *eol = '\0';

            if (error_line_buffer[0] != '\0') {
                dispatch_log_error(error_line_buffer);
            }

            remaining = error_line_pos - (line_len + 1);
            if (remaining > 0) {
                memmove64(error_line_buffer, eol + 1, remaining);
            }
            error_line_pos = remaining;
        }

        if (error_line_pos >= (int32)SIZEOF(error_line_buffer) - 1) {
            error_line_buffer[error_line_pos] = '\0';
            dispatch_log_error(error_line_buffer);
            error_line_pos = 0;
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

    if (scanner_thread != NULL) {
        g_thread_join(scanner_thread);
    }

finalize:
    dispatch_progress(DATA_TYPE_PROGRESS_RSYNC, 1.0);
    dispatch_progress(DATA_TYPE_PROGRESS_PREVIEW, 1.0);

    {
        UIUpdateData *ready_signal;

        g_mutex_lock(&cecup.ui_arena_mutex);
        ready_signal = xarena_push(cecup.ui_arena, SIZEOF(UIUpdateData));
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

static gpointer
diff_worker(gpointer user_data) {
    UIUpdateData *ud;
    char *cmd = xmalloc(MAX_COMMAND_LENGTH);
    char *esc_src;
    char *esc_dst;
    char *esc_file;

    ud = (UIUpdateData *)user_data;
    esc_src = shell_escape(ud->src_base);
    esc_dst = shell_escape(ud->dst_base);
    esc_file = shell_escape(ud->filepath);

    snprintf2(
        cmd, MAX_COMMAND_LENGTH,
        "%s -e bash -c \"%s '%s/%s' '%s/%s'; read -p 'Press Enter...'\" &",
        ud->term_cmd, ud->diff_tool, esc_src, esc_file, esc_dst, esc_file);

    free(esc_src);
    free(esc_dst);
    free(esc_file);

    if (system(cmd) < 0) {
        dispatch_log_error("Error system call: %s.\n", strerror(errno));
    }

    g_mutex_lock(&cecup.ui_arena_mutex);
    if (ud->filepath) {
        arena_pop(cecup.ui_arena, ud->filepath);
    }
    if (ud->src_base) {
        arena_pop(cecup.ui_arena, ud->src_base);
    }
    if (ud->dst_base) {
        arena_pop(cecup.ui_arena, ud->dst_base);
    }
    if (ud->term_cmd) {
        arena_pop(cecup.ui_arena, ud->term_cmd);
    }
    if (ud->diff_tool) {
        arena_pop(cecup.ui_arena, ud->diff_tool);
    }
    if (ud->link_target) {
        arena_pop(cecup.ui_arena, ud->link_target);
    }
    arena_pop(cecup.ui_arena, ud);
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
