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

#if !defined(WORK_RSYNC)
#define WORK_RSYNC

#include <ctype.h>
#include "cecup.h"
#include "update.c"

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_work_rsync 1
#elif !defined(TESTING_work_rsync)
#define TESTING_work_rsync 0
#endif
#if !defined(TESTING)
#define TESTING 0
#endif

static void
work_batch_flush(MessageBatch **batch_ptr) {
    MessageBatch *batch;

    batch = *batch_ptr;
    if (batch == NULL) {
        return;
    }

    if (batch->count > 0) {
        g_idle_add(update_ui_handler, batch);
    } else {
        free(batch, SIZEOF(*batch));
    }

    *batch_ptr = NULL;
    return;
}

static void
work_batch_push(MessageBatch **batch_ptr, Message *message) {
    MessageBatch *batch;

    batch = *batch_ptr;
    if (batch == NULL) {
        batch = xmalloc(SIZEOF(*batch));
        memset64(batch, 0, SIZEOF(*batch));
        batch->type = MSG_BATCH;
        batch->count = 0;
        clock_gettime(CLOCK_MONOTONIC_COARSE, &batch->time_last_flush);
        *batch_ptr = batch;
    }

    batch->messages[batch->count] = message;
    batch->count += 1;

    if (batch->count >= LENGTH(batch->messages)) {
        work_batch_flush(batch_ptr);
    } else {
        struct timespec time_this_push;
        int64 time_diff;

        clock_gettime(CLOCK_MONOTONIC_COARSE, &time_this_push);
        time_diff = (int64)(time_this_push.tv_sec - batch->time_last_flush.tv_sec);

        if (time_diff > 10) {
            work_batch_flush(batch_ptr);
        }
    }

    return;
}

static char *
work_check_log_date(char *buf_output, int32 line_len) {
    char *bracket_open;
    char *bracket_close;
    int32 left;

    if (line_len <= strlen32(RSYNC_LOG_DATE_PLACEHOLDERS) + 2) {
        return NULL;
    }

    if (buf_output[4] != '/') {
        return NULL;
    }
    if (buf_output[7] != '/') {
        return NULL;
    }
    if (buf_output[10] != ' ') {
        return NULL;
    }

    if ((bracket_open = memchr64(buf_output + 10, '[', line_len - 10)) == NULL) {
        return NULL;
    }

    left = line_len - (int32)(bracket_open - buf_output);
    if ((bracket_close = memchr64(bracket_open, ']', left)) == NULL) {
        return NULL;
    }
    return bracket_close + 2;
}

static char *
work_check_itemize_line(char *buf_output, int32 line_len) {
    if (line_len <= strlen32(RSYNC_ITEMIZE_PLACEHOLDERS)) {
        return NULL;
    }
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

    return buf_output + strlen32(RSYNC_ITEMIZE_PLACEHOLDERS) + 1;
}

static bool
work_rsync_run(char *files_from_filename, int32 nfiles_total,
               bool checksum, MessageBatch **batch_ptr) {
    char *rsync_args[64];
    char buf_error[MAX_PATH_LENGTH*2];
    char buf_output[MAX_PATH_LENGTH*2];
    char cmd[MAX_PATH_LENGTH*2];
    char dst_base_with_slash[MAX_PATH_LENGTH];
    char src_base_with_slash[MAX_PATH_LENGTH];
    int32 pipe_stderr[2];
    int32 pipe_stdout[2];
    int32 rsync_args_len = 0;
    int32 buf_output_pos = 0;
    pid_t child_rsync;
    struct pollfd pipes[2];
    int32 nfiles_checksummed = 0;
    struct timespec time_stop = {0};
    bool delete_after = cecup.delete_after;
    bool dropping_long_line = false;

    if (checksum) {
        update_progress_state(_("Verifying checksums"),
                              _("Performing verification of the new files..."));
    } else {
        update_progress_state(_("Syncing files"),
                              _("Transferring data and updating metadata..."));
    }

    SNPRINTF(src_base_with_slash, "%s/", cecup.src_base);
    SNPRINTF(dst_base_with_slash, "%s/", cecup.dst_base);

    rsync_args_len = 0;
    rsync_args[rsync_args_len++] = "rsync";
    rsync_args[rsync_args_len++] = "--verbose";
    rsync_args[rsync_args_len++] = "--verbose";

    if (!delete_after) {
        rsync_args[rsync_args_len++] = "--update";
    }

    // these 2 options are implied by --files-from
    rsync_args[rsync_args_len++] = "--dirs";
    rsync_args[rsync_args_len++] = "--relative";

    rsync_args[rsync_args_len++] = "--partial";
    rsync_args[rsync_args_len++] = "--progress";
    rsync_args[rsync_args_len++] = "--info=progress2";
    rsync_args[rsync_args_len++] = "--links";
    rsync_args[rsync_args_len++] = "--bwlimit=1000";
    rsync_args[rsync_args_len++] = "--hard-links";
    if (checksum) {
        rsync_args[rsync_args_len++] = "--checksum";
    }
    rsync_args[rsync_args_len++] = "--perms";
    rsync_args[rsync_args_len++] = "--times";
    rsync_args[rsync_args_len++] = "--owner";
    rsync_args[rsync_args_len++] = "--group";
    rsync_args[rsync_args_len++] = "--log-file=/dev/stdout";
    rsync_args[rsync_args_len++] = "--log-file-format=%i %n%L";
    rsync_args[rsync_args_len++] = "--files-from";
    rsync_args[rsync_args_len++] = files_from_filename;
    rsync_args[rsync_args_len++] = "--iconv=.,.";

    rsync_args[rsync_args_len++] = src_base_with_slash;
    rsync_args[rsync_args_len++] = dst_base_with_slash;
    rsync_args[rsync_args_len++] = NULL;

    LOG(_("Running sync...\n"));
    STRING_FROM_ARRAY(cmd, " ", rsync_args, rsync_args_len - 1);
    LOG_CMD("%s\n", cmd);

    xpipe(pipe_stdout);
    xpipe(pipe_stderr);

    switch (child_rsync = fork()) {
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
        cecup.child_pid = child_rsync;
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

        if (cecup.stop_working) {
            if (time_stop.tv_sec == 0) {
                clock_gettime(CLOCK_MONOTONIC_COARSE, &time_stop);
            } else {
                struct timespec time_now;
                int64 time_diff;

                clock_gettime(CLOCK_MONOTONIC_COARSE, &time_now);
                time_diff = (int64)(time_now.tv_sec - time_stop.tv_sec);

                if (time_diff > 5) {
                    xkill(-child_rsync, SIGKILL);
                    time_stop.tv_sec = time_now.tv_sec;
                }
            }
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

        r = read64(pipe_stdout[0],
                   buf_output + buf_output_pos, SIZEOF(buf_output) - buf_output_pos - 1);

        if (r <= 0) {
            if (r < 0) {
                LOG_ERROR(_("Error reading stdout pipe: %s.\n"), strerror(errno));
                pipes[0].fd = -1;
            }
            goto read_error_pipe;
        }
        buf_output_pos += r;

        while (buf_output_pos > 0) {
            char *eol_lf = memchr64(buf_output, '\n', buf_output_pos);
            char *eol_cr = memchr64(buf_output, '\r', buf_output_pos);
            char *eol;
            char end;
            int32 line_len;
            char *path;
            char *itemize;
            int32 remaining;

            if (eol_lf && eol_cr) {
                if ((eol_lf < eol_cr)) {
                    eol = eol_lf;
                } else {
                    eol = eol_cr;
                }
            } else {
                if (eol_lf) {
                    eol = eol_lf;
                } else {
                    eol = eol_cr;
                }
            }

            if (eol == NULL) {
                if (buf_output_pos >= (SIZEOF(buf_output) - 1)) {
                    dropping_long_line = true;
                    buf_output_pos = 0;
                }
                break;
            }

            if (dropping_long_line) {
                dropping_long_line = false;
                remaining = buf_output_pos - ((int32)(eol - buf_output) + 1);
                if (remaining > 0) {
                    memmove64(buf_output, eol + 1, remaining);
                }
                buf_output_pos = remaining;
                continue;
            }

            end = *eol;
            line_len = (int32)(eol - buf_output);
            *eol = '\0';

            if (!BEGINS_WITH(buf_output, RSYNC_DUPLICATE)) {
                LOG("%s%c", buf_output, end);
            }

            if ((itemize = work_check_log_date(buf_output, line_len))
                 && (path = work_check_itemize_line(itemize, line_len - (itemize - buf_output)))) {
                int32 path_len;
                char *sep;

                if (checksum) {
                    nfiles_checksummed += 1;
                    update_progress_bar(MSG_PROGRESS,
                                        (double)nfiles_checksummed / (double)nfiles_total);
                }

                while (*path == ' ') {
                    path += 1;
                }
                path_len = (int32)(eol - path);
                if ((sep = memmem64(path, path_len, RSYNC_HARDLINK, strlen32(RSYNC_HARDLINK)))) {
                    *sep = '\0';
                    path_len = (int32)(sep - path);
                }
                if ((sep = memmem64(path, path_len, RSYNC_SYMLINK, strlen32(RSYNC_SYMLINK)))) {
                    *sep = '\0';
                    path_len = (int32)(sep - path);
                }

                if (checksum && ((path_len != 2) || memcmp64(path, "./", 2))) {
                    Message *msg_update = xmalloc(SIZEOF(*msg_update));
                    memset64(msg_update, 0, SIZEOF(*msg_update));

                    msg_update->type = MSG_ROW_TRANSFER;
                    msg_update->src_path_len = path_len;
                    msg_update->src_path = xmalloc(path_len + 1);
                    memcpy64(msg_update->src_path, path, path_len + 1);

                    work_batch_push(batch_ptr, msg_update);
                }
            } else {
                char *percentage;
                int64 progress;

                if (!checksum && (percentage = memmem64(buf_output + 1, line_len - 1, "% ", 2))) {
                    while (((percentage - 1) > buf_output)
                            && isdigit(*(percentage - 1))) {
                        percentage -= 1;
                    }
                    if (*(percentage - 1) == ' ') {
                        progress = atoll(percentage);
                        update_progress_bar(MSG_PROGRESS, (double)progress / 100.0);
                    }
                }
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
                LOG_ERROR(_("Error reading stderr pipe: %s.\n"), strerror(errno));
                pipes[1].fd = -1;
            }
            continue;
        }
        buf_error[r] = '\0';
        LOG_ERROR("%s", buf_error);

    } while ((pipes[0].fd >= 0) || (pipes[1].fd >= 0));

    if (waitpid(child_rsync, NULL, 0) < 0) {
        LOG_ERROR(_("Error waiting for child process: %s.\n"), strerror(errno));
        LOG_ERROR(_("Killing the child process with SIGKILL..."));
        xkill(-child_rsync, SIGKILL);
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
    MessageBatch *batch = NULL;
    int32 nfiles_total;

    if (tasks == NULL) {
        if (cecup.ntransfers <= 0) {
            LOG_ERROR(_("There are no operations to make.\n"));
            work_finalize(false);
            free(thread_data, SIZEOF(*thread_data));
            return NULL;
        } else {
            has_transfers = true;
            nfiles_total = cecup.ntransfers;
        }
        tasks = xmalloc(sizeof(*tasks));
        memset64(tasks, 0, sizeof(*tasks));
    } else {
        nfiles_total = tasks->count;
    }

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

        if (cecup.stop_working) {
            LOG_ERROR(_("Stop requested.\n"));
            free_task_list(tasks);
            work_batch_flush(&batch);
            work_finalize(false);
            free(thread_data, SIZEOF(*thread_data));
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

            STRING_FROM_ARRAY(cmd_rm, " ", args_rm, LENGTH(args_rm) - 1);

            execvp(args_rm[0], args_rm);
            error("Error executing\n%s\n%s.\n", cmd_rm, strerror(errno));
            _exit(EXIT_FAILURE);
        }
        default:
        {
            int waited;
            int32 term_timeout = 0;
            cecup.child_pid = child_rm;

            while ((waited = waitpid(child_rm, &child_status, WNOHANG)) == 0) {
                if (cecup.stop_working) {
                    term_timeout += 1;
                    if (term_timeout > 50) {
                        xkill(child_rm, SIGKILL);
                        term_timeout = 0;
                    }
                }
                usleep(100*1000);
            }

            if ((waited > 0) && WIFEXITED(child_status)) {
                removed = !WEXITSTATUS(child_status);
            }
            cecup.child_pid = 0;
            break;
        }
        }

        if (removed) {
            Message *message = xmalloc(SIZEOF(*message));
            memset64(message, 0, SIZEOF(*message));

            message->type = MSG_ROW_REMOVE;
            message->side = task->side;
            message->src_path_len = task->path_len;
            message->src_path = xmalloc(message->src_path_len + 1);
            memcpy64(message->src_path, task->path, message->src_path_len + 1);

            work_batch_push(&batch, message);

            LOG("Removed %s...\n", full_path);
        }
    }

    if (!has_transfers) {
        LOG_ERROR(_("No transfers to make.\n"));
        work_batch_flush(&batch);
        work_finalize(false);
        free_task_list(tasks);
        free(thread_data, SIZEOF(*thread_data));
        return NULL;
    }

    if ((files_from_fd = mkstemp(files_from_filename)) < 0) {
        error("Error in mkstemp: %s.\n", strerror(errno));
        fatal(EXIT_FAILURE);
    }

    for (int32 i = 0; (tasks->count == 0) && (i < cecup.ntransfers); i += 1) {
        char *file = cecup.transfers[i];
        int64 left = cecup.transfers_lens[i];
        int64 w;
        int64 written = 0;

        if (left > 1) {
            if (file[left - 1] == '/') {
                left -= 1;
            }
        }

        while ((w = write64(files_from_fd, &file[written], left)) > 0) {
            written += w;
            left -= w;
            if (left <= 0) {
                break;
            }
        }
        if (w < 0) {
            error("Error writing to %s: %s.\n", files_from_filename, strerror(errno));
            fatal(EXIT_FAILURE);
        }

        write64(files_from_fd, "\n", 1);
    }

    for (int32 i = 0; i < tasks->count; i += 1) {
        Task *task = tasks->items[i];
        int32 write_len;

        if ((task->action == ACTION_EQUAL) || (task->action == ACTION_IGNORE)) {
            continue;
        }

        if (task->action == ACTION_HARDLINK) {
            for (HardLinkList *link = task->hard_links; link->next; link = link->next) {
                write_len = link->name_len;
                if (write_len > 1) {
                    if (link->name[write_len - 1] == '/') {
                        write_len -= 1;
                    }
                }
                write64(files_from_fd, link->name, write_len);
                write64(files_from_fd, "\n", 1);
            }
        }

        write_len = task->path_len;
        if (write_len > 1) {
            if (task->path[write_len - 1] == '/') {
                write_len -= 1;
            }
        }
        write64(files_from_fd, task->path, write_len);
        write64(files_from_fd, "\n", 1);
    }

    XCLOSE(&files_from_fd);

    do {
        if (work_rsync_run(files_from_filename, nfiles_total, false, &batch)) {
            if (cecup.stop_working) {
                LOG_ERROR(_("Stop requested.\n"));
                break;
            }
            work_rsync_run(files_from_filename, nfiles_total, true, &batch);
        }
    } while (0);
    if (!DEBUGGING) {
        xunlink(files_from_filename);
    }

    work_batch_flush(&batch);
    free_task_list(tasks);
    work_finalize(false);
    free(thread_data, SIZEOF(*thread_data));
    return NULL;
}

#if 0 == TESTING_work_rsync
static inline void
work_rsync_functions_sink(void) {
    (void)work_rsync_functions_sink;
    (void)work_rsync;
    (void)work_rsync_run;
}
#endif

#if TESTING_work_rsync
#include "work.c"

int
main(void) {
    (void)work_rsync;
    (void)work_rsync_run;
    return 0;
}

#endif

#endif /* WORK_RSYNC_C */
