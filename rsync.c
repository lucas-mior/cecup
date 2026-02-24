#include <gtk/gtk.h>
#include <ctype.h>
#include <sys/wait.h>
#include <dirent.h>
#include <poll.h>
#include <unistd.h>
#include "util.c"
#include "cecup.h"

static void
dispatch_log(AppWidgets *w, char *msg) {
    UIUpdateData *data;

    data = g_new0(UIUpdateData, 1);
    data->widgets = w;
    data->type = DATA_TYPE_LOG;
    data->message = g_strdup(msg);
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
    table = gtk_text_buffer_get_tag_table(data->widgets->log_buffer);
    tag = gtk_text_tag_table_lookup(table, "err_red");
    if (tag == NULL) {
        gtk_text_buffer_create_tag(data->widgets->log_buffer, "err_red",
                                   "foreground", "red", NULL);
    }

    gtk_text_buffer_get_end_iter(data->widgets->log_buffer, &end);
    gtk_text_buffer_insert_with_tags_by_name(
        data->widgets->log_buffer, &end, data->message, -1, "err_red", NULL);
    gtk_text_buffer_insert(data->widgets->log_buffer, &end, "\n", -1);

    g_free(data->message);
    g_free(data);
    return G_SOURCE_REMOVE;
}

static void
dispatch_log_error(AppWidgets *w, char *msg) {
    UIUpdateData *data;

    data = g_new0(UIUpdateData, 1);
    data->widgets = w;
    data->message = g_strdup(msg);
    g_idle_add(log_error_handler, data);
    return;
}

static void
dispatch_tree(AppWidgets *w, int32 side, char *act, char *path, int64 size,
              char *reason) {
    UIUpdateData *data;

    data = g_new0(UIUpdateData, 1);
    data->widgets = w;
    data->type = DATA_TYPE_TREE_ROW;
    data->side = side;
    data->action = g_strdup(act);
    data->filepath = g_strdup(path);
    data->reason = g_strdup(reason);
    data->size = size;
    g_idle_add(update_ui_handler, data);
    return;
}

static void
find_equal_files(AppWidgets *w, char *src_base, char *dst_base,
                 char *relative_path) {
    DIR *dir;
    struct dirent *entry;
    char *full_src;

    full_src = g_build_filename(src_base, relative_path, NULL);
    if (!(dir = opendir(full_src))) {
        g_free(full_src);
        return;
    }

    while ((entry = readdir(dir))) {
        char *name;
        char *sub_rel;
        char *s_path;
        char *d_path;
        struct stat st_s;
        struct stat st_d;

        name = entry->d_name;
        if (g_strcmp0(name, ".") == 0 || g_strcmp0(name, "..") == 0) {
            continue;
        }

        sub_rel = g_build_filename(relative_path, name, NULL);
        s_path = g_build_filename(src_base, sub_rel, NULL);
        d_path = g_build_filename(dst_base, sub_rel, NULL);

        if (stat(s_path, &st_s) == 0 && stat(d_path, &st_d) == 0) {
            if (S_ISDIR(st_s.st_mode)) {
                find_equal_files(w, src_base, dst_base, sub_rel);
            } else if (S_ISREG(st_s.st_mode)) {
                if (st_s.st_size == st_d.st_size
                    && st_s.st_mtime == st_d.st_mtime) {
                    dispatch_tree(w, 0, "Equal", sub_rel, st_s.st_size,
                                  "Identical");
                }
            }
        }
        g_free(sub_rel);
        g_free(s_path);
        g_free(d_path);
    }
    closedir(dir);
    g_free(full_src);
    return;
}

static gpointer
bulk_sync_worker(gpointer user_data) {
    GList *tasks;

    tasks = (GList *)user_data;

    for (GList *l = tasks; l != NULL; l = l->next) {
        UIUpdateData *ud;
        char cmd[4096];
        int32 pipe_output[2];
        int32 pipe_error[2];
        pid_t child_pid;
        struct pollfd poll_descriptors[2];
        char output_buffer[8192];
        char error_buffer[8192];
        char temp_output[2048];
        char temp_error[2048];
        int32 output_position;
        int32 error_position;
        ssize_t output_bytes;
        ssize_t error_bytes;
        int32 poll_return;
        UIUpdateData *remove_data;

        ud = (UIUpdateData *)l->data;

        if (g_strcmp0(ud->action, "Delete") == 0) {
            char *full_dst;

            full_dst = g_build_filename(ud->dst_base, ud->filepath, NULL);
            snprintf(cmd, sizeof(cmd), "rm -rfv '%s'", full_dst);
            g_free(full_dst);
        } else {
            snprintf(cmd, sizeof(cmd),
                     "rsync --verbose --update --recursive "
                     "--partial --progress --info=progress2 "
                     "--links --hard-links --itemize-changes "
                     "--perms --times --owner --group "
                     "--include='%s' --include='%s/**' --exclude='*' '%s/' "
                     "'%s/'",
                     ud->filepath, ud->filepath, ud->src_base, ud->dst_base);
        }

        dispatch_log(ud->widgets, cmd);

        pipe(pipe_output);
        pipe(pipe_error);
        child_pid = fork();

        if (child_pid == 0) {
            close(pipe_output[0]);
            close(pipe_error[0]);
            dup2(pipe_output[1], STDOUT_FILENO);
            dup2(pipe_error[1], STDERR_FILENO);
            close(pipe_output[1]);
            close(pipe_error[1]);
            execl("/bin/sh", "sh", "-c", cmd, NULL);
            exit(1);
        }

        close(pipe_output[1]);
        close(pipe_error[1]);

        poll_descriptors[0].fd = pipe_output[0];
        poll_descriptors[0].events = POLLIN;
        poll_descriptors[1].fd = pipe_error[0];
        poll_descriptors[1].events = POLLIN;

        output_position = 0;
        error_position = 0;

        while (1) {
            poll_return = poll(poll_descriptors, 2, -1);
            if (poll_return < 0) {
                break;
            }

            if (poll_descriptors[0].revents & POLLIN) {
                output_bytes
                    = read(pipe_output[0], temp_output, sizeof(temp_output));
                if (output_bytes > 0) {
                    for (ssize_t k = 0; k < output_bytes; k += 1) {
                        if (temp_output[k] == '\n'
                            || output_position == sizeof(output_buffer) - 1) {
                            output_buffer[output_position] = '\0';
                            dispatch_log(ud->widgets, output_buffer);
                            output_position = 0;
                        } else {
                            output_buffer[output_position] = temp_output[k];
                            output_position += 1;
                        }
                    }
                } else {
                    poll_descriptors[0].fd = -1;
                }
            } else if (poll_descriptors[0].revents & (POLLHUP | POLLERR)) {
                poll_descriptors[0].fd = -1;
            }

            if (poll_descriptors[1].revents & POLLIN) {
                error_bytes
                    = read(pipe_error[0], temp_error, sizeof(temp_error));
                if (error_bytes > 0) {
                    for (ssize_t k = 0; k < error_bytes; k += 1) {
                        if (temp_error[k] == '\n'
                            || error_position == sizeof(error_buffer) - 1) {
                            error_buffer[error_position] = '\0';
                            dispatch_log_error(ud->widgets, error_buffer);
                            error_position = 0;
                        } else {
                            error_buffer[error_position] = temp_error[k];
                            error_position += 1;
                        }
                    }
                } else {
                    poll_descriptors[1].fd = -1;
                }
            } else if (poll_descriptors[1].revents & (POLLHUP | POLLERR)) {
                poll_descriptors[1].fd = -1;
            }

            if (poll_descriptors[0].fd == -1 && poll_descriptors[1].fd == -1) {
                break;
            }
        }

        close(pipe_output[0]);
        close(pipe_error[0]);
        waitpid(child_pid, NULL, 0);

        if (output_position > 0) {
            output_buffer[output_position] = '\0';
            dispatch_log(ud->widgets, output_buffer);
        }
        if (error_position > 0) {
            error_buffer[error_position] = '\0';
            dispatch_log_error(ud->widgets, error_buffer);
        }

        remove_data = g_new0(UIUpdateData, 1);
        remove_data->widgets = ud->widgets;
        remove_data->type = DATA_TYPE_REMOVE_TREE_ROW;
        remove_data->filepath = g_strdup(ud->filepath);
        g_idle_add(update_ui_handler, remove_data);

        g_free(ud->filepath);
        g_free(ud->action);
        g_free(ud->src_base);
        g_free(ud->dst_base);
        g_free(ud);
    }
    g_list_free(tasks);
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
    struct pollfd poll_descriptors[2];
    char output_buffer[8192];
    char error_buffer[8192];
    char temp_output[2048];
    char temp_error[2048];
    int32 output_position;
    int32 error_position;
    ssize_t output_bytes;
    ssize_t error_bytes;
    int32 poll_return;
    UIUpdateData *ready;

    thread_data = (ThreadData *)user_data;

    if (thread_data->is_preview) {
        UIUpdateData *clear;

        clear = g_new0(UIUpdateData, 1);
        clear->widgets = thread_data->widgets;
        clear->type = DATA_TYPE_CLEAR_TREES;
        g_idle_add(update_ui_handler, clear);
    }

    ex = (access(thread_data->widgets->exclude_path, F_OK) != -1)
             ? g_strdup_printf("--exclude-from='%s'",
                               thread_data->widgets->exclude_path)
             : g_strdup("");

    snprintf(cmd, sizeof(cmd),
             "rsync --verbose --update --recursive"
             " --partial --progress --info=progress2"
             " --links --hard-links --itemize-changes"
             " --perms --times --owner --group"
             " --delete-excluded %s %s "
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

    dispatch_log(thread_data->widgets, log_cmd);

    pipe(pipe_output);
    pipe(pipe_error);
    child_pid = fork();

    if (child_pid == 0) {
        close(pipe_output[0]);
        close(pipe_error[0]);
        dup2(pipe_output[1], STDOUT_FILENO);
        dup2(pipe_error[1], STDERR_FILENO);
        close(pipe_output[1]);
        close(pipe_error[1]);
        execl("/bin/sh", "sh", "-c", cmd, NULL);
        exit(1);
    }

    close(pipe_output[1]);
    close(pipe_error[1]);

    poll_descriptors[0].fd = pipe_output[0];
    poll_descriptors[0].events = POLLIN;
    poll_descriptors[1].fd = pipe_error[0];
    poll_descriptors[1].events = POLLIN;

    output_position = 0;
    error_position = 0;

    while (1) {
        poll_return = poll(poll_descriptors, 2, -1);
        if (poll_return < 0) {
            break;
        }

        if (poll_descriptors[0].revents & POLLIN) {
            output_bytes
                = read(pipe_output[0], temp_output, sizeof(temp_output));
            if (output_bytes > 0) {
                for (ssize_t k = 0; k < output_bytes; k += 1) {
                    if (temp_output[k] == '\n'
                        || output_position == sizeof(output_buffer) - 1) {
                        output_buffer[output_position] = '\0';

                        if (thread_data->is_preview) {
                            if (strncmp(output_buffer, "*deleting", 9) == 0) {
                                char *relative_path;
                                char *full_src;
                                char *full_dst;
                                struct stat st_s;
                                struct stat st_d;
                                int64 sz;
                                char *reason;

                                relative_path = output_buffer + 10;
                                while (isspace(*relative_path)) {
                                    relative_path++;
                                }

                                full_src = g_build_filename(
                                    thread_data->src_path, relative_path, NULL);
                                full_dst = g_build_filename(
                                    thread_data->dst_path, relative_path, NULL);
                                sz = (stat(full_dst, &st_d) == 0) ? st_d.st_size
                                                                  : 0;
                                reason = (stat(full_src, &st_s) == 0)
                                             ? "Excluded by pattern"
                                             : "Missing in source";

                                dispatch_tree(thread_data->widgets, 1, "Delete",
                                              relative_path, sz, reason);
                                g_free(full_src);
                                g_free(full_dst);
                            } else if (strchr(output_buffer, ' ')
                                       && (output_buffer[0] == '>'
                                           || output_buffer[0] == '.'
                                           || output_buffer[0] == 'h'
                                           || output_buffer[0] == 'c')) {
                                char *relative_path;
                                char *act;
                                char *full_src;
                                struct stat st;
                                int64 sz;

                                relative_path = strchr(output_buffer, ' ') + 1;
                                act = "Update";

                                if (strncmp(output_buffer, "hf", 2) == 0) {
                                    act = "Hardlink";
                                } else if (strncmp(output_buffer, "cd", 2) == 0
                                           || strncmp(output_buffer, ">f+++++",
                                                      7)
                                                  == 0) {
                                    act = "New";
                                }

                                full_src = g_build_filename(
                                    thread_data->src_path, relative_path, NULL);
                                sz = (stat(full_src, &st) == 0) ? st.st_size
                                                                : 0;
                                dispatch_tree(thread_data->widgets, 0, act,
                                              relative_path, sz, act);
                                g_free(full_src);
                            }
                        } else {
                            dispatch_log(thread_data->widgets, output_buffer);
                        }

                        output_position = 0;
                    } else {
                        output_buffer[output_position] = temp_output[k];
                        output_position += 1;
                    }
                }
            } else {
                poll_descriptors[0].fd = -1;
            }
        } else if (poll_descriptors[0].revents & (POLLHUP | POLLERR)) {
            poll_descriptors[0].fd = -1;
        }

        if (poll_descriptors[1].revents & POLLIN) {
            error_bytes = read(pipe_error[0], temp_error, sizeof(temp_error));
            if (error_bytes > 0) {
                for (ssize_t k = 0; k < error_bytes; k += 1) {
                    if (temp_error[k] == '\n'
                        || error_position == sizeof(error_buffer) - 1) {
                        error_buffer[error_position] = '\0';
                        dispatch_log_error(thread_data->widgets, error_buffer);
                        error_position = 0;
                    } else {
                        error_buffer[error_position] = temp_error[k];
                        error_position += 1;
                    }
                }
            } else {
                poll_descriptors[1].fd = -1;
            }
        } else if (poll_descriptors[1].revents & (POLLHUP | POLLERR)) {
            poll_descriptors[1].fd = -1;
        }

        if (poll_descriptors[0].fd == -1 && poll_descriptors[1].fd == -1) {
            break;
        }
    }

    close(pipe_output[0]);
    close(pipe_error[0]);
    waitpid(child_pid, NULL, 0);

    if (output_position > 0) {
        output_buffer[output_position] = '\0';
        if (!thread_data->is_preview) {
            dispatch_log(thread_data->widgets, output_buffer);
        }
    }

    if (error_position > 0) {
        error_buffer[error_position] = '\0';
        dispatch_log_error(thread_data->widgets, error_buffer);
    }

    if (thread_data->is_preview && thread_data->show_equal) {
        find_equal_files(thread_data->widgets, thread_data->src_path,
                         thread_data->dst_path, "");
    }

    ready = g_new0(UIUpdateData, 1);
    ready->widgets = thread_data->widgets;
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
    snprintf(cmd, sizeof(cmd),
             "%s -e bash -c \"%s '%s/%s' '%s/%s'; read -p 'Press Enter...'\" &",
             ud->term_cmd, ud->diff_tool, ud->src_base, ud->filepath,
             ud->dst_base, ud->filepath);
    system(cmd);

    g_free(ud->filepath);
    g_free(ud->action);
    g_free(ud->src_base);
    g_free(ud->dst_base);
    g_free(ud->term_cmd);
    g_free(ud->diff_tool);
    g_free(ud);
    return NULL;
}
