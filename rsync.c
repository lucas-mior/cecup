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
#include "util.c"
#include "cecup.h"

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

    va_start(va_args, format);
    vsnprintf(buffer, sizeof(buffer), format, va_args);
    va_end(va_args);

    data = g_new0(UIUpdateData, 1);
    data->type = DATA_TYPE_LOG;
    data->message = g_strdup(buffer);
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

    g_free(data->message);
    g_free(data);
    return G_SOURCE_REMOVE;
}

static void
dispatch_log_error(char *format, ...) {
    UIUpdateData *data;
    va_list variable_arguments;
    char message_buffer[8192];

    va_start(variable_arguments, format);
    vsnprintf(message_buffer, sizeof(message_buffer), format,
              variable_arguments);
    va_end(variable_arguments);

    data = g_new0(UIUpdateData, 1);
    data->message = g_strdup(message_buffer);
    g_idle_add(log_error_handler, data);
    return;
}

static void
dispatch_progress(enum DataType type, double fraction) {
    UIUpdateData *data;

    data = g_new0(UIUpdateData, 1);
    data->type = type;
    data->fraction = fraction;
    g_idle_add(update_ui_handler, data);
    return;
}

static void
dispatch_tree(int32 side, enum CecupAction action, char *path, int64 size,
              enum CecupReason reason) {
    UIUpdateData *data;

    data = g_new0(UIUpdateData, 1);
    data->type = DATA_TYPE_TREE_ROW;
    data->side = side;
    data->action = action;
    data->filepath = g_strdup(path);
    data->reason = reason;
    data->size = size;
    g_idle_add(update_ui_handler, data);
    return;
}

static int32
count_files_recursive(char *base_path, char *relative_path) {
    DIR *dir;
    struct dirent *entry;
    char path[MAX_PATH_LENGTH];
    int32 count;

    count = 0;
    SNPRINTF(path, "%s/%s", base_path, relative_path);
    if (!(dir = opendir(path))) {
        return 0;
    }

    while ((entry = readdir(dir))) {
        char *name;
        char sub_rel[MAX_PATH_LENGTH];
        struct stat st;

        name = entry->d_name;
        if (name[0] == '.'
            && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'))) {
            continue;
        }

        if (strlen(relative_path) > 0) {
            SNPRINTF(sub_rel, "%s/%s", relative_path, name);
        } else {
            SNPRINTF(sub_rel, "%s", name);
        }

        SNPRINTF(path, "%s/%s", base_path, sub_rel);
        if (stat(path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                count += count_files_recursive(base_path, sub_rel);
            } else if (S_ISREG(st.st_mode)) {
                count += 1;
            }
        }
    }
    closedir(dir);
    return count;
}

static void
find_equal_files(EqualScannerData *sd, char *relative_path) {
    DIR *dir;
    struct dirent *entry;
    char src_path[MAX_PATH_LENGTH];
    char dst_path[MAX_PATH_LENGTH];
    int32 rel_len;
    char *name;

    if (cecup_state.cancel_sync) {
        return;
    }

    SNPRINTF(src_path, "%s/%s", sd->src_path, relative_path);
    if (!(dir = opendir(src_path))) {
        return;
    }

    rel_len = (int32)strlen(relative_path);

    while ((entry = readdir(dir))) {
        struct stat st_s;
        struct stat st_d;
        int32 name_len;
        char sub_rel[MAX_PATH_LENGTH];

        if (cecup_state.cancel_sync) {
            break;
        }

        name = entry->d_name;
        if (name[0] == '.'
            && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'))) {
            continue;
        }

        name_len = (int32)strlen(name);
        if (rel_len + name_len + 2 > MAX_PATH_LENGTH) {
            continue;
        }

        SNPRINTF(src_path, "%s/%s/%s", sd->src_path, relative_path, name);
        SNPRINTF(dst_path, "%s/%s/%s", sd->dst_path, relative_path, name);

        if (stat(src_path, &st_s) != 0) {
            continue;
        }

        if (rel_len > 0) {
            SNPRINTF(sub_rel, "%s/%s", relative_path, name);
        } else {
            SNPRINTF(sub_rel, "%s", name);
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

            if (stat(dst_path, &st_d) == 0) {
                if (st_s.st_size == st_d.st_size
                    && st_s.st_mtime == st_d.st_mtime) {
                    dispatch_tree(0, UI_ACTION_EQUAL, sub_rel, st_s.st_size,
                                  UI_REASON_EQUAL);
                }
            }
        }
    }

    closedir(dir);
    return;
}

static gpointer
equal_scanner_worker(gpointer user_data) {
    EqualScannerData *data;

    data = (EqualScannerData *)user_data;
    data->total_files = count_files_recursive(data->src_path, "");
    find_equal_files(data, "");
    dispatch_progress(DATA_TYPE_PROGRESS_EQUAL, 1.0);
    g_free(data);
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
            g_free(ud->filepath);
            g_free(ud->src_base);
            g_free(ud->dst_base);
            g_free(ud->term_cmd);
            g_free(ud->diff_tool);
            g_free(ud);
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
                     " --include='%s' --include='%s/**' --exclude='*'"
                     " '%s/' '%s/'",
                     ud->filepath, ud->filepath, ud->src_base, ud->dst_base);
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
            setpgid(0, 0);
            XCLOSE(&pipe_output[0]);
            XCLOSE(&pipe_error[0]);
            if (dup2(pipe_output[1], STDOUT_FILENO) < 0) {
                exit(1);
            }
            if (dup2(pipe_error[1], STDERR_FILENO) < 0) {
                exit(1);
            }
            XCLOSE(&pipe_output[1]);
            XCLOSE(&pipe_error[1]);
            execl("/bin/sh", "sh", "-c", cmd, NULL);
            fprintf(stderr, "Error: execl failed\n");
            exit(1);
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
                kill(-child_pid, SIGTERM);
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
                int64 r = read64(pipe_output[0], buffer, sizeof(buffer));
                if (r < 0) {
                    pipes[0].fd = -1;
                } else if (r > 0) {
                    for (int64 k = 0; k < r; k += 1) {
                        if (buffer[k] == '\n'
                            || output_position == sizeof(output_buffer) - 1) {
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
                int64 r = read64(pipe_error[0], buffer, sizeof(buffer));
                if (r < 0) {
                    pipes[1].fd = -1;
                } else if (r > 0) {
                    for (int64 k = 0; k < r; k += 1) {
                        if (buffer[k] == '\n'
                            || error_position == sizeof(error_buffer) - 1) {
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
            remove_data = g_new0(UIUpdateData, 1);
            remove_data->type = DATA_TYPE_REMOVE_TREE_ROW;
            remove_data->filepath = g_strdup(ud->filepath);
            g_idle_add(update_ui_handler, remove_data);
        }

        g_free(ud->filepath);
        g_free(ud->src_base);
        g_free(ud->dst_base);
        g_free(ud->term_cmd);
        g_free(ud->diff_tool);
        g_free(ud);
    }
out:

    ready = g_new0(UIUpdateData, 1);
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

    thread_data = (ThreadData *)user_data;
    scanner_thread = NULL;

    if (thread_data->is_preview) {
        UIUpdateData *clear;
        clear = g_new0(UIUpdateData, 1);
        clear->type = DATA_TYPE_CLEAR_TREES;
        g_idle_add(update_ui_handler, clear);
    }

    if (thread_data->is_preview && thread_data->scan_equal) {
        EqualScannerData *sd;

        dispatch_log("Scanning for equal files (parallel)...\n");
        sd = g_new0(EqualScannerData, 1);
        strncpy(sd->src_path, thread_data->src_path, MAX_PATH_LENGTH - 1);
        strncpy(sd->dst_path, thread_data->dst_path, MAX_PATH_LENGTH - 1);
        scanner_thread
            = g_thread_new("equal_scanner", equal_scanner_worker, sd);
    }

    ex = (access(cecup_state.exclude_path, F_OK) != -1)
             ? g_strdup_printf("--exclude-from='%s'", cecup_state.exclude_path)
             : g_strdup("");

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
        setpgid(0, 0);
        XCLOSE(&pipe_output[0]);
        XCLOSE(&pipe_error[0]);
        if (dup2(pipe_output[1], STDOUT_FILENO) < 0) {
            exit(1);
        }
        if (dup2(pipe_error[1], STDERR_FILENO) < 0) {
            exit(1);
        }
        XCLOSE(&pipe_output[1]);
        XCLOSE(&pipe_error[1]);
        execl("/bin/sh", "sh", "-c", cmd, NULL);
        fprintf(stderr, "Error: execl failed\n");
        exit(1);
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
            kill(-child_pid, SIGTERM);
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
            int64 r = read64(pipe_output[0], buffer, sizeof(buffer));
            if (r < 0) {
                pipes[0].fd = -1;
            } else if (r > 0) {
                for (int64 k = 0; k < r; k += 1) {
                    if (buffer[k] != '\n' && buffer[k] != '\r'
                        && output_position < (int32)sizeof(output_buffer) - 1) {
                        output_buffer[output_position] = buffer[k];
                        output_position += 1;
                        continue;
                    }

                    output_buffer[output_position] = '\0';
                    output_position = 0;

                    char *p_pos;
                    p_pos = strstr(output_buffer, "%");
                    if (p_pos != NULL) {
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
                        sz = (stat(full_dst, &st_d) == 0) ? st_d.st_size : 0;
                        reason = (stat(full_src, &st_s) == 0)
                                     ? UI_REASON_EXCLUDED
                                     : UI_REASON_MISSING;

                        dispatch_tree(1, UI_ACTION_DELETE, relative_path, sz,
                                      reason);
                        g_free(full_src);
                        g_free(full_dst);
                        continue;
                    }

                    char *space_pos;
                    space_pos = strchr(output_buffer, ' ');
                    if (space_pos == NULL) {
                        continue;
                    }

                    char type_char;
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
                    action = UI_ACTION_UPDATE;

                    if (strncmp(output_buffer, "hf", 2) == 0) {
                        action = UI_ACTION_HARDLINK;
                    } else if (strncmp(output_buffer, "cd", 2) == 0
                               || strncmp(output_buffer, ">f+++++", 7) == 0) {
                        action = UI_ACTION_NEW;
                    }

                    full_src_path = g_build_filename(thread_data->src_path,
                                                     relative_path_entry, NULL);
                    sz_path = (stat(full_src_path, &st_path) == 0)
                                  ? st_path.st_size
                                  : 0;
                    dispatch_tree(0, action, relative_path_entry, sz_path,
                                  (enum CecupReason)action);
                    g_free(full_src_path);
                }
            } else {
                pipes[0].fd = -1;
            }
        } else if (pipes[0].revents & (POLLHUP | POLLERR)) {
            pipes[0].fd = -1;
        }

        if (pipes[1].revents & POLLIN) {
            char buffer[2048];
            int64 r = read64(pipe_error[0], buffer, sizeof(buffer));
            if (r < 0) {
                pipes[1].fd = -1;
            } else if (r > 0) {
                for (int64 k = 0; k < r; k += 1) {
                    if (buffer[k] == '\n'
                        && error_position == sizeof(error_buffer) - 1) {
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
    ready = g_new0(UIUpdateData, 1);
    ready->type = DATA_TYPE_ENABLE_BUTTONS;
    g_idle_add(update_ui_handler, ready);
    g_free(thread_data);
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
    system(cmd);

    g_free(ud->filepath);
    g_free(ud->src_base);
    g_free(ud->dst_base);
    g_free(ud->term_cmd);
    g_free(ud->diff_tool);
    g_free(ud);
    return NULL;
}
