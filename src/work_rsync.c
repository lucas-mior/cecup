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
#include <fts.h>

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
    MessageBatch *batch = *batch_ptr;

    if (batch == NULL) {
        return;
    }

    if (batch->count > 0) {
        g_idle_add(update_ui_handler, batch);
    } else {
        error("Called work_batch_flush without count!\n");
        if (batch->paths != NULL) {
            free(batch->paths, batch->capacity * SIZEOF(*(batch->paths)));
            free(batch->paths_lens, batch->capacity * SIZEOF(*(batch->paths_lens)));
        }
        free(batch, SIZEOF(*batch));
    }

    *batch_ptr = NULL;
    return;
}

static void
work_batch_push(MessageBatch **batch_ptr, enum MsgType type, int32 side, char *path, int32 path_len) {
    MessageBatch *batch = *batch_ptr;

    if (batch && (batch->type != type)) {
        work_batch_flush(batch_ptr);
        batch = NULL;
    }

    if (batch == NULL) {
        batch = xmalloc(SIZEOF(*batch));
        memset64(batch, 0, SIZEOF(*batch));

        batch->type = type;
        batch->side = (int8)side;
        batch->capacity = INITIAL_CAPACITY;
        batch->paths = xmalloc(batch->capacity * SIZEOF(*(batch->paths)));
        batch->paths_lens = xmalloc(batch->capacity * SIZEOF(*(batch->paths_lens)));

        clock_gettime(CLOCK_MONOTONIC_COARSE, &batch->time_last_flush);
        *batch_ptr = batch;
    }

    if (batch->count >= batch->capacity) {
        int32 old_capacity;

        old_capacity = batch->capacity;
        batch->capacity *= 2;
        batch->paths = realloc(batch->paths,
                               old_capacity, batch->capacity,
                               SIZEOF(*(batch->paths)));
        batch->paths_lens = realloc(batch->paths_lens,
                                    old_capacity, batch->capacity,
                                    SIZEOF(*(batch->paths_lens)));
    }

    batch->paths[batch->count] = xmalloc(path_len + 1);
    memcpy64(batch->paths[batch->count], path, path_len + 1);
    batch->paths_lens[batch->count] = path_len;
    batch->count += 1;

    if (batch->count >= batch->capacity) {
        work_batch_flush(batch_ptr);
    } else {
        struct timespec time_this_push;
        int64 time_diff;

        clock_gettime(CLOCK_MONOTONIC_COARSE, &time_this_push);
        time_diff = (int64)(time_this_push.tv_sec - batch->time_last_flush.tv_sec);

        if (time_diff > 1) {
            work_batch_flush(batch_ptr);
        }
    }

    return;
}

static void
work_batch_push_rename(MessageBatch **batch_ptr, enum MsgType type, int8 side,
                       char *old_path, int32 old_len,
                       char *new_path, int32 new_len) {
    MessageBatch *batch = *batch_ptr;

    if (batch && (batch->type != MSG_BATCH_ROW_RENAME)) {
        work_batch_flush(batch_ptr);
        batch = NULL;
    }

    if (batch == NULL) {
        batch = xmalloc(SIZEOF(*batch));
        memset64(batch, 0, SIZEOF(*batch));

        batch->type = type;
        batch->side = side;
        batch->capacity = INITIAL_CAPACITY;
        batch->paths = xmalloc(batch->capacity * SIZEOF(*(batch->paths)));
        batch->paths_lens = xmalloc(batch->capacity * SIZEOF(*(batch->paths_lens)));
        batch->dst_paths = xmalloc(batch->capacity * SIZEOF(*(batch->dst_paths)));
        batch->dst_paths_lens = xmalloc(batch->capacity * SIZEOF(*(batch->dst_paths_lens)));

        clock_gettime(CLOCK_MONOTONIC_COARSE, &batch->time_last_flush);
        *batch_ptr = batch;
    }

    if (batch->count >= batch->capacity) {
        int32 old_capacity;

        old_capacity = batch->capacity;
        batch->capacity *= 2;
        batch->paths = realloc(batch->paths, old_capacity, batch->capacity, SIZEOF(*(batch->paths)));
        batch->paths_lens = realloc(batch->paths_lens, old_capacity, batch->capacity, SIZEOF(*(batch->paths_lens)));
        batch->dst_paths = realloc(batch->dst_paths, old_capacity, batch->capacity, SIZEOF(*(batch->dst_paths)));
        batch->dst_paths_lens = realloc(batch->dst_paths_lens, old_capacity, batch->capacity, SIZEOF(*(batch->dst_paths_lens)));
    }

    batch->paths[batch->count] = xmalloc(old_len + 1);
    memcpy64(batch->paths[batch->count], old_path, old_len + 1);
    batch->paths_lens[batch->count] = old_len;

    batch->dst_paths[batch->count] = xmalloc(new_len + 1);
    memcpy64(batch->dst_paths[batch->count], new_path, new_len + 1);
    batch->dst_paths_lens[batch->count] = new_len;

    batch->count += 1;

    if (batch->count >= batch->capacity) {
        work_batch_flush(batch_ptr);
    }

    return;
}

static char *
work_rsync_itemize_skip(char *buf_output, int32 line_len) {
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
    rsync_args[rsync_args_len++] = "--hard-links";
    if (checksum) {
        rsync_args[rsync_args_len++] = "--checksum";
    }
    rsync_args[rsync_args_len++] = "--perms";
    rsync_args[rsync_args_len++] = "--times";
    rsync_args[rsync_args_len++] = "--owner";
    rsync_args[rsync_args_len++] = "--group";
    rsync_args[rsync_args_len++] = "--itemize-changes";
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

        // TODO: Integration Bug. Data race. `cecup.stop_working` is written by the GTK UI thread in
        // `aux.c` using `cecup.stop_lock`, but is being read directly here in the background worker
        // thread without acquiring the mutex lock.
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
            // TODO: Bug. Setting fd = -1 on POLLHUP while POLLIN is also set discards the remaining
            // unread data in the pipe buffer. The file descriptor should only be closed when
            // `read64` actually returns 0.
            pipes[0].fd = -1;
        }
        if (!(pipes[0].revents & POLLIN)) {
            goto read_error_pipe;
        }

        // TODO: Bug. If `buf_output` fills up completely without hitting an EOL in the previous
        // iteration, `SIZEOF(buf_output) - buf_output_pos - 1` evaluates to 0.  `read64` is called
        // with a size of 0, returning 0 immediately, which is misinterpreted below as EOF,
        // permanently and prematurely closing the pipe.
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

            if ((path = work_rsync_itemize_skip(buf_output, line_len))) {
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
                    work_batch_push(batch_ptr, MSG_BATCH_ROW_TRANSFER, R, path, path_len);
                }
            } else if (line_len > 2) {
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

static void
work_remove(MessageBatch **batch, char *path, int32 path_len, int32 side) {
    char full_path[MAX_PATH_LENGTH];
    int32 base_path_len;

    if (side == L) {
        SNPRINTF(full_path, "%s/%s", cecup.src_base, path);
        base_path_len = cecup.src_base_len;
    } else {
        SNPRINTF(full_path, "%s/%s", cecup.dst_base, path);
        base_path_len = cecup.dst_base_len;
    }

    ASSERT_MORE(path_len, 0);

    if (path[path_len - 1] != '/') {
        if (unlink(full_path) < 0) {
            error("Error in unlink(%s): %s.\n", full_path, strerror(errno));
        } else {
            work_batch_push(batch, MSG_BATCH_ROW_REMOVE, side, path, path_len);
            LOG("Removed %s...\n", full_path);
        }
    } else {
        char *paths[] = {full_path, NULL};
        FTS *fts_handle;
        FTSENT *entry;

        if ((fts_handle = fts_open(paths, FTS_PHYSICAL | FTS_NOCHDIR, NULL)) == NULL) {
            error("Error in fts_open(%s): %s.\n", paths[0], strerror(errno));
            return;
        }

        while ((entry = fts_read(fts_handle))) {
            char *path_tmp;
            int32 rel_path_len;
            int32 is_dir = false;
            char rel_path[MAX_PATH_LENGTH];

            if (cecup.stop_working) {
                break;
            }

            switch (entry->fts_info) {
            case FTS_F:
            case FTS_SL:
            case FTS_SLNONE:
            case FTS_DEFAULT:
                break;
            case FTS_DP:
                is_dir = true;
                break;
            case FTS_DNR:
            case FTS_ERR:
            case FTS_NS:
                error("FTS error on %s: %s.\n", entry->fts_path, strerror(entry->fts_errno));
                continue;
            default:
                continue;
            }

            path_tmp = entry->fts_path + base_path_len;
            rel_path_len = (int32)entry->fts_pathlen - base_path_len;

            if (path_tmp[0] == '/') {
                path_tmp += 1;
                rel_path_len -= 1;
            }

            if (rel_path_len == 0) {
                path_tmp = ".";
                rel_path_len = 1;
            }

            // TODO: Bug. Buffer overflow risk. `entry->fts_pathlen` can be arbitrarily large for
            // deeply nested directories, causing `rel_path_len` to exceed `MAX_PATH_LENGTH`, which
            // will overflow the `rel_path` stack buffer.
            memcpy64(rel_path, path_tmp, rel_path_len + 1);
            normalize(rel_path, &rel_path_len);

            if (is_dir && (rel_path[rel_path_len - 1] != '/')) {
                rel_path_len += 1;
                rel_path[rel_path_len - 1] = '/';
                rel_path[rel_path_len] = '\0';
            }

            if (is_dir) {
                if (rmdir(entry->fts_accpath) < 0) {
                    error("Error in rmdir(%s): %s.\n", entry->fts_accpath, strerror(errno));
                } else {
                    work_batch_push(batch, MSG_BATCH_ROW_REMOVE, side, rel_path, rel_path_len);
                }
            } else {
                if (unlink(entry->fts_accpath) < 0) {
                    error("Error in unlink(%s): %s.\n", entry->fts_accpath, strerror(errno));
                } else {
                    work_batch_push(batch, MSG_BATCH_ROW_REMOVE, side, rel_path, rel_path_len);
                }
            }
        }
        fts_close(fts_handle);
        LOG("Removed directory tree %s...\n", full_path);
    }

    return;
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
        if ((cecup.ntransfers <= 0) && (cecup.ndeletions <= 0)) {
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

    for (int32 i = 0; (tasks->count == 0) && (i < cecup.ndeletions); i += 1) {
        // TODO: Integration Bug. Data race. `cecup.stop_working` is read here without using atomic
        // operations or `cecup.stop_lock`.
        if (cecup.stop_working) {
            LOG_ERROR(_("Stop requested.\n"));
            task_list_free(tasks);
            work_batch_flush(&batch);
            work_finalize(false);
            free(thread_data, SIZEOF(*thread_data));
            return NULL;
        }

        work_remove(&batch, cecup.deletions[i], cecup.deletions_lens[i], R);
    }

    for (int32 i = 0; i < tasks->count; i += 1) {
        Task *task = tasks->items[i];

        if (task->action != ACTION_DELETE) {
            has_transfers = true;
            continue;
        }

        if (cecup.stop_working) {
            LOG_ERROR(_("Stop requested.\n"));
            task_list_free(tasks);
            work_batch_flush(&batch);
            work_finalize(false);
            free(thread_data, SIZEOF(*thread_data));
            return NULL;
        }

        work_remove(&batch, task->path, task->path_len, task->side);
    }

    if (!has_transfers) {
        LOG_ERROR(_("No transfers to make.\n"));
        work_batch_flush(&batch);
        work_finalize(false);
        task_list_free(tasks);
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
        Traversal *traversal = &cecup.traversal[task->side];
        int32 write_len;
        HardLinks hardlinks;

        if ((task->action == ACTION_EQUAL) || (task->action == ACTION_IGNORE)) {
            continue;
        }

        if (task->action == ACTION_HARDLINK) {
            if ((hash_lookup_inode_map(traversal->inode_map, &(task->inode), &hardlinks))) {
                for (int32 j = 0; j < hardlinks.count; j += 1) {
                    char *link_name = hardlinks.names[j];
                    write_len = hardlinks.names_lens[j];
                    if (write_len > 1) {
                        if (link_name[write_len - 1] == '/') {
                            write_len -= 1;
                        }
                    }
                    write64(files_from_fd, link_name, write_len);
                    write64(files_from_fd, "\n", 1);
                }
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
    // TODO: Bug. Resource leak. The temporary file created by `mkstemp` is never deleted
    // because this code is commented out. Orphaned `cecup_XXXXXX` files will eventually
    // exhaust the system's /tmp storage space or inodes.
    /* if (!DEBUGGING) { */
    /* xunlink(files_from_filename); */
    /* } */

    work_batch_flush(&batch);
    task_list_free(tasks);
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
#include "assert.c"

int
main(void) {
    char *result;

    /* Test valid file itemization: standard rsync flags + space + path */
    {
        char *line = ">f.st...... some/file.txt";
        result = work_rsync_itemize_skip(line, strlen32(line));
        ASSERT(result != NULL);
        ASSERT_EQUAL(result, line + 12);
        ASSERT(strcmp(result, "some/file.txt") == 0);
    }

    /* Test valid directory itemization */
    {
        char *line = ".d..t...... some/dir/";
        result = work_rsync_itemize_skip(line, strlen32(line));
        ASSERT(result != NULL);
        ASSERT_EQUAL(result, line + 12);
        ASSERT(strcmp(result, "some/dir/") == 0);
    }

    /* Test invalid first character (unknown action) */
    {
        char *line = "?f.st...... some/file.txt";
        result = work_rsync_itemize_skip(line, strlen32(line));
        ASSERT(result == NULL);
    }

    /* Test invalid second character (unknown type) */
    {
        char *line = ">?.st...... some/file.txt";
        result = work_rsync_itemize_skip(line, strlen32(line));
        ASSERT(result == NULL);
    }

    /* Test missing space separator between flags and path */
    {
        char *line = ">f.st......Xsome/file.txt";
        result = work_rsync_itemize_skip(line, strlen32(line));
        ASSERT(result == NULL);
    }

    /* Test line too short to contain flags and separator */
    {
        char *line = ">f.st";
        result = work_rsync_itemize_skip(line, strlen32(line));
        ASSERT(result == NULL);
    }

    exit(EXIT_SUCCESS);
}

#endif

#endif /* WORK_RSYNC_C */
