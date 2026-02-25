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

#define RSYNC_UNIVERSAL_ARGS "--verbose --update --recursive" \
                             " --partial --progress --info=progress2" \
                             " --links --hard-links --itemize-changes" \
                             " --perms --times --owner --group"

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
        error("Error in vsnprintf.\n");
        exit(EXIT_FAILURE);
    }

    g_mutex_lock(&cecup_state.ui_arena_mutex);
    data = arena_push(cecup_state.ui_arena, SIZEOF(UIUpdateData));
    memset64(data, 0, SIZEOF(UIUpdateData));

    data->message = arena_push(cecup_state.ui_arena, n + 1);
    memcpy64(data->message, buffer, n + 1);
    g_mutex_unlock(&cecup_state.ui_arena_mutex);

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
    table = gtk_text_buffer_get_tag_table(cecup_state.log_buffer);
    tag = gtk_text_tag_table_lookup(table, "err_red");
    if (tag == NULL) {
        gtk_text_buffer_create_tag(cecup_state.log_buffer, "err_red",
                                   "foreground", "red", NULL);
    }

    gtk_text_buffer_get_end_iter(cecup_state.log_buffer, &end);
    gtk_text_buffer_insert_with_tags_by_name(
        cecup_state.log_buffer, &end, data->message, -1, "err_red", NULL);

    g_mutex_lock(&cecup_state.ui_arena_mutex);
    arena_pop(cecup_state.ui_arena, data->message);
    arena_pop(cecup_state.ui_arena, data);
    g_mutex_unlock(&cecup_state.ui_arena_mutex);
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
        error("Error in vsnprintf.\n");
        exit(EXIT_FAILURE);
    }

    g_mutex_lock(&cecup_state.ui_arena_mutex);
    data = arena_push(cecup_state.ui_arena, SIZEOF(UIUpdateData));
    memset64(data, 0, SIZEOF(UIUpdateData));

    data->message = arena_push(cecup_state.ui_arena, n + 1);
    memcpy64(data->message, buffer, n + 1);
    g_mutex_unlock(&cecup_state.ui_arena_mutex);

    g_idle_add(log_error_handler, data);
    return;
}

static void
dispatch_progress(enum DataType type, double fraction) {
    UIUpdateData *data;

    g_mutex_lock(&cecup_state.ui_arena_mutex);
    data = arena_push(cecup_state.ui_arena, SIZEOF(UIUpdateData));
    memset64(data, 0, SIZEOF(UIUpdateData));
    g_mutex_unlock(&cecup_state.ui_arena_mutex);

    data->type = type;
    data->fraction = fraction;
    g_idle_add(update_ui_handler, data);
    return;
}

static void
dispatch_tree(int32 side, enum CecupAction action, char *path, int64 size,
              enum CecupReason reason) {
    UIUpdateData *data;
    int64 path_len;

    g_mutex_lock(&cecup_state.ui_arena_mutex);
    data = arena_push(cecup_state.ui_arena, SIZEOF(UIUpdateData));
    memset64(data, 0, SIZEOF(UIUpdateData));

    path_len = strlen64(path);
    data->filepath = arena_push(cecup_state.ui_arena, path_len + 1);
    memcpy64(data->filepath, path, path_len + 1);
    g_mutex_unlock(&cecup_state.ui_arena_mutex);

    data->type = DATA_TYPE_TREE_ROW;
    data->side = side;
    data->action = action;
    data->reason = reason;
    data->size = size;
    g_idle_add(update_ui_handler, data);
    return;
}

static int32
count_files_recursive(const char *base_path, const char *relative_path) {
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
find_equal_files(EqualScannerData *sd, char *relative_path) {
    DIR *dir;
    struct dirent *entry;
    char src_full[MAX_PATH_LENGTH];
    char dst_full[MAX_PATH_LENGTH];

    if (cecup_state.cancel_sync) {
        return;
    }

    if (relative_path[0] == '\0') {
        SNPRINTF(src_full, "%s", sd->src_path);
    } else {
        SNPRINTF(src_full, "%s/%s", sd->src_path, relative_path);
    }

    if (!(dir = opendir(src_full))) {
        dispatch_log_error("Error opendir %s: %s.\n", src_full,
                           strerror(errno));
        return;
    }

    errno = 0;
    while ((entry = readdir(dir))) {
        struct stat st_s;
        struct stat st_d;
        char sub_rel[MAX_PATH_LENGTH];
        char *name;

        if (cecup_state.cancel_sync) {
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

        SNPRINTF(src_full, "%s/%s", sd->src_path, sub_rel);
        SNPRINTF(dst_full, "%s/%s", sd->dst_path, sub_rel);

        if (lstat(src_full, &st_s) != 0) {
            dispatch_log_error("Error lstat %s: %s.\n", src_full,
                               strerror(errno));
            continue;
        }

        if (S_ISDIR(st_s.st_mode)) {
            find_equal_files(sd, sub_rel);
            continue;
        }

        if (S_ISREG(st_s.st_mode)) {
            sd->processed_files += 1;
            if (sd->total_files > 0) {
                dispatch_progress(DATA_TYPE_PROGRESS_EQUAL,
                                  (double)sd->processed_files
                                      / sd->total_files);
            }

            if (lstat(dst_full, &st_d) == 0) {
                if (st_s.st_size == st_d.st_size
                    && st_s.st_mtime == st_d.st_mtime) {
                    dispatch_tree(0, UI_ACTION_EQUAL, sub_rel, st_s.st_size,
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

    data = (EqualScannerData *)user_data;
    data->total_files = count_files_recursive(data->src_path, "");
    find_equal_files(data, "");
    dispatch_progress(DATA_TYPE_PROGRESS_EQUAL, 1.0);

    g_mutex_lock(&cecup_state.ui_arena_mutex);
    arena_pop(cecup_state.ui_arena, data);
    g_mutex_unlock(&cecup_state.ui_arena_mutex);
    return NULL;
}

static gpointer
bulk_sync_worker(gpointer user_data) {
    GPtrArray *tasks;
    UIUpdateData *ready;

    tasks = (GPtrArray *)user_data;

    for (uint32 i = 0; i < tasks->len; i += 1) {
        UIUpdateData *ud;
        char cmd[4096];
        int32 pipe_output[2];
        int32 pipe_error[2];
        pid_t child_pid;
        struct pollfd pipes[2];
        char output_buffer[8192];
        char error_buffer[8192];
        int32 output_position;
        int32 error_position;
        int32 poll_return;
        UIUpdateData *remove_data;

        ud = (UIUpdateData *)g_ptr_array_index(tasks, i);

        if (cecup_state.cancel_sync) {
            g_mutex_lock(&cecup_state.ui_arena_mutex);
            if (ud->filepath) {
                arena_pop(cecup_state.ui_arena, ud->filepath);
            }
            if (ud->src_base) {
                arena_pop(cecup_state.ui_arena, ud->src_base);
            }
            if (ud->dst_base) {
                arena_pop(cecup_state.ui_arena, ud->dst_base);
            }
            if (ud->term_cmd) {
                arena_pop(cecup_state.ui_arena, ud->term_cmd);
            }
            if (ud->diff_tool) {
                arena_pop(cecup_state.ui_arena, ud->diff_tool);
            }
            arena_pop(cecup_state.ui_arena, ud);
            g_mutex_unlock(&cecup_state.ui_arena_mutex);
            continue;
        }

        if (ud->action == UI_ACTION_DELETE) {
            char *full_dst;
            full_dst = g_build_filename(ud->dst_base, ud->filepath, NULL);
            SNPRINTF(cmd, "rm -rfv '%s'", full_dst);
            g_free(full_dst);
        } else {
            SNPRINTF(cmd,
                     "rsync " RSYNC_UNIVERSAL_ARGS
                     " --relative '%s/./%s' '%s/'",
                     ud->src_base, ud->filepath, ud->dst_base);
        }

        dispatch_log("+ %s\n", cmd);

        if (pipe(pipe_output) < 0) {
            dispatch_log_error("Error creating pipe for stdout: %s.\n",
                               strerror(errno));
            continue;
        }

        if (pipe(pipe_error) < 0) {
            dispatch_log_error("Error creating pipe for stderr: %s.\n",
                               strerror(errno));
            XCLOSE(&pipe_output[0]);
            XCLOSE(&pipe_output[1]);
            continue;
        }

        switch (child_pid = fork()) {
        case -1:
            dispatch_log_error("Error forking: %s.\n", strerror(errno));
            XCLOSE(&pipe_output[0]);
            XCLOSE(&pipe_output[1]);
            XCLOSE(&pipe_error[0]);
            XCLOSE(&pipe_error[1]);
            continue;
        case 0:
            if (setpgid(0, 0) < 0) {
                fprintf(stderr, "Error setpgid: %s.\n", strerror(errno));
                exit(EXIT_FAILURE);
            }
            XCLOSE(&pipe_output[0]);
            XCLOSE(&pipe_error[0]);
            if (dup2(pipe_output[1], STDOUT_FILENO) < 0) {
                fprintf(stderr, "Error dup2 stdout: %s.\n", strerror(errno));
                exit(EXIT_FAILURE);
            }
            if (dup2(pipe_error[1], STDERR_FILENO) < 0) {
                fprintf(stderr, "Error dup2 stderr: %s.\n", strerror(errno));
                exit(EXIT_FAILURE);
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

        output_position = 0;
        error_position = 0;

        while (1) {
            if (cecup_state.cancel_sync) {
                if (kill(-child_pid, SIGTERM) < 0) {
                    dispatch_log_error("Error kill process group: %s.\n",
                                       strerror(errno));
                }
                dispatch_log_error("Process cancelled: %s\n", ud->filepath);
                break;
            }

            switch ((poll_return = poll(pipes, 2, 100))) {
            case -1:
                dispatch_log_error("Error in poll: %s.\n", strerror(errno));
                goto out;
            case 0:
                continue;
            default:
                break;
            }

            if (pipes[0].revents & POLLIN) {
                char buffer[2048];
                int64 r = read64(pipe_output[0], buffer, SIZEOF(buffer));
                if (r < 0) {
                    dispatch_log_error("Error reading stdout pipe: %s.\n",
                                       strerror(errno));
                    pipes[0].fd = -1;
                } else if (r > 0) {
                    for (int64 k = 0; k < r; k += 1) {
                        if (buffer[k] == '\n'
                            || output_position == SIZEOF(output_buffer) - 1) {
                            output_buffer[output_position] = '\0';
                            dispatch_log(output_buffer);
                            output_position = 0;
                        } else {
                            output_buffer[output_position] = buffer[k];
                            output_position += 1;
                        }
                    }
                } else {
                    pipes[0].fd = -1;
                }
            } else if (pipes[0].revents & (POLLHUP | POLLERR)) {
                pipes[0].fd = -1;
            }

            if (pipes[1].revents & POLLIN) {
                char buffer[2048];
                int64 r = read64(pipe_error[0], buffer, SIZEOF(buffer));
                if (r < 0) {
                    dispatch_log_error("Error reading stderr pipe: %s.\n",
                                       strerror(errno));
                    pipes[1].fd = -1;
                } else if (r > 0) {
                    for (int64 k = 0; k < r; k += 1) {
                        if (buffer[k] == '\n'
                            || error_position == SIZEOF(error_buffer) - 1) {
                            error_buffer[error_position] = '\0';
                            dispatch_log_error(error_buffer);
                            error_position = 0;
                        } else {
                            error_buffer[error_position] = buffer[k];
                            error_position += 1;
                        }
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

        if (!cecup_state.cancel_sync) {
            int64 path_len;
            g_mutex_lock(&cecup_state.ui_arena_mutex);
            remove_data
                = arena_push(cecup_state.ui_arena, SIZEOF(UIUpdateData));
            memset64(remove_data, 0, SIZEOF(UIUpdateData));

            path_len = strlen64(ud->filepath);
            remove_data->filepath
                = arena_push(cecup_state.ui_arena, path_len + 1);
            memcpy64(remove_data->filepath, ud->filepath, path_len + 1);
            g_mutex_unlock(&cecup_state.ui_arena_mutex);

            remove_data->type = DATA_TYPE_REMOVE_TREE_ROW;
            g_idle_add(update_ui_handler, remove_data);
        }

        g_mutex_lock(&cecup_state.ui_arena_mutex);
        if (ud->filepath) {
            arena_pop(cecup_state.ui_arena, ud->filepath);
        }
        if (ud->src_base) {
            arena_pop(cecup_state.ui_arena, ud->src_base);
        }
        if (ud->dst_base) {
            arena_pop(cecup_state.ui_arena, ud->dst_base);
        }
        if (ud->term_cmd) {
            arena_pop(cecup_state.ui_arena, ud->term_cmd);
        }
        if (ud->diff_tool) {
            arena_pop(cecup_state.ui_arena, ud->diff_tool);
        }
        arena_pop(cecup_state.ui_arena, ud);
        g_mutex_unlock(&cecup_state.ui_arena_mutex);
    }
out:

    g_mutex_lock(&cecup_state.ui_arena_mutex);
    ready = arena_push(cecup_state.ui_arena, SIZEOF(UIUpdateData));
    memset64(ready, 0, SIZEOF(UIUpdateData));
    g_mutex_unlock(&cecup_state.ui_arena_mutex);

    ready->type = DATA_TYPE_ENABLE_BUTTONS;
    g_idle_add(update_ui_handler, ready);

    g_ptr_array_unref(tasks);
    return NULL;
}

static gpointer
sync_worker(gpointer user_data) {
    ThreadData *thread_data;
    char cmd[4096];
    char log_cmd[8192];
    char *ex;
    int32 i;
    int32 j;
    int32 line_len;
    int32 last_space_index;
    int32 word_len;
    int32 pipe_output[2];
    int32 pipe_error[2];
    pid_t child_pid;
    struct pollfd pipes[2];
    char output_buffer[8192];
    char error_buffer[8192];
    int32 output_position;
    int32 error_position;
    int32 poll_return;
    UIUpdateData *ready;
    GThread *scanner_thread;
    int32 total_files_preview;
    int32 processed_files_preview;

    thread_data = (ThreadData *)user_data;
    scanner_thread = NULL;
    total_files_preview = 0;
    processed_files_preview = 0;

    if (thread_data->is_preview) {
        UIUpdateData *clear;
        g_mutex_lock(&cecup_state.ui_arena_mutex);
        clear = arena_push(cecup_state.ui_arena, SIZEOF(UIUpdateData));
        memset64(clear, 0, SIZEOF(UIUpdateData));
        g_mutex_unlock(&cecup_state.ui_arena_mutex);

        clear->type = DATA_TYPE_CLEAR_TREES;
        g_idle_add(update_ui_handler, clear);

        dispatch_log("Calculating file count for preview...\n");
        total_files_preview = count_files_recursive(thread_data->src_path, "");
    }

    if (thread_data->is_preview && thread_data->scan_equal) {
        EqualScannerData *sd;

        dispatch_log("Scanning for equal files (parallel)...\n");
        g_mutex_lock(&cecup_state.ui_arena_mutex);
        sd = arena_push(cecup_state.ui_arena, SIZEOF(EqualScannerData));
        memset64(sd, 0, SIZEOF(EqualScannerData));
        g_mutex_unlock(&cecup_state.ui_arena_mutex);

        strncpy(sd->src_path, thread_data->src_path, MAX_PATH_LENGTH - 1);
        strncpy(sd->dst_path, thread_data->dst_path, MAX_PATH_LENGTH - 1);
        scanner_thread
            = g_thread_new("equal_scanner", equal_scanner_worker, sd);
    }

    if (access(cecup_state.ignore_path, F_OK) != -1) {
        ex = g_strdup_printf("--exclude-from='%s'", cecup_state.ignore_path);
    } else {
        if (errno != ENOENT) {
            dispatch_log_error("Error access %s: %s.\n",
                               cecup_state.ignore_path, strerror(errno));
        }
        ex = g_strdup("");
    }

    SNPRINTF(cmd,
             "rsync " RSYNC_UNIVERSAL_ARGS " --delete-excluded %s %s "
             "'%s/' '%s/'",
             thread_data->is_preview ? "--dry-run" : "", ex,
             thread_data->src_path, thread_data->dst_path);
    g_free(ex);

    i = 0;
    j = 0;
    line_len = 0;
    last_space_index = -1;

    while (cmd[i] != '\0') {
        log_cmd[j] = cmd[i];
        if (cmd[i] == ' ') {
            last_space_index = j;
        }
        line_len += 1;
        if (line_len > 120 && last_space_index != -1) {
            log_cmd[last_space_index] = '\n';
            word_len = j - last_space_index;
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

    if (pipe(pipe_output) < 0) {
        dispatch_log_error("Error creating pipe for stdout: %s.\n",
                           strerror(errno));
        goto clean_exit;
    }

    if (pipe(pipe_error) < 0) {
        dispatch_log_error("Error creating pipe for stderr: %s.\n",
                           strerror(errno));
        XCLOSE(&pipe_output[0]);
        XCLOSE(&pipe_output[1]);
        goto clean_exit;
    }

    switch (child_pid = fork()) {
    case -1:
        dispatch_log_error("Error forking: %s.\n", strerror(errno));
        XCLOSE(&pipe_output[0]);
        XCLOSE(&pipe_output[1]);
        XCLOSE(&pipe_error[0]);
        XCLOSE(&pipe_error[1]);
        goto clean_exit;
    case 0:
        if (setpgid(0, 0) < 0) {
            fprintf(stderr, "Error setpgid: %s.\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
        XCLOSE(&pipe_output[0]);
        XCLOSE(&pipe_error[0]);
        if (dup2(pipe_output[1], STDOUT_FILENO) < 0) {
            fprintf(stderr, "Error dup2 stdout: %s.\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
        if (dup2(pipe_error[1], STDERR_FILENO) < 0) {
            fprintf(stderr, "Error dup2 stderr: %s.\n", strerror(errno));
            exit(EXIT_FAILURE);
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

    output_position = 0;
    error_position = 0;

    while (1) {
        if (cecup_state.cancel_sync) {
            if (kill(-child_pid, SIGTERM) < 0) {
                dispatch_log_error("Error kill process group: %s.\n",
                                   strerror(errno));
            }
            dispatch_log_error("rsync operation stopped by user.\n");
            break;
        }

        switch (poll_return = poll(pipes, 2, 100)) {
        case -1:
            dispatch_log_error("Error in poll: %s.\n", strerror(errno));
            goto out;
        case 0:
            continue;
        default:
            break;
        }

        if (pipes[0].revents & POLLIN) {
            char buffer[2048];
            int64 r;

            if ((r = read64(pipe_output[0], buffer, SIZEOF(buffer))) <= 0) {
                if (r < 0) {
                    dispatch_log_error("Error reading stdout pipe: %s.\n",
                                       strerror(errno));
                }
                pipes[0].fd = -1;
                goto output_end;
            }

            for (int64 k = 0; k < r; k += 1) {
                char *p_pos;
                char *space_pos;
                char type_char;

                if (buffer[k] != '\n' && buffer[k] != '\r'
                    && output_position < (int32)SIZEOF(output_buffer) - 1) {
                    output_buffer[output_position] = buffer[k];
                    output_position += 1;
                    continue;
                }

                output_buffer[output_position] = '\0';
                output_position = 0;

                if (output_buffer[0] == '\0') {
                    continue;
                }

                if ((p_pos = strstr(output_buffer, "%"))) {
                    char *start;
                    start = p_pos;
                    while (start > output_buffer && isdigit(*(start - 1))) {
                        start -= 1;
                    }
                    dispatch_progress(DATA_TYPE_PROGRESS_RSYNC,
                                      atof(start) / 100.0);
                }

                if (thread_data->is_preview == 0) {
                    dispatch_log(output_buffer);
                    continue;
                }

                if (strncmp(output_buffer, "*deleting", 9) == 0) {
                    char *relative_path;
                    char *full_src;
                    char *full_dst;
                    struct stat st_s;
                    struct stat st_d;
                    int64 sz;
                    enum CecupReason reason;

                    relative_path = output_buffer + 10;
                    while (isspace(*relative_path)) {
                        relative_path += 1;
                    }

                    full_src = g_build_filename(thread_data->src_path,
                                                relative_path, NULL);
                    full_dst = g_build_filename(thread_data->dst_path,
                                                relative_path, NULL);

                    if (lstat(full_dst, &st_d) == 0) {
                        sz = st_d.st_size;
                    } else {
                        dispatch_log_error("Error lstat %s: %s.\n", full_dst,
                                           strerror(errno));
                        sz = 0;
                    }

                    if (lstat(full_src, &st_s) == 0) {
                        reason = UI_REASON_IGNORED;
                    } else {
                        reason = UI_REASON_MISSING;
                    }

                    dispatch_tree(1, UI_ACTION_DELETE, relative_path, sz,
                                  reason);
                    g_free(full_src);
                    g_free(full_dst);
                    continue;
                }

                if (!(space_pos = strchr(output_buffer, ' '))) {
                    continue;
                }

                type_char = output_buffer[0];
                if (type_char != '>' && type_char != '.' && type_char != 'h'
                    && type_char != 'c') {
                    continue;
                }

                char *relative_path_entry;
                enum CecupAction action;
                char *full_src_path;
                struct stat st_path;
                int64 sz_path;

                relative_path_entry = space_pos + 1;
                while (isspace(*relative_path_entry)) {
                    relative_path_entry += 1;
                }

                action = UI_ACTION_UPDATE;

                if (strncmp(output_buffer, "hf", 2) == 0) {
                    action = UI_ACTION_HARDLINK;
                } else if (strncmp(output_buffer, "cd", 2) == 0
                           || strncmp(output_buffer, ">f+++++", 7) == 0) {
                    action = UI_ACTION_NEW;
                }

                full_src_path = g_build_filename(thread_data->src_path,
                                                 relative_path_entry, NULL);
                if (lstat(full_src_path, &st_path) < 0) {
                    dispatch_log_error("Error lstat %s: %s.\n", full_src_path,
                                       strerror(errno));
                    sz_path = 0;
                } else {
                    sz_path = st_path.st_size;
                }

                dispatch_tree(0, action, relative_path_entry, sz_path,
                              (enum CecupReason)action);
                g_free(full_src_path);

                processed_files_preview += 1;
                if (total_files_preview > 0) {
                    dispatch_progress(DATA_TYPE_PROGRESS_PREVIEW,
                                      (double)processed_files_preview
                                          / total_files_preview);
                }
            }
        } else if (pipes[0].revents & (POLLHUP | POLLERR)) {
            pipes[0].fd = -1;
        }

    output_end:

        if (pipes[1].revents & POLLIN) {
            char buffer[2048];
            int64 r = read64(pipe_error[0], buffer, SIZEOF(buffer));
            if (r < 0) {
                dispatch_log_error("Error reading stderr pipe: %s.\n",
                                   strerror(errno));
                pipes[1].fd = -1;
            } else if (r > 0) {
                for (int64 k = 0; k < r; k += 1) {
                    if (buffer[k] == '\n'
                        && error_position == SIZEOF(error_buffer) - 1) {
                        error_buffer[error_position] = '\0';
                        dispatch_log_error(error_buffer);
                        error_position = 0;
                    } else {
                        error_buffer[error_position] = buffer[k];
                        error_position += 1;
                    }
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
out:

    XCLOSE(&pipe_output[0]);
    XCLOSE(&pipe_error[0]);
    if (waitpid(child_pid, NULL, 0) < 0) {
        dispatch_log_error("Error waiting for child: %s.\n", strerror(errno));
    }

    if (!cecup_state.cancel_sync) {
        dispatch_log("Sync analysis finished.\n");
    }

clean_exit:
    if (scanner_thread != NULL) {
        g_thread_join(scanner_thread);
    }
    dispatch_progress(DATA_TYPE_PROGRESS_RSYNC, 1.0);
    dispatch_progress(DATA_TYPE_PROGRESS_PREVIEW, 1.0);

    g_mutex_lock(&cecup_state.ui_arena_mutex);
    ready = arena_push(cecup_state.ui_arena, SIZEOF(UIUpdateData));
    memset64(ready, 0, SIZEOF(UIUpdateData));
    g_mutex_unlock(&cecup_state.ui_arena_mutex);

    ready->type = DATA_TYPE_ENABLE_BUTTONS;
    g_idle_add(update_ui_handler, ready);

    g_mutex_lock(&cecup_state.ui_arena_mutex);
    arena_pop(cecup_state.ui_arena, thread_data);
    g_mutex_unlock(&cecup_state.ui_arena_mutex);
    return NULL;
}

static gpointer
diff_worker(gpointer user_data) {
    UIUpdateData *ud;
    char cmd[8192];

    ud = (UIUpdateData *)user_data;
    SNPRINTF(cmd,
             "%s -e bash -c \"%s '%s/%s' '%s/%s'; read -p 'Press Enter...'\" &",
             ud->term_cmd, ud->diff_tool, ud->src_base, ud->filepath,
             ud->dst_base, ud->filepath);
    if (system(cmd) < 0) {
        dispatch_log_error("Error system call: %s.\n", strerror(errno));
    }

    g_mutex_lock(&cecup_state.ui_arena_mutex);
    if (ud->filepath) {
        arena_pop(cecup_state.ui_arena, ud->filepath);
    }
    if (ud->src_base) {
        arena_pop(cecup_state.ui_arena, ud->src_base);
    }
    if (ud->dst_base) {
        arena_pop(cecup_state.ui_arena, ud->dst_base);
    }
    if (ud->term_cmd) {
        arena_pop(cecup_state.ui_arena, ud->term_cmd);
    }
    if (ud->diff_tool) {
        arena_pop(cecup_state.ui_arena, ud->diff_tool);
    }
    arena_pop(cecup_state.ui_arena, ud);
    g_mutex_unlock(&cecup_state.ui_arena_mutex);
    return NULL;
}
