// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(WORK_RSYNC)
#define WORK_RSYNC

#include <ctype.h>
#include <fts.h>
#include <stdatomic.h>

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
        free2(batch->paths, batch->capacity * SIZEOF(*(batch->paths)));
        free2(batch->paths_lens, batch->capacity * SIZEOF(*(batch->paths_lens)));
        free2(batch, SIZEOF(*batch));
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
        batch = malloc2(SIZEOF(*batch));
        *batch = (MessageBatch){0};

        batch->type = type;
        batch->side = (int8)side;
        batch->capacity = INITIAL_CAPACITY;
        batch->paths = malloc2(batch->capacity * SIZEOF(*(batch->paths)));
        batch->paths_lens = malloc2(batch->capacity * SIZEOF(*(batch->paths_lens)));

        clock_gettime(CLOCK_MONOTONIC_COARSE, &batch->time_last_flush);
        *batch_ptr = batch;
    }

    if (batch->count >= batch->capacity) {
        int32 old_capacity;

        old_capacity = batch->capacity;
        batch->capacity *= 2;
        batch->paths = realloc2(batch->paths,
                                old_capacity, batch->capacity,
                                SIZEOF(*(batch->paths)));
        batch->paths_lens = realloc2(batch->paths_lens,
                                     old_capacity, batch->capacity,
                                     SIZEOF(*(batch->paths_lens)));
    }

    batch->paths[batch->count] = malloc2(path_len + 1);
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
        batch = malloc2(SIZEOF(*batch));
        *batch = (MessageBatch){0};

        batch->type = type;
        batch->side = side;
        batch->capacity = INITIAL_CAPACITY;
        batch->paths = malloc2(batch->capacity * SIZEOF(*(batch->paths)));
        batch->paths_lens = malloc2(batch->capacity * SIZEOF(*(batch->paths_lens)));
        batch->dst_paths = malloc2(batch->capacity * SIZEOF(*(batch->dst_paths)));
        batch->dst_paths_lens = malloc2(batch->capacity * SIZEOF(*(batch->dst_paths_lens)));

        clock_gettime(CLOCK_MONOTONIC_COARSE, &batch->time_last_flush);
        *batch_ptr = batch;
    }

    if (batch->count >= batch->capacity) {
        int32 old_capacity;

        old_capacity = batch->capacity;
        batch->capacity *= 2;
        batch->paths = realloc2(batch->paths,
                                old_capacity, batch->capacity, SIZEOF(*(batch->paths)));
        batch->paths_lens = realloc2(batch->paths_lens,
                                     old_capacity, batch->capacity, SIZEOF(*(batch->paths_lens)));
        batch->dst_paths = realloc2(batch->dst_paths,
                                    old_capacity, batch->capacity, SIZEOF(*(batch->dst_paths)));
        batch->dst_paths_lens = realloc2(batch->dst_paths_lens,
                                         old_capacity, batch->capacity, SIZEOF(*(batch->dst_paths_lens)));
    }

    batch->paths[batch->count] = malloc2(old_len + 1);
    memcpy64(batch->paths[batch->count], old_path, old_len + 1);
    batch->paths_lens[batch->count] = old_len;

    batch->dst_paths[batch->count] = malloc2(new_len + 1);
    memcpy64(batch->dst_paths[batch->count], new_path, new_len + 1);
    batch->dst_paths_lens[batch->count] = new_len;

    batch->count += 1;

    if (batch->count >= batch->capacity) {
        work_batch_flush(batch_ptr);
    }

    return;
}

static bool
work_rsync_action_is_transfer(enum Action action) {
    switch (action) {
    case ACTION_NEW:
    case ACTION_UPDATE:
    case ACTION_HARDLINK:
    case ACTION_SYMLINK:
        return true;
    default:
        return false;
    }
}

static bool
work_rsync_wait_nohang(Command *command, bool *done) {
    int status;
    pid_t pid;
    pid_t result;

    ASSERT(command->result.pid > 0);

    pid = (pid_t)command->result.pid;
    *done = false;

    do {
        result = waitpid(pid, &status, WNOHANG);
    } while ((result < 0) && (errno == EINTR));

    if (result == 0) {
        return true;
    }
    if (result == pid) {
        command_status_from_wait(status, &command->result);
        *done = true;
        return true;
    }

    command_error_set(command, errno);
    error("Error waiting for child: %s.\n", strerror(errno));
    return false;
}

static bool
work_rsync_wait_child(Command *command) {
    bool done;
    bool stopping = false;
    int64 waited_usec = 0;
    int64 timeout_usec = 2*1000*1000;
    int64 step_usec = 100*1000;

    while (true) {
        if (!work_rsync_wait_nohang(command, &done)) {
            return false;
        }
        if (done) {
            return true;
        }

        if (work_should_stop()) {
            if (!stopping) {
                LOG_ERROR(_("Stop requested. Terminating rsync...\n"));
                child_pid_signal(SIGTERM);
                stopping = true;
                waited_usec = 0;
            } else if (waited_usec >= timeout_usec) {
                LOG_ERROR(_("Killing the child process with SIGKILL...\n"));
                child_pid_signal(SIGKILL);
                return command_wait(command);
            }

            waited_usec += step_usec;
        }

        g_usleep((ulong)step_usec);
    }
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
    Command command = {0};
    char buf_error[MAX_PATH_LENGTH*2];
    char buf_output[MAX_PATH_LENGTH*2];
    char dst_base_with_slash[MAX_PATH_LENGTH];
    char src_base_with_slash[MAX_PATH_LENGTH];
    int32 buf_output_pos = 0;
    struct pollfd pipes[2];
    int32 nfiles_checksummed = 0;
    bool delete_after = cecup.delete_after;

    if (checksum) {
        update_progress_info(_("Verifying checksums"),
                             _("Performing verification of the new files..."));
    } else {
        update_progress_info(_("Syncing files"),
                             _("Transferring data and updating metadata..."));
    }

    SNPRINTF(src_base_with_slash, "%s/", cecup.base[L]);
    SNPRINTF(dst_base_with_slash, "%s/", cecup.base[R]);

    COMMAND_PUSH(&command, "rsync", "--verbose", "--verbose");

    if (!delete_after) {
        COMMAND_PUSH(&command, "--update");
    }

    // these 2 options are implied by --files-from
    COMMAND_PUSH(&command, "--dirs", "--relative");

    COMMAND_PUSH(&command,
                 "--partial",
                 "--progress",
                 "--info=progress2",
                 "--links",
                 "--hard-links");
    if (checksum) {
        COMMAND_PUSH(&command, "--checksum");
    }
    COMMAND_PUSH(&command,
                 "--perms",
                 "--times",
                 "--owner",
                 "--group",
                 "--itemize-changes",
                 "--files-from",
                 files_from_filename,
                 "--iconv=.,.");

    COMMAND_PUSH(&command, src_base_with_slash, dst_base_with_slash);
    command_env_push(&command, "LC_ALL=C.UTF-8");

    LOG(_("Running sync...\n"));
    {
        char *cmd;
        int32 cmd_len;

        cmd = command_str(&command, &cmd_len);
        LOG_CMD("%s\n", cmd);
        free2(cmd, cmd_len + 1);
    }

    if (!command_run_async(&command,
                           COMMAND_CAPTURE_STDOUT
                           |COMMAND_CAPTURE_STDERR
                           |COMMAND_NEW_PROCESS_GROUP)) {
        command_free(&command);
        return false;
    }

    child_pid_set((pid_t)command.result.pid);
    pipes[0].fd = command.result.stdout_fd;
    pipes[1].fd = command.result.stderr_fd;
    pipes[0].events = POLLIN;
    pipes[1].events = POLLIN;

    do {
        int64 r;

        pipes[0].revents = 0;
        pipes[1].revents = 0;

        if (work_should_stop()) {
            break;
        }

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
        if (!(pipes[0].revents & POLLIN)) {
            if (pipes[0].revents & POLLHUP) {
                pipes[0].fd = -1;
            }
            goto read_error_pipe;
        }

        r = read64(pipes[0].fd,
                   buf_output + buf_output_pos,
                   SIZEOF(buf_output) - buf_output_pos - 1);

        if (r <= 0) {
            if (r < 0) {
                LOG_ERROR(_("Error reading stdout pipe: %s.\n"), strerror(errno));
                pipes[0].fd = -1;
            }
            if (pipes[0].revents & POLLHUP) {
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
                    error("rsync is outputting too long line.\n");
                    fatal(EXIT_FAILURE);
                }
                break;
            }

            end = *eol;
            line_len = (int32)(eol - buf_output);
            *eol = '\0';

            if (!BEGINS_WITH(buf_output, line_len, RSYNC_DUPLICATE)) {
                LOG("%s%c", buf_output, end);
            }

            if ((path = work_rsync_itemize_skip(buf_output, line_len))) {
                int32 path_len;
                char *sep;

                if (checksum) {
                    nfiles_checksummed += 1;
                    if ((nfiles_total < 1000) || ((nfiles_checksummed % 1000) == 0)) {
                        update_progress_bar((double)nfiles_checksummed / (double)nfiles_total);
                    }
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
                        update_progress_bar((double)progress / 100.0);
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
        if (!(pipes[1].revents & POLLIN)) {
            if (pipes[1].revents & POLLHUP) {
                pipes[1].fd = -1;
            }
            continue;
        }

        r = read64(pipes[1].fd, buf_error, SIZEOF(buf_error) - 1);
        if (r <= 0) {
            if (r < 0) {
                LOG_ERROR(_("Error reading stderr pipe: %s.\n"), strerror(errno));
                pipes[1].fd = -1;
            }
            if (pipes[1].revents & POLLHUP) {
                pipes[1].fd = -1;
            }
            continue;
        }
        buf_error[r] = '\0';
        LOG_ERROR("%s", buf_error);

    } while ((pipes[0].fd >= 0) || (pipes[1].fd >= 0));

    if (!work_rsync_wait_child(&command)) {
        LOG_ERROR(_("Error waiting for child process: %s.\n"),
                  strerror(command.error_status));
        child_pid_set((pid_t)0);
        command_free(&command);
        return false;
    }

    if (command.result.status != 0) {
        if (work_should_stop()) {
            LOG_ERROR(_("Stop requested. Cancelled sync.\n"));
        } else if (command.result.signaled) {
            LOG_ERROR(_("rsync terminated by signal %d.\n"),
                      command.result.term_signal);
        } else if (command.result.exited) {
            LOG_ERROR(_("rsync exited with status %d.\n"),
                      command.result.exit_status);
        } else {
            LOG_ERROR(_("rsync returned status %d.\n"),
                      command.result.status);
        }

        child_pid_set((pid_t)0);
        command_free(&command);
        return false;
    }

    child_pid_set((pid_t)0);
    command_free(&command);
    return true;
}

static void
work_remove(MessageBatch **batch, char *path, int32 path_len, int32 side) {
    char full_path[MAX_PATH_LENGTH];
    int32 base_path_len;

    ASSERT_MORE(path_len, 0);

    if (aux_is_root(path)) {
        LOG_ERROR(_("Refusing to remove configured root path %s.\n"), path);
        return;
    }

    SNPRINTF(full_path, "%s/%s", cecup.base[side], path);
    base_path_len = cecup.base_len[side];

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
        bool had_errors = false;

        if ((fts_handle = fts_open(paths, FTS_PHYSICAL | FTS_NOCHDIR, NULL)) == NULL) {
            error("Error in fts_open(%s): %s.\n", paths[0], strerror(errno));
            return;
        }

        errno = 0;
        while ((entry = fts_read(fts_handle))) {
            char *path_tmp;
            int32 rel_path_len;
            int32 is_dir = false;
            char rel_path[MAX_PATH_LENGTH];

            if (work_should_stop()) {
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
                had_errors = true;
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

            ASSERT_LESS(entry->fts_pathlen, MAX_PATH_LENGTH);
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
                    had_errors = true;
                } else {
                    work_batch_push(batch, MSG_BATCH_ROW_REMOVE, side, rel_path, rel_path_len);
                }
            } else {
                if (unlink(entry->fts_accpath) < 0) {
                    error("Error in unlink(%s): %s.\n", entry->fts_accpath, strerror(errno));
                    had_errors = true;
                } else {
                    work_batch_push(batch, MSG_BATCH_ROW_REMOVE, side, rel_path, rel_path_len);
                }
            }
            errno = 0;
        }

        if (errno) {
            LOG_ERROR(_("Error in fts_read(%s): %s.\n"), full_path, strerror(errno));
            had_errors = true;
        }
        if (fts_close(fts_handle) < 0) {
            LOG_ERROR(_("Error in fts_close: %s.\n"), strerror(errno));
            had_errors = true;
        }

        if (work_should_stop()) {
            LOG_ERROR("Stop requested. Cancelled recursive removal.\n");
        } else if (had_errors) {
            LOG_ERROR(_("Partially removed directory tree %s. "
                        "Some entries could not be removed.\n"), full_path);
        } else {
            LOG("Removed directory tree %s...\n", full_path);
        }
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
    int32 nfiles_total = 0;

    if (tasks == NULL) {
        if ((cecup.ntransfers <= 0) && (cecup.ndeletions <= 0)) {
            LOG_ERROR(_("There are no operations to make.\n"));
            work_finalize(thread_data, false);
        } else {
            has_transfers = true;
            nfiles_total = cecup.ntransfers;
        }
        tasks = malloc2(sizeof(*tasks));
        *tasks = (TaskList){0};
    }

    for (int32 i = 0; (tasks->count == 0) && (i < cecup.ndeletions); i += 1) {
        if (work_should_stop()) {
            LOG_ERROR(_("Stop requested.\n"));
            task_list_free(tasks);
            work_batch_flush(&batch);
            work_finalize(thread_data, false);
        }

        work_remove(&batch, cecup.deletions[i], cecup.deletions_lens[i], R);
    }

    for (int32 i = 0; i < tasks->count; i += 1) {
        Task *task = tasks->items[i];

        if (work_rsync_action_is_transfer(task->action)) {
            has_transfers = true;
            nfiles_total += 1;
            continue;
        }

        if (task->action != ACTION_DELETE) {
            continue;
        }

        if (work_should_stop()) {
            LOG_ERROR(_("Stop requested.\n"));
            task_list_free(tasks);
            work_batch_flush(&batch);
            work_finalize(thread_data, false);
        }

        work_remove(&batch, task->path, task->path_len, task->side);
    }

    if (!has_transfers) {
        LOG_ERROR(_("No transfers to make.\n"));
        work_batch_flush(&batch);
        task_list_free(tasks);
        work_finalize(thread_data, false);
    }

    if ((files_from_fd = mkstemp(files_from_filename)) < 0) {
        error("Error in mkstemp: %s.\n", strerror(errno));
        fatal(EXIT_FAILURE);
    }

    for (int32 i = 0; (tasks->count == 0) && (i < cecup.ntransfers); i += 1) {
        char *file = cecup.transfers[i];
        int64 left = cecup.transfers_lens[i];
        int64 written = 0;

        if (left > 1) {
            // rsync interprets slashes at the end of dir names differently
            if (file[left - 1] == '/') {
                left -= 1;
            }
        }

        write_all(files_from_fd, &file[written], left);
        write_all(files_from_fd, "\n", 1);
    }

    for (int32 i = 0; i < tasks->count; i += 1) {
        Task *task = tasks->items[i];
        Traversal *traversal = &cecup.traversal[task->side];
        int32 write_len;
        HardLinks hardlinks;

        if (!work_rsync_action_is_transfer(task->action)) {
            continue;
        }

        if (task->action == ACTION_HARDLINK) {
            if ((hash_lookup_inode_map(traversal->inode_map, &task->file_id, &hardlinks))) {
                for (int32 j = 0; j < hardlinks.count; j += 1) {
                    char *link_name = hardlinks.names[j];
                    
                    if ((write_len = hardlinks.names_lens[j]) > 1) {
                        // rsync interprets slashes at the end of dir names differently
                        if (link_name[write_len - 1] == '/') {
                            write_len -= 1;
                        }
                    }

                    write_all(files_from_fd, link_name, write_len);
                    write_all(files_from_fd, "\n", 1);
                }
            }
        }

        if ((write_len = task->path_len) > 1) {
            if (task->path[write_len - 1] == '/') {
                write_len -= 1;
            }
        }
        write_all(files_from_fd, task->path, write_len);
        write_all(files_from_fd, "\n", 1);
    }

    XCLOSE(&files_from_fd);

    do {
        if (work_rsync_run(files_from_filename, nfiles_total, false, &batch)) {
            if (work_should_stop()) {
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
    task_list_free(tasks);
    work_finalize(thread_data, false);
    pthread_exit(NULL);
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
#define CBASE_IMPLEMENT
#include "cbase.h"

#include "work.c"
#include "tasks.c"

int
main(void) {
    char *result;
    MessageBatch *batch;
    int32 fd;
    pthread_t thread;

    if (!gtk_init_check()) {
        exit(EXIT_SUCCESS);
    }

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

    /* Initialize System State for IO Tests */
    system("rm -rf /tmp/cecup_test_src /tmp/cecup_test_dst /tmp/cecup_test_files_from");
    mkdir("/tmp/cecup_test_src", 0755);
    mkdir("/tmp/cecup_test_dst", 0755);

    cecup.base[L] = xstrdup("/tmp/cecup_test_src");
    cecup.base_len[L] = strlen32(cecup.base[L]);
    cecup.base[R] = xstrdup("/tmp/cecup_test_dst");
    cecup.base_len[R] = strlen32(cecup.base[R]);
    cecup.delete_after = false;
    stop_working(false);
    child_pid_set((pid_t)0);
    cecup.ntransfers = 0;
    cecup.ndeletions = 0;

    /* Test transfer-action classification */
    ASSERT(work_rsync_action_is_transfer(ACTION_NEW));
    ASSERT(work_rsync_action_is_transfer(ACTION_UPDATE));
    ASSERT(work_rsync_action_is_transfer(ACTION_HARDLINK));
    ASSERT(work_rsync_action_is_transfer(ACTION_SYMLINK));
    ASSERT(!work_rsync_action_is_transfer(ACTION_EQUAL));
    ASSERT(!work_rsync_action_is_transfer(ACTION_DELETED));
    ASSERT(!work_rsync_action_is_transfer(ACTION_DELETE));
    ASSERT(!work_rsync_action_is_transfer(ACTION_IGNORE));

    /* Test work_batch_push and work_batch_flush */
    batch = NULL;
    work_batch_push(&batch, MSG_BATCH_ROW_TRANSFER, L, "file.txt", 8);
    ASSERT(batch != NULL);
    ASSERT_EQUAL(batch->count, 1);
    ASSERT(strcmp(batch->paths[0], "file.txt") == 0);

    /* Push different type to force a flush */
    work_batch_push(&batch, MSG_BATCH_ROW_REMOVE, L, "other.txt", 9);
    ASSERT(batch != NULL);
    ASSERT_EQUAL(batch->count, 1);
    ASSERT_EQUAL((uint32)batch->type, (uint32)MSG_BATCH_ROW_REMOVE);
    ASSERT(strcmp(batch->paths[0], "other.txt") == 0);

    work_batch_flush(&batch);
    ASSERT(batch == NULL);

    /* Test work_batch_push_rename */
    work_batch_push_rename(&batch, MSG_BATCH_ROW_RENAME, L, "old.txt", 7, "new.txt", 7);
    ASSERT(batch != NULL);
    ASSERT_EQUAL(batch->count, 1);
    ASSERT(strcmp(batch->paths[0], "old.txt") == 0);
    ASSERT(strcmp(batch->dst_paths[0], "new.txt") == 0);
    work_batch_flush(&batch);
    ASSERT(batch == NULL);

    /* Test work_remove refuses to remove configured root */
    fd = open("/tmp/cecup_test_dst/root_guard.txt", O_CREAT | O_WRONLY, 0644);
    close(fd);
    ASSERT(access("/tmp/cecup_test_dst/root_guard.txt", F_OK) == 0);
    work_remove(&batch, ".", 1, R);
    ASSERT(access("/tmp/cecup_test_dst/root_guard.txt", F_OK) == 0);
    ASSERT(batch == NULL);
    work_remove(&batch, "./", 2, R);
    ASSERT(access("/tmp/cecup_test_dst/root_guard.txt", F_OK) == 0);
    ASSERT(access("/tmp/cecup_test_dst", F_OK) == 0);
    ASSERT(batch == NULL);
    unlink("/tmp/cecup_test_dst/root_guard.txt");

    /* Test work_remove on file */
    fd = open("/tmp/cecup_test_dst/rm_test.txt", O_CREAT | O_WRONLY, 0644);
    close(fd);
    ASSERT(access("/tmp/cecup_test_dst/rm_test.txt", F_OK) == 0);
    work_remove(&batch, "rm_test.txt", 11, R);
    ASSERT(access("/tmp/cecup_test_dst/rm_test.txt", F_OK) != 0);

    /* Test work_remove on directory using FTS */
    mkdir("/tmp/cecup_test_dst/rm_dir/", 0755);
    fd = open("/tmp/cecup_test_dst/rm_dir/file.txt", O_CREAT | O_WRONLY, 0644);
    close(fd);
    work_remove(&batch, "rm_dir/", 7, R);
    ASSERT(access("/tmp/cecup_test_dst/rm_dir", F_OK) != 0);

    /* Test work_rsync_run */
    fd = open("/tmp/cecup_test_src/sync_test.txt", O_CREAT | O_WRONLY, 0644);
    write64(fd, "data", 4);
    close(fd);

    fd = open("/tmp/cecup_test_files_from", O_CREAT | O_WRONLY, 0644);
    write64(fd, "sync_test.txt\n", 14);
    close(fd);

    ASSERT(work_rsync_run("/tmp/cecup_test_files_from", 1, false, &batch));
    ASSERT(access("/tmp/cecup_test_dst/sync_test.txt", F_OK) == 0);

    /* Test work_rsync (Thread Runner) */
    {
        ThreadData *thread_data;
        TaskList *task_list;
        Task *task;

        thread_data = malloc2(SIZEOF(*thread_data));
        task_list = malloc2(SIZEOF(*task_list) + 1*SIZEOF(Task*));
        task = malloc2(SIZEOF(*task));

        *thread_data = (ThreadData){0};
        *task_list = (TaskList){0};
        *task = (Task){0};

        task->action = ACTION_UPDATE;
        task->side = R;
        task->path = xstrdup("sync_test.txt");
        task->path_len = 13;

        task_list->count = 1;
        task_list->items[0] = task;
        thread_data->tasks = task_list;

        xpthread_create(&thread, NULL, work_rsync, thread_data);
        xpthread_join(&thread, NULL);
    }

    /* Teardown */
    system("rm -rf /tmp/cecup_test_src /tmp/cecup_test_dst /tmp/cecup_test_files_from");
    free2(cecup.base[L], cecup.base_len[L] + 1);
    free2(cecup.base[R], cecup.base_len[R] + 1);

    exit(EXIT_SUCCESS);
}

#endif

#endif /* WORK_RSYNC_C */
