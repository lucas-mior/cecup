// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(WORK_RSYNC)
#define WORK_RSYNC

#include "cbase.h"
#include "cecup.h"
#include "update.c"
#include "traversal.c"

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

        time_monotonic_coarse(&batch->time_last_flush);
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

        time_monotonic_coarse(&time_this_push);
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

        time_monotonic_coarse(&batch->time_last_flush);
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
work_transfer_action_is_transfer(enum Action action) {
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
work_rsync_action_is_transfer(enum Action action) {
    return work_transfer_action_is_transfer(action);
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

    COMMAND_PUSH(&command, "--partial", "--progress", "--info=progress2");
    COMMAND_PUSH(&command, "--links", "--hard-links");
    if (checksum) {
        COMMAND_PUSH(&command, "--checksum");
    }
    COMMAND_PUSH(&command, "--perms", "--times", "--owner", "--group");
    COMMAND_PUSH(&command, "--itemize-changes");
    COMMAND_PUSH(&command, "--files-from", files_from_filename);
    COMMAND_PUSH(&command, "--iconv=.,.");

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
        FsWalk fs_walk;
        FsWalkEntry *entry;
        bool had_errors = false;

        if (!fs_walk_open(&fs_walk, full_path)) {
            error("Error in fs_walk_open(%s): %s.\n",
                  full_path, strerror(errno));
            return;
        }

        errno = 0;
        while ((entry = fs_walk_read(&fs_walk))) {
            char *path_tmp;
            int32 rel_path_len;
            int32 is_dir = false;
            char rel_path[MAX_PATH_LENGTH];

            if (work_should_stop()) {
                break;
            }

            switch (entry->info) {
            case FS_WALK_FILE:
            case FS_WALK_SYMLINK:
            case FS_WALK_SYMLINK_BROKEN:
            case FS_WALK_DEFAULT:
                break;
            case FS_WALK_POST_DIR:
                is_dir = true;
                break;
            case FS_WALK_DIR_UNREADABLE:
            case FS_WALK_ERROR:
            case FS_WALK_STAT_ERROR:
                error("FsWalk error on %s: %s.\n",
                      entry->path, strerror(entry->error));
                had_errors = true;
                continue;
            default:
                continue;
            }

            path_tmp = entry->path + base_path_len;
            rel_path_len = entry->path_len - base_path_len;

            if (path_tmp[0] == '/') {
                path_tmp += 1;
                rel_path_len -= 1;
            }

            if (rel_path_len == 0) {
                path_tmp = ".";
                rel_path_len = 1;
            }

            ASSERT_LESS(entry->path_len, MAX_PATH_LENGTH);
            memcpy64(rel_path, path_tmp, rel_path_len + 1);
            normalize(rel_path, &rel_path_len);

            if (is_dir && (rel_path[rel_path_len - 1] != '/')) {
                rel_path_len += 1;
                rel_path[rel_path_len - 1] = '/';
                rel_path[rel_path_len] = '\0';
            }

            if (is_dir) {
                if (rmdir(entry->access_path) < 0) {
                    error("Error in rmdir(%s): %s.\n",
                          entry->access_path, strerror(errno));
                    had_errors = true;
                } else {
                    work_batch_push(batch, MSG_BATCH_ROW_REMOVE,
                                    side, rel_path, rel_path_len);
                }
            } else {
                if (unlink(entry->access_path) < 0) {
                    error("Error in unlink(%s): %s.\n",
                          entry->access_path, strerror(errno));
                    had_errors = true;
                } else {
                    work_batch_push(batch, MSG_BATCH_ROW_REMOVE,
                                    side, rel_path, rel_path_len);
                }
            }
            errno = 0;
        }

        if (errno) {
            LOG_ERROR(_("Error in fs_walk_read(%s): %s.\n"),
                      full_path, strerror(errno));
            had_errors = true;
        }
        if (fs_walk_close(&fs_walk) < 0) {
            LOG_ERROR(_("Error in fs_walk_close: %s.\n"), strerror(errno));
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

typedef struct ManualHardLink {
    FileID file_id;
    char *dst_path;
    int32 dst_path_len;
} ManualHardLink;

typedef struct ManualTransferState {
    ManualHardLink *hardlinks;
    MessageBatch **batch;

    int32 hardlinks_count;
    int32 hardlinks_capacity;
    int32 nfiles_done;
    int32 nfiles_total;

    bool had_errors;
} ManualTransferState;

static char *
work_transfer_backend_name(enum TransferBackend backend) {
    switch (backend) {
    case TRANSFER_BACKEND_RSYNC:
        return "rsync";
    case TRANSFER_BACKEND_MANUAL:
        return "manual";
    default:
        return "unknown";
    }
}

static enum TransferBackend
work_transfer_backend_default(void) {
    return transfer_backend_platform_default();
}

static enum TransferBackend
work_transfer_backend_current(void) {
    enum TransferBackend selected_backend;
    char *backend;

    backend = getenv("CECUP_TRANSFER_BACKEND");
    if (transfer_backend_parse(backend, &selected_backend)) {
        return selected_backend;
    }

    selected_backend = work_transfer_backend_default();
    if (backend != NULL) {
        LOG_ERROR(_("Unknown transfer backend '%s'. Using %s instead.\n"),
                  backend, work_transfer_backend_name(selected_backend));
    }

    return selected_backend;
}

static void
work_transfer_log_metadata_policy(enum TransferBackend backend) {
    TransferMetadataPolicy policy;

    policy = transfer_metadata_policy_for_backend(backend);

    if (policy.preserve_mtime && policy.preserve_perm
        && policy.preserve_symlink && policy.preserve_hardlink) {
        LOG(_("Metadata policy: preserving file contents, mtimes, "
              "permissions, symlinks, and hardlinks.\n"));
    }

    if (policy.preserve_owner && policy.preserve_group) {
        LOG(_("Metadata policy: preserving owner and group.\n"));
    } else {
        LOG(_("Metadata policy: owner, group, and directory ctime "
              "differences are ignored by this backend.\n"));
    }

    if (!policy.preserve_extended) {
        LOG(_("Metadata policy: ACLs, extended attributes, Finder "
              "metadata, and resource forks are not preserved.\n"));
    }

    if (!policy.preserve_special) {
        LOG(_("Metadata policy: special files are not copied by this "
              "backend.\n"));
    }

    return;
}

static void
work_transfer_trim_rsync_path(char *path, int64 *path_len) {
    if (*path_len > 1) {
        // rsync interprets slashes at the end of dir names differently
        if (path[*path_len - 1] == '/') {
            *path_len -= 1;
        }
    }

    return;
}

static bool
work_rsync_write_path(int32 fd, char *path, int32 path_len) {
    int64 write_len;

    write_len = path_len;
    work_transfer_trim_rsync_path(path, &write_len);

    write_all(fd, path, write_len);
    write_all(fd, "\n", 1);
    return true;
}

static bool
work_rsync_backend_run(
    TaskList *tasks,
    int32 nfiles_total,
    MessageBatch **batch
) {
    char files_from_filename[] = "/tmp/cecup_XXXXXX";
    int files_from_fd;
    bool success = false;

    if ((files_from_fd = mkstemp(files_from_filename)) < 0) {
        error("Error in mkstemp: %s.\n", strerror(errno));
        fatal(EXIT_FAILURE);
    }

    for (int32 i = 0; (tasks->count == 0) && (i < cecup.ntransfers); i += 1) {
        work_rsync_write_path(files_from_fd,
                              cecup.transfers[i], cecup.transfers_lens[i]);
    }

    for (int32 i = 0; i < tasks->count; i += 1) {
        Task *task = tasks->items[i];
        Traversal *traversal = &cecup.traversal[task->side];
        HardLinks hardlinks;

        if (!work_transfer_action_is_transfer(task->action)) {
            continue;
        }

        if (task->action == ACTION_HARDLINK) {
            if ((hash_lookup_inode_map(traversal->inode_map,
                                       &task->file_id, &hardlinks))) {
                for (int32 j = 0; j < hardlinks.count; j += 1) {
                    work_rsync_write_path(files_from_fd,
                                          hardlinks.names[j],
                                          hardlinks.names_lens[j]);
                }
            }
        }

        work_rsync_write_path(files_from_fd, task->path, task->path_len);
    }

    XCLOSE(&files_from_fd);

    do {
        if (work_rsync_run(files_from_filename, nfiles_total, false, batch)) {
            if (work_should_stop()) {
                LOG_ERROR(_("Stop requested.\n"));
                break;
            }
            success = work_rsync_run(files_from_filename,
                                     nfiles_total, true, batch);
        }
    } while (0);

    if (!DEBUGGING) {
        xunlink(files_from_filename);
    }

    return success;
}

static bool
work_transfer_full_path(
    char *full_path,
    int32 side,
    char *path,
    int32 path_len
) {
    if (path_len <= 0) {
        LOG_ERROR(_("Refusing empty transfer path.\n"));
        return false;
    }

    if (aux_is_root(path) || ((path_len == 1) && (path[0] == '.'))) {
        SNPRINTF(full_path, "%s", cecup.base[side]);
    } else {
        SNPRINTF(full_path, "%s/%s", cecup.base[side], path);
    }

    return true;
}

static bool
work_manual_make_parent_dirs(char *path) {
    int32 base_len = cecup.base_len[R];

    for (int32 i = base_len + 1; path[i] != '\0'; i += 1) {
        if (path[i] != '/') {
            continue;
        }

        path[i] = '\0';
        if ((mkdir(path, 0777) < 0) && (errno != EEXIST)) {
            LOG_ERROR(_("Error creating parent directory %s: %s.\n"),
                      path, strerror(errno));
            path[i] = '/';
            return false;
        }
        path[i] = '/';
    }

    return true;
}

static bool
work_manual_remove_destination(
    ManualTransferState *state,
    char *path,
    int32 path_len,
    char *dst_path
) {
    struct stat dst_stat;

    if (lstat(dst_path, &dst_stat) < 0) {
        if (errno == ENOENT) {
            return true;
        }
        LOG_ERROR(_("Error checking destination %s: %s.\n"),
                  dst_path, strerror(errno));
        state->had_errors = true;
        return false;
    }

    work_remove(state->batch, path, path_len, R);
    return !work_should_stop();
}

static bool
work_manual_remove_type_conflict(
    ManualTransferState *state,
    char *path,
    int32 path_len,
    char *dst_path,
    struct stat *src_stat
) {
    struct stat dst_stat;

    if (lstat(dst_path, &dst_stat) < 0) {
        if (errno == ENOENT) {
            return true;
        }
        LOG_ERROR(_("Error checking destination %s: %s.\n"),
                  dst_path, strerror(errno));
        state->had_errors = true;
        return false;
    }

    if ((dst_stat.st_mode & S_IFMT) == (src_stat->st_mode & S_IFMT)) {
        return true;
    }

    LOG(_("Removing destination type conflict %s...\n"), dst_path);
    work_remove(state->batch, path, path_len, R);
    return !work_should_stop();
}

static void
work_manual_preserve_metadata(char *dst_path, struct stat *src_stat) {
    struct utimbuf times;

    if (chmod(dst_path, src_stat->st_mode & 07777) < 0) {
        LOG_ERROR(_("Error setting permissions on %s: %s.\n"),
                  dst_path, strerror(errno));
    }

    times.actime = src_stat->st_atime;
    times.modtime = src_stat->st_mtime;
    if (utime(dst_path, &times) < 0) {
        LOG_ERROR(_("Error setting timestamps on %s: %s.\n"),
                  dst_path, strerror(errno));
    }

    return;
}

static int32
work_manual_hardlink_find(ManualTransferState *state, FileID *file_id) {
    for (int32 i = 0; i < state->hardlinks_count; i += 1) {
        ManualHardLink *hardlink = &state->hardlinks[i];

        if ((hardlink->file_id.device == file_id->device)
            && (hardlink->file_id.inode == file_id->inode)) {
            return i;
        }
    }

    return -1;
}

static void
work_manual_hardlink_remember(
    ManualTransferState *state,
    FileID *file_id,
    char *dst_path
) {
    ManualHardLink *hardlink;
    int32 dst_path_len;

    if (work_manual_hardlink_find(state, file_id) >= 0) {
        return;
    }

    if (state->hardlinks_count >= state->hardlinks_capacity) {
        int32 old_capacity;

        old_capacity = state->hardlinks_capacity;
        if (state->hardlinks_capacity == 0) {
            state->hardlinks_capacity = INITIAL_CAPACITY;
        } else {
            state->hardlinks_capacity *= 2;
        }
        state->hardlinks = realloc2(state->hardlinks,
                                    old_capacity, state->hardlinks_capacity,
                                    SIZEOF(*(state->hardlinks)));
    }

    hardlink = &state->hardlinks[state->hardlinks_count];
    *hardlink = (ManualHardLink){0};

    dst_path_len = strlen32(dst_path);
    hardlink->file_id = *file_id;
    hardlink->dst_path = malloc2(dst_path_len + 1);
    memcpy64(hardlink->dst_path, dst_path, dst_path_len + 1);
    hardlink->dst_path_len = dst_path_len;

    state->hardlinks_count += 1;
    return;
}

static void
work_manual_hardlinks_free(ManualTransferState *state) {
    for (int32 i = 0; i < state->hardlinks_count; i += 1) {
        ManualHardLink *hardlink = &state->hardlinks[i];

        free2(hardlink->dst_path, hardlink->dst_path_len + 1);
    }

    free2(state->hardlinks,
          state->hardlinks_capacity * SIZEOF(*(state->hardlinks)));
    return;
}

static void
work_manual_item_done(
    ManualTransferState *state,
    char *path,
    int32 path_len
) {
    state->nfiles_done += 1;
    work_batch_push(state->batch, MSG_BATCH_ROW_TRANSFER, R, path, path_len);

    if (state->nfiles_total > 0) {
        update_progress_bar((double)state->nfiles_done
                            / (double)state->nfiles_total);
    }

    return;
}

static bool
work_manual_copy_regular_contents(char *dst_path, char *src_path, mode_t mode) {
    char buffer[BUFSIZ];
    int32 src_fd;
    int32 dst_fd;
    int64 bytes_read;

    src_fd = open(src_path, O_RDONLY);
    if (src_fd < 0) {
        LOG_ERROR(_("Error opening %s for reading: %s.\n"),
                  src_path, strerror(errno));
        return false;
    }

    dst_fd = open(dst_path, O_WRONLY | O_CREAT | O_TRUNC, mode & 07777);
    if (dst_fd < 0) {
        LOG_ERROR(_("Error opening %s for writing: %s.\n"),
                  dst_path, strerror(errno));
        XCLOSE(&src_fd, src_path);
        return false;
    }

    while ((bytes_read = read64(src_fd, buffer, SIZEOF(buffer))) > 0) {
        int64 bytes_written;

        bytes_written = write64(dst_fd, buffer, bytes_read);
        if (bytes_written != bytes_read) {
            LOG_ERROR(_("Error writing %s: %s.\n"),
                      dst_path, strerror(errno));
            XCLOSE(&src_fd, src_path);
            XCLOSE(&dst_fd, dst_path);
            return false;
        }
    }

    if (bytes_read < 0) {
        LOG_ERROR(_("Error reading %s: %s.\n"), src_path, strerror(errno));
        XCLOSE(&src_fd, src_path);
        XCLOSE(&dst_fd, dst_path);
        return false;
    }

    XCLOSE(&src_fd, src_path);
    XCLOSE(&dst_fd, dst_path);
    return true;
}

static bool
work_manual_copy_regular(
    ManualTransferState *state,
    char *src_path,
    char *dst_path,
    char *path,
    int32 path_len,
    struct stat *src_stat
) {
    FileID file_id;
    int32 hardlink_idx;

    if (!work_manual_make_parent_dirs(dst_path)) {
        state->had_errors = true;
        return false;
    }
    if (!work_manual_remove_type_conflict(state, path, path_len,
                                          dst_path, src_stat)) {
        state->had_errors = true;
        return false;
    }

    file_id = file_id_from_stat(src_stat);
    hardlink_idx = -1;
    if (src_stat->st_nlink > 1) {
        hardlink_idx = work_manual_hardlink_find(state, &file_id);
    }

    if (hardlink_idx >= 0) {
        ManualHardLink *hardlink = &state->hardlinks[hardlink_idx];

        if (STREQUAL(hardlink->dst_path, hardlink->dst_path_len,
                     dst_path, strlen32(dst_path))) {
            return true;
        }

        if (!work_manual_remove_destination(state, path, path_len, dst_path)) {
            state->had_errors = true;
            return false;
        }
        if (link(hardlink->dst_path, dst_path) == 0) {
            LOG(_("Created hardlink %s -> %s.\n"),
                dst_path, hardlink->dst_path);
            work_manual_item_done(state, path, path_len);
            return true;
        }

        LOG_ERROR(_("Error creating hardlink %s -> %s: %s. Copying instead.\n"),
                  dst_path, hardlink->dst_path, strerror(errno));
    }

    if (!work_manual_copy_regular_contents(dst_path, src_path,
                                           src_stat->st_mode)) {
        state->had_errors = true;
        return false;
    }

    work_manual_preserve_metadata(dst_path, src_stat);
    if (src_stat->st_nlink > 1) {
        work_manual_hardlink_remember(state, &file_id, dst_path);
    }

    LOG(_("Copied %s -> %s.\n"), src_path, dst_path);
    work_manual_item_done(state, path, path_len);
    return true;
}

static bool
work_manual_copy_symlink(
    ManualTransferState *state,
    char *src_path,
    char *dst_path,
    char *path,
    int32 path_len
) {
    char target[MAX_PATH_LENGTH];
    int64 target_len;

    target_len = readlink(src_path, target, SIZEOF(target) - 1);
    if (target_len < 0) {
        LOG_ERROR(_("Error reading symlink %s: %s.\n"),
                  src_path, strerror(errno));
        state->had_errors = true;
        return false;
    }
    target[target_len] = '\0';

    if (!work_manual_make_parent_dirs(dst_path)) {
        state->had_errors = true;
        return false;
    }
    if (!work_manual_remove_destination(state, path, path_len, dst_path)) {
        state->had_errors = true;
        return false;
    }

    if (symlink(target, dst_path) < 0) {
        LOG_ERROR(_("Error creating symlink %s -> %s: %s.\n"),
                  dst_path, target, strerror(errno));
        state->had_errors = true;
        return false;
    }

    LOG(_("Created symlink %s -> %s.\n"), dst_path, target);
    work_manual_item_done(state, path, path_len);
    return true;
}

static bool
work_manual_copy_dir(
    ManualTransferState *state,
    char *dst_path,
    char *path,
    int32 path_len,
    struct stat *src_stat
) {
    if (!work_manual_make_parent_dirs(dst_path)) {
        state->had_errors = true;
        return false;
    }
    if (!work_manual_remove_type_conflict(state, path, path_len,
                                          dst_path, src_stat)) {
        state->had_errors = true;
        return false;
    }

    if ((mkdir(dst_path, src_stat->st_mode & 07777) < 0) && (errno != EEXIST)) {
        LOG_ERROR(_("Error creating directory %s: %s.\n"),
                  dst_path, strerror(errno));
        state->had_errors = true;
        return false;
    }

    work_manual_preserve_metadata(dst_path, src_stat);
    LOG(_("Created directory %s.\n"), dst_path);
    work_manual_item_done(state, path, path_len);
    return true;
}

static bool
work_manual_copy_path(
    ManualTransferState *state,
    char *path,
    int32 path_len
) {
    char src_path[MAX_PATH_LENGTH];
    char dst_path[MAX_PATH_LENGTH];
    struct stat src_stat;

    if (!work_transfer_full_path(src_path, L, path, path_len)) {
        state->had_errors = true;
        return false;
    }
    if (!work_transfer_full_path(dst_path, R, path, path_len)) {
        state->had_errors = true;
        return false;
    }

    if (lstat(src_path, &src_stat) < 0) {
        LOG_ERROR(_("Error checking source %s: %s.\n"),
                  src_path, strerror(errno));
        state->had_errors = true;
        return false;
    }

    if (S_ISDIR(src_stat.st_mode)) {
        return work_manual_copy_dir(state, dst_path,
                                    path, path_len, &src_stat);
    }
    if (S_ISLNK(src_stat.st_mode)) {
        return work_manual_copy_symlink(state, src_path, dst_path,
                                        path, path_len);
    }
    if (S_ISREG(src_stat.st_mode)) {
        return work_manual_copy_regular(state, src_path, dst_path,
                                        path, path_len, &src_stat);
    }

    LOG_ERROR(_("Manual copier does not support special file %s.\n"),
              src_path);
    state->had_errors = true;
    return false;
}

static void
work_manual_copy_hardlink_task(ManualTransferState *state, Task *task) {
    Traversal *traversal = &cecup.traversal[task->side];
    HardLinks hardlinks;

    if ((hash_lookup_inode_map(traversal->inode_map,
                               &task->file_id, &hardlinks))) {
        for (int32 j = 0; j < hardlinks.count; j += 1) {
            if (work_should_stop()) {
                return;
            }
            work_manual_copy_path(state,
                                  hardlinks.names[j],
                                  hardlinks.names_lens[j]);
        }
    }

    return;
}

static bool
work_manual_backend_run(
    TaskList *tasks,
    int32 nfiles_total,
    MessageBatch **batch
) {
    ManualTransferState state = {0};

    state.batch = batch;
    state.nfiles_total = nfiles_total;

    update_progress_info(
        _("Copying files"),
        _("Copying files with the manual recursive copier...")
    );
    LOG(_("Running manual transfer backend...\n"));

    for (int32 i = 0; (tasks->count == 0) && (i < cecup.ntransfers); i += 1) {
        if (work_should_stop()) {
            break;
        }
        work_manual_copy_path(&state,
                              cecup.transfers[i], cecup.transfers_lens[i]);
    }

    for (int32 i = 0; i < tasks->count; i += 1) {
        Task *task = tasks->items[i];

        if (work_should_stop()) {
            break;
        }
        if (!work_transfer_action_is_transfer(task->action)) {
            continue;
        }

        if (task->action == ACTION_HARDLINK) {
            work_manual_copy_hardlink_task(&state, task);
        }
        work_manual_copy_path(&state, task->path, task->path_len);
    }

    work_manual_hardlinks_free(&state);

    if (work_should_stop()) {
        LOG_ERROR(_("Stop requested. Cancelled manual transfer.\n"));
        return false;
    }
    if (state.had_errors) {
        LOG_ERROR(_("Manual transfer finished with errors.\n"));
        return false;
    }

    return true;
}

static void *
work_transfer(void *user_data) {
    ThreadData *thread_data = user_data;
    TaskList *tasks = thread_data->tasks;
    enum TransferBackend backend;
    MessageBatch *batch = NULL;
    bool has_transfers = false;
    bool success = true;
    int32 nfiles_total = 0;

    if (tasks == NULL) {
        if ((cecup.ntransfers <= 0) && (cecup.ndeletions <= 0)) {
            LOG_ERROR(_("There are no operations to make.\n"));
            work_finalize(thread_data, false);
        } else {
            has_transfers = cecup.ntransfers > 0;
            nfiles_total = cecup.ntransfers;
        }
        tasks = malloc2(SIZEOF(*tasks));
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

        if (work_transfer_action_is_transfer(task->action)) {
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

    backend = work_transfer_backend_current();
    LOG(_("Using %s transfer backend.\n"), work_transfer_backend_name(backend));
    work_transfer_log_metadata_policy(backend);

    switch (backend) {
    case TRANSFER_BACKEND_RSYNC:
        success = work_rsync_backend_run(tasks, nfiles_total, &batch);
        break;
    case TRANSFER_BACKEND_MANUAL:
        success = work_manual_backend_run(tasks, nfiles_total, &batch);
        break;
    default:
        LOG_ERROR(_("Invalid transfer backend.\n"));
        success = false;
        break;
    }

    if (!success && !work_should_stop()) {
        LOG_ERROR(_("Transfer backend failed.\n"));
    }

    work_batch_flush(&batch);
    task_list_free(tasks);
    work_finalize(thread_data, false);
    pthread_exit(NULL);
}

static void *
work_rsync(void *user_data) {
    return work_transfer(user_data);
}

#if 0 == TESTING_work_rsync
static inline void
work_rsync_functions_sink(void) {
    (void)work_rsync_functions_sink;
    (void)work_rsync;
    (void)work_rsync_run;
    (void)work_rsync_action_is_transfer;
    (void)work_transfer;
}
#endif

#if TESTING_work_rsync
#define CBASE_IMPLEMENT
#include "cbase.h"

#include "work.c"
#include "tasks.c"

static bool
test_rsync_backend_supported(void) {
    Command command = {0};
    bool supported;

    if (!test_command_exists("rsync")) {
        return false;
    }

    COMMAND_PUSH(&command,
                 "rsync", "--info=progress2", "--iconv=.,.", "--version");
    supported = command_run_capture(&command, COMMAND_CAPTURE_STDOUT)
                && (command.result.status == 0);
    command_free(&command);
    return supported;
}

static TaskList *
test_task_list_create(int32 count) {
    TaskList *tasks;

    tasks = malloc2(SIZEOF(*tasks) + count*SIZEOF(Task *));
    *tasks = (TaskList){0};
    tasks->count = count;
    return tasks;
}

static Task *
test_task_create(enum Action action, char *path) {
    Task *task;

    task = malloc2(SIZEOF(*task));
    *task = (Task){0};
    task->action = action;
    task->side = L;
    task->path_len = strlen32(path);
    task->path = xstrdup(path);
    return task;
}

static void
test_write_file(char *path, char *contents) {
    int32 fd;
    int32 len;

    len = strlen32(contents);
    fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    ASSERT_MORE_EQUAL(fd, 0);
    write64(fd, contents, len);
    XCLOSE(&fd, path);
    return;
}

static void
test_manual_copy_regular_and_dir(MessageBatch **batch) {
    TaskList *tasks;
    char src_file[MAX_PATH_LENGTH];
    char dst_file[MAX_PATH_LENGTH];
    char src_dir[MAX_PATH_LENGTH];
    char src_nested[MAX_PATH_LENGTH];
    char dst_dir[MAX_PATH_LENGTH];
    char dst_nested[MAX_PATH_LENGTH];

    SNPRINTF(src_file, "%s/manual_file.txt", cecup.base[L]);
    SNPRINTF(dst_file, "%s/manual_file.txt", cecup.base[R]);
    SNPRINTF(src_dir, "%s/manual_dir", cecup.base[L]);
    SNPRINTF(src_nested, "%s/manual_dir/nested.txt", cecup.base[L]);
    SNPRINTF(dst_dir, "%s/manual_dir", cecup.base[R]);
    SNPRINTF(dst_nested, "%s/manual_dir/nested.txt", cecup.base[R]);

    test_write_file(src_file, "file-data");
    mkdir(src_dir, 0755);
    test_write_file(src_nested, "nested-data");

    tasks = test_task_list_create(3);
    tasks->items[0] = test_task_create(ACTION_NEW, "manual_file.txt");
    tasks->items[1] = test_task_create(ACTION_NEW, "manual_dir/");
    tasks->items[2] = test_task_create(ACTION_NEW, "manual_dir/nested.txt");

    ASSERT(work_manual_backend_run(tasks, tasks->count, batch));
    ASSERT(access(dst_dir, F_OK) == 0);
    ASSERT(util_equal_files(src_file, dst_file));
    ASSERT(util_equal_files(src_nested, dst_nested));

    work_batch_flush(batch);
    task_list_free(tasks);
    return;
}

static void
test_manual_copy_symlink(MessageBatch **batch) {
    TaskList *tasks;
    char src_link[MAX_PATH_LENGTH];
    char dst_link[MAX_PATH_LENGTH];
    char target[PATH_MAX];
    int32 target_len;

    if (!test_symlink_supported(cecup.base[L])) {
        return;
    }

    SNPRINTF(src_link, "%s/manual_link", cecup.base[L]);
    SNPRINTF(dst_link, "%s/manual_link", cecup.base[R]);
    ASSERT(symlink("manual_target.txt", src_link) == 0);

    tasks = test_task_list_create(1);
    tasks->items[0] = test_task_create(ACTION_SYMLINK, "manual_link");

    ASSERT(work_manual_backend_run(tasks, tasks->count, batch));
    target_len = readlink(dst_link, target, SIZEOF(target) - 1);
    ASSERT_MORE(target_len, 0);
    target[target_len] = '\0';
    ASSERT_EQUAL(target, "manual_target.txt");

    work_batch_flush(batch);
    task_list_free(tasks);
    return;
}

static void
test_manual_copy_hardlinks(MessageBatch **batch) {
    TaskList *tasks;
    char src_a[MAX_PATH_LENGTH];
    char src_b[MAX_PATH_LENGTH];
    char dst_a[MAX_PATH_LENGTH];
    char dst_b[MAX_PATH_LENGTH];
    struct stat stat_a;
    struct stat stat_b;

    if (!test_hardlink_supported(cecup.base[L])) {
        return;
    }

    SNPRINTF(src_a, "%s/manual_hard_a", cecup.base[L]);
    SNPRINTF(src_b, "%s/manual_hard_b", cecup.base[L]);
    SNPRINTF(dst_a, "%s/manual_hard_a", cecup.base[R]);
    SNPRINTF(dst_b, "%s/manual_hard_b", cecup.base[R]);

    test_write_file(src_a, "hardlink-data");
    ASSERT(link(src_a, src_b) == 0);

    tasks = test_task_list_create(2);
    tasks->items[0] = test_task_create(ACTION_NEW, "manual_hard_a");
    tasks->items[1] = test_task_create(ACTION_NEW, "manual_hard_b");

    ASSERT(work_manual_backend_run(tasks, tasks->count, batch));
    ASSERT(lstat(dst_a, &stat_a) == 0);
    ASSERT(lstat(dst_b, &stat_b) == 0);
    ASSERT_EQUAL(stat_a.st_ino, stat_b.st_ino);
    ASSERT_EQUAL(stat_a.st_dev, stat_b.st_dev);

    work_batch_flush(batch);
    task_list_free(tasks);
    return;
}

int
main(void) {
    char *result;
    MessageBatch *batch;
    int32 fd;
    pthread_t thread;
    char temp_dir[MAX_PATH_LENGTH];
    char files_from[MAX_PATH_LENGTH];
    char path[MAX_PATH_LENGTH];

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

    test_make_temp_dir(temp_dir, SIZEOF(temp_dir), "work_rsync");
    SNPRINTF(path, "%s/src", temp_dir);
    mkdir(path, 0755);
    cecup.base[L] = xstrdup(path);
    cecup.base_len[L] = strlen32(cecup.base[L]);

    SNPRINTF(path, "%s/dst", temp_dir);
    mkdir(path, 0755);
    cecup.base[R] = xstrdup(path);
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
    work_batch_push_rename(&batch,
                           MSG_BATCH_ROW_RENAME,
                           L,
                           "old.txt", 7,
                           "new.txt", 7);
    ASSERT(batch != NULL);
    ASSERT_EQUAL(batch->count, 1);
    ASSERT(strcmp(batch->paths[0], "old.txt") == 0);
    ASSERT(strcmp(batch->dst_paths[0], "new.txt") == 0);
    work_batch_flush(&batch);
    ASSERT(batch == NULL);

    /* Test work_remove refuses to remove configured root */
    SNPRINTF(path, "%s/root_guard.txt", cecup.base[R]);
    fd = open(path, O_CREAT | O_WRONLY, 0644);
    close(fd);
    ASSERT(access(path, F_OK) == 0);
    work_remove(&batch, ".", 1, R);
    ASSERT(access(path, F_OK) == 0);
    ASSERT(batch == NULL);
    work_remove(&batch, "./", 2, R);
    ASSERT(access(path, F_OK) == 0);
    ASSERT(access(cecup.base[R], F_OK) == 0);
    ASSERT(batch == NULL);
    unlink(path);

    /* Test work_remove on file */
    SNPRINTF(path, "%s/rm_test.txt", cecup.base[R]);
    fd = open(path, O_CREAT | O_WRONLY, 0644);
    close(fd);
    ASSERT(access(path, F_OK) == 0);
    work_remove(&batch, "rm_test.txt", 11, R);
    ASSERT(access(path, F_OK) != 0);

    /* Test work_remove on directory using FsWalk */
    SNPRINTF(path, "%s/rm_dir", cecup.base[R]);
    mkdir(path, 0755);
    SNPRINTF(path, "%s/rm_dir/file.txt", cecup.base[R]);
    fd = open(path, O_CREAT | O_WRONLY, 0644);
    close(fd);
    work_remove(&batch, "rm_dir/", 7, R);
    SNPRINTF(path, "%s/rm_dir", cecup.base[R]);
    ASSERT(access(path, F_OK) != 0);

    test_manual_copy_regular_and_dir(&batch);
    test_manual_copy_symlink(&batch);
    test_manual_copy_hardlinks(&batch);

    if (test_rsync_backend_supported()) {
        SNPRINTF(path, "%s/sync_test.txt", cecup.base[L]);
        test_write_file(path, "data");

        SNPRINTF(files_from, "%s/files_from", temp_dir);
        fd = open(files_from, O_CREAT | O_WRONLY, 0644);
        ASSERT_MORE_EQUAL(fd, 0);
        write64(fd, "sync_test.txt\n", 14);
        close(fd);

        ASSERT(work_rsync_run(files_from, 1, false, &batch));
        SNPRINTF(path, "%s/sync_test.txt", cecup.base[R]);
        ASSERT(access(path, F_OK) == 0);
        work_batch_flush(&batch);
    }

    /* Test work_rsync compatibility wrapper with the portable backend. */
    {
        ThreadData *thread_data;
        TaskList *task_list;
        Task *task;

        SNPRINTF(path, "%s/thread_test.txt", cecup.base[L]);
        test_write_file(path, "thread-data");

        thread_data = malloc2(SIZEOF(*thread_data));
        task_list = test_task_list_create(1);
        task = test_task_create(ACTION_UPDATE, "thread_test.txt");
        task_list->items[0] = task;

        *thread_data = (ThreadData){0};
        thread_data->tasks = task_list;

        ASSERT(setenv("CECUP_TRANSFER_BACKEND", "manual", true) == 0);
        xpthread_create(&thread, NULL, work_rsync, thread_data);
        xpthread_join(&thread, NULL);
        unsetenv("CECUP_TRANSFER_BACKEND");

        SNPRINTF(path, "%s/thread_test.txt", cecup.base[R]);
        ASSERT(access(path, F_OK) == 0);
    }

    test_remove_tree(temp_dir);
    free2(cecup.base[L], cecup.base_len[L] + 1);
    free2(cecup.base[R], cecup.base_len[R] + 1);

    exit(EXIT_SUCCESS);
}

#endif

#endif /* WORK_RSYNC_C */
