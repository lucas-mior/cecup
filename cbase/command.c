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

#if !defined(COMMAND_C)
#define COMMAND_C

#include "util.h"

#define ENUM_NAME CommandFlag
#define ENUM_BITFLAGS 1
#define ENUM_PREFIX_ COMMAND_FLAG_
#define ENUM_FIELDS \
    X(COMMAND_FLAG_CAPTURE_STDOUT)      \
    X(COMMAND_FLAG_CAPTURE_STDERR)      \
    X(COMMAND_FLAG_MERGE_STDERR)        \
    X(COMMAND_FLAG_ASYNC)               \
    X(COMMAND_FLAG_DETACHED)            \
    X(COMMAND_FLAG_NEW_SESSION)         \
    X(COMMAND_FLAG_NEW_PROCESS_GROUP)   \
    X(COMMAND_FLAG_STDIN_TTY)           \
    X(COMMAND_FLAG_CLOSE_STDIN)
#include "xenums.c"

typedef struct CommandResult {
    char *output;
    char *stdout_output;
    char *stderr_output;

    int64 pid;

    int32 output_len;
    int32 stdout_len;
    int32 stderr_len;
    int32 status;
    int32 error_status;
    int32 exit_status;
    int32 term_signal;
    int32 stdout_fd;
    int32 stderr_fd;

    bool exited;
    bool signaled;
    bool stdout_fd_open;
    bool stderr_fd_open;
} CommandResult;

typedef struct Command {
    char **argv;
    char **env;
    char *cwd;

    int32 *argvs_lens;
    int32 *env_lens;
    int32 cwd_len;
    int32 argc;
    int32 env_len;
    int32 cap;
    int32 env_cap;
    int32 error_status;

    CommandResult result;
} Command;

static void command_result_free(CommandResult *result);
static char *command_str(Command *command, int32 *len);
static void command_error_set(Command *command, int32 error_status);
static bool command_wait(Command *command);

static void
command_result_init(CommandResult *result) {
    memset64(result, 0, SIZEOF(*result));
    result->pid = -1;
    result->status = -1;
    result->stdout_fd = -1;
    result->stderr_fd = -1;
    return;
}

static void
command_error_set(Command *command, int32 error_status) {
    command->error_status = error_status;
    command->result.error_status = error_status;
    return;
}

static enum CommandFlag
command_flags_normalized(enum CommandFlag flags) {
    if (flags & COMMAND_FLAG_MERGE_STDERR) {
        if (flags & COMMAND_FLAG_CAPTURE_STDERR) {
            flags |= COMMAND_FLAG_CAPTURE_STDOUT;
        }
    }
    return flags;
}

static bool
command_flags_capture_stdout(enum CommandFlag flags) {
    return flags & COMMAND_FLAG_CAPTURE_STDOUT;
}

static bool
command_flags_capture_stderr(enum CommandFlag flags) {
    return flags & COMMAND_FLAG_CAPTURE_STDERR;
}

static bool
command_flags_capture(enum CommandFlag flags) {
    return command_flags_capture_stdout(flags)
           || command_flags_capture_stderr(flags);
}

#if OS_UNIX
static int32
command_status_from_wait(int status, CommandResult *result) {
    result->exited = false;
    result->signaled = false;
    result->exit_status = -1;
    result->term_signal = 0;

    if (WIFEXITED(status)) {
        result->exited = true;
        result->exit_status = WEXITSTATUS(status);
        result->status = result->exit_status;
    } else if (WIFSIGNALED(status)) {
        result->signaled = true;
        result->term_signal = WTERMSIG(status);
        result->status = 128 + result->term_signal;
    } else {
        result->status = 127;
    }

    return result->status;
}

#endif

static void
command_result_file_descriptors_close(CommandResult *result) {
    if (result->stdout_fd_open) {
        XCLOSE(&result->stdout_fd);
        result->stdout_fd_open = false;
    }
    if (result->stderr_fd_open) {
        XCLOSE(&result->stderr_fd);
        result->stderr_fd_open = false;
    }
    result->stdout_fd = -1;
    result->stderr_fd = -1;
    return;
}

static void
command_result_free(CommandResult *result) {
    command_result_file_descriptors_close(result);

    if (result->output) {
        free2(result->output, result->output_len + 1);
    }
    if (result->stdout_output) {
        free2(result->stdout_output, result->stdout_len + 1);
    }
    if (result->stderr_output) {
        free2(result->stderr_output, result->stderr_len + 1);
    }

    command_result_init(result);
    return;
}

static void
command_result_append(
    StrBuilder *output,
    StrBuilder *stdout_output,
    StrBuilder *stderr_output,
    bool is_stderr,
    char *data,
    int32 data_len
) {
    if (data_len <= 0) {
        return;
    }

    sb_append(output, data, data_len);
    if (is_stderr) {
        sb_append(stderr_output, data, data_len);
    } else {
        sb_append(stdout_output, data, data_len);
    }

    return;
}

#if OS_UNIX
static void
command_result_read_captured(Command *command) {
    enum {
        COMMAND_CAPTURE_STDOUT_INDEX = 0,
        COMMAND_CAPTURE_STDERR_INDEX = 1,
    };
    struct pollfd pipes[2] = {0};
    StrBuilder output = {0};
    StrBuilder stdout_output = {0};
    StrBuilder stderr_output = {0};
    int32 nfds = 0;
    int32 left = 0;
    char buffer[4096];

    pipes[COMMAND_CAPTURE_STDOUT_INDEX].fd = -1;
    pipes[COMMAND_CAPTURE_STDERR_INDEX].fd = -1;

    if (command->result.stdout_fd_open) {
        pipes[COMMAND_CAPTURE_STDOUT_INDEX].fd = command->result.stdout_fd;
        pipes[COMMAND_CAPTURE_STDOUT_INDEX].events = POLLIN;
        pipes[COMMAND_CAPTURE_STDOUT_INDEX].revents = 0;
        nfds = COMMAND_CAPTURE_STDOUT_INDEX + 1;
        left += 1;
    }
    if (command->result.stderr_fd_open) {
        pipes[COMMAND_CAPTURE_STDERR_INDEX].fd = command->result.stderr_fd;
        pipes[COMMAND_CAPTURE_STDERR_INDEX].events = POLLIN;
        pipes[COMMAND_CAPTURE_STDERR_INDEX].revents = 0;
        nfds = COMMAND_CAPTURE_STDERR_INDEX + 1;
        left += 1;
    }

    while (left > 0) {
        int32 ready;

        ready = poll(pipes, (nfds_t)nfds, -1);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            command_error_set(command, errno);
            error("Error polling child output: %s.\n", strerror(errno));
            fatal(EXIT_FAILURE);
        }

        for (int32 i = 0; (i < nfds) && (ready > 0); i += 1) {
            int64 read_bytes;
            bool is_stderr;

            if (pipes[i].fd < 0) {
                continue;
            }
            if (!pipes[i].revents) {
                continue;
            }

            ready -= 1;
            is_stderr = i == COMMAND_CAPTURE_STDERR_INDEX;
            errno = 0;
            read_bytes = read64(pipes[i].fd, buffer, SIZEOF(buffer));
            if (read_bytes > 0) {
                if (read_bytes >= MAXOF(command->result.output_len)) {
                    error("Command output is too long.\n");
                    fatal(EXIT_FAILURE);
                }
                command_result_append(&output,
                                      &stdout_output,
                                      &stderr_output,
                                      is_stderr,
                                      buffer,
                                      (int32)read_bytes);
                pipes[i].revents = 0;
                continue;
            }
            if (read_bytes < 0) {
                if (errno == EINTR) {
                    pipes[i].revents = 0;
                    continue;
                }
                command_error_set(command, errno);
                error("Error reading child output: %s.\n", strerror(errno));
                fatal(EXIT_FAILURE);
            }

            if (is_stderr) {
                XCLOSE(&command->result.stderr_fd);
                command->result.stderr_fd_open = false;
            } else {
                XCLOSE(&command->result.stdout_fd);
                command->result.stdout_fd_open = false;
            }
            pipes[i].fd = -1;
            pipes[i].revents = 0;
            left -= 1;
        }
    }

    command->result.output = sb_steal_exact(&output,
                                            &command->result.output_len);
    command->result.stdout_output = sb_steal_exact(
        &stdout_output,
        &command->result.stdout_len);
    command->result.stderr_output = sb_steal_exact(
        &stderr_output,
        &command->result.stderr_len);
    return;
}

static void
command_child_env_apply(Command *command) {
    for (int32 i = 0; i < command->env_len; i += 1) {
        putenv(command->env[i]);
    }
    return;
}

static void
command_child_exec(
    Command *command,
    enum CommandFlag flags,
    int stdout_pipe[2],
    int stderr_pipe[2]
) {
    if (command->cwd) {
        if (chdir(command->cwd) < 0) {
            error("Error changing directory to %s: %s.\n",
                  command->cwd, strerror(errno));
            _exit(127);
        }
    }

    if (flags & COMMAND_FLAG_NEW_SESSION) {
        if (setsid() < 0) {
            error("Error in setsid: %s.\n", strerror(errno));
            _exit(127);
        }
    } else if (flags & COMMAND_FLAG_NEW_PROCESS_GROUP) {
        if (setpgid(0, 0) < 0) {
            error("Error in setpgid: %s.\n", strerror(errno));
            _exit(127);
        }
    }

    if (flags & COMMAND_FLAG_STDIN_TTY) {
        if (!freopen("/dev/tty", "r", stdin)) {
            error("Error reopening stdin: %s.\n", strerror(errno));
        }
    } else if (flags & COMMAND_FLAG_CLOSE_STDIN) {
        int stdin_fd = STDIN_FILENO;

        XCLOSE(&stdin_fd);
    }

    if (flags & COMMAND_FLAG_CAPTURE_STDOUT) {
        XCLOSE(&stdout_pipe[0]);
        xdup2(stdout_pipe[1], STDOUT_FILENO);
    }

    if (flags & COMMAND_FLAG_CAPTURE_STDERR) {
        if (flags & COMMAND_FLAG_MERGE_STDERR) {
            xdup2(stdout_pipe[1], STDERR_FILENO);
        } else {
            XCLOSE(&stderr_pipe[0]);
            xdup2(stderr_pipe[1], STDERR_FILENO);
        }
    }

    if (flags & COMMAND_FLAG_CAPTURE_STDOUT) {
        XCLOSE(&stdout_pipe[1]);
    }
    if ((flags & COMMAND_FLAG_CAPTURE_STDERR)
        && !(flags & COMMAND_FLAG_MERGE_STDERR)) {
        XCLOSE(&stderr_pipe[1]);
    }

    command_child_env_apply(command);
    execvp(command->argv[0], command->argv);
    {
        char *command_text;
        int32 command_text_len;

        command_text = command_str(command, &command_text_len);
        error("Error executing\n%s\n%s.\n", command_text, strerror(errno));
        free2(command_text, command_text_len + 1);
    }
    _exit(127);
}

static bool
command_start(Command *command, enum CommandFlag flags) {
    int stdout_pipe[2] = {-1, -1};
    int stderr_pipe[2] = {-1, -1};
    pid_t pid;

    flags = command_flags_normalized(flags);
    command_result_free(&command->result);
    command->error_status = 0;

    if (command->argc <= 0) {
        command_error_set(command, EINVAL);
        return false;
    }
    if ((flags & COMMAND_FLAG_DETACHED) && command_flags_capture(flags)) {
        command_error_set(command, EINVAL);
        return false;
    }

    if (flags & COMMAND_FLAG_CAPTURE_STDOUT) {
        xpipe(stdout_pipe);
    }
    if ((flags & COMMAND_FLAG_CAPTURE_STDERR)
        && !(flags & COMMAND_FLAG_MERGE_STDERR)) {
        xpipe(stderr_pipe);
    }

    switch (pid = fork()) {
    case -1:
        if (stdout_pipe[0] >= 0) {
            XCLOSE(&stdout_pipe[0]);
        }
        if (stdout_pipe[1] >= 0) {
            XCLOSE(&stdout_pipe[1]);
        }
        if (stderr_pipe[0] >= 0) {
            XCLOSE(&stderr_pipe[0]);
        }
        if (stderr_pipe[1] >= 0) {
            XCLOSE(&stderr_pipe[1]);
        }
        command_error_set(command, errno);
        error("Error forking: %s.\n", strerror(errno));
        fatal(EXIT_FAILURE);
    case 0:
        if (flags & COMMAND_FLAG_DETACHED) {
            pid_t detached_pid;

            switch (detached_pid = fork()) {
            case -1:
                error("Error forking detached child: %s.\n", strerror(errno));
                _exit(127);
            case 0:
                break;
            default:
                _exit(0);
            }
        }
        command_child_exec(command, flags, stdout_pipe, stderr_pipe);
    default:
        break;
    }

    command->result.pid = (int64)pid;

    if (flags & COMMAND_FLAG_CAPTURE_STDOUT) {
        XCLOSE(&stdout_pipe[1]);
        command->result.stdout_fd = stdout_pipe[0];
        command->result.stdout_fd_open = true;
    }
    if ((flags & COMMAND_FLAG_CAPTURE_STDERR)
        && !(flags & COMMAND_FLAG_MERGE_STDERR)) {
        XCLOSE(&stderr_pipe[1]);
        command->result.stderr_fd = stderr_pipe[0];
        command->result.stderr_fd_open = true;
    }

    return true;
}

static bool
command_wait(Command *command) {
    int status;

    if (command->result.pid <= 0) {
        command_error_set(command, EINVAL);
        return false;
    }

    while (waitpid((pid_t)command->result.pid, &status, 0) < 0) {
        if (errno == EINTR) {
            continue;
        }
        command_error_set(command, errno);
        error("Error waiting for child: %s.\n", strerror(errno));
        return false;
    }

    command_status_from_wait(status, &command->result);
    return true;
}

static bool
command_signal(Command *command, int32 signal_number, bool process_group) {
    pid_t pid;

    if (command->result.pid <= 0) {
        command_error_set(command, EINVAL);
        return false;
    }

    pid = (pid_t)command->result.pid;
    if (process_group) {
        pid = -pid;
    }

    if (kill(pid, signal_number) < 0) {
        command_error_set(command, errno);
        error("Error sending signal %d to child: %s.\n",
              signal_number, strerror(errno));
        return false;
    }

    return true;
}
#endif

static bool
command_run(Command *command, enum CommandFlag flags) {
    flags = command_flags_normalized(flags);

#if OS_UNIX
    if (!command_start(command, flags)) {
        return false;
    }
    if (flags & COMMAND_FLAG_DETACHED) {
        return command_wait(command);
    }
    if (flags & COMMAND_FLAG_ASYNC) {
        return true;
    }
    if (command_flags_capture(flags)) {
        command_result_read_captured(command);
    }
    return command_wait(command);
#elif OS_WINDOWS
    command_result_free(&command->result);
    command->error_status = 0;
    if (command->argc <= 0) {
        command_error_set(command, EINVAL);
        return false;
    }
    if (command_flags_capture(flags) || (flags & COMMAND_FLAG_ASYNC)) {
        command_error_set(command, ENOSYS);
        return false;
    }
    command->result.status = util_command(command->argc, command->argv);
    command->result.exit_status = command->result.status;
    command->result.exited = true;
    return true;
#else
    (void)flags;
    command_result_free(&command->result);
    command_error_set(command, ENOSYS);
    return false;
#endif
}

static bool
command_run_sync(Command *command, int *exit_status) {
    bool success;

    success = command_run(command, COMMAND_FLAG_NONE);
    if (success && exit_status) {
        *exit_status = command->result.status;
    }
    return success;
}

static bool
command_run_async(Command *command, enum CommandFlag flags) {
    flags |= COMMAND_FLAG_ASYNC;
    return command_run(command, flags);
}

static bool
command_run_capture(Command *command, enum CommandFlag flags) {
    flags |= COMMAND_FLAG_CAPTURE_STDOUT;
    return command_run(command, flags);
}

static bool
command_run_capture_all(Command *command) {
    return command_run(command,
                       COMMAND_FLAG_CAPTURE_STDOUT
                       |COMMAND_FLAG_CAPTURE_STDERR);
}

static bool
command_run_capture_combined(Command *command) {
    return command_run(command,
                       COMMAND_FLAG_CAPTURE_STDOUT
                       |COMMAND_FLAG_CAPTURE_STDERR
                       |COMMAND_FLAG_MERGE_STDERR);
}

static void
command_print(Command *command) {
    printf(RED("%s"), command->argv[0]);
    for (int32 i = 1; i < command->argc; i += 1) {
        printf(" %s", command->argv[i]);
    }
    printf("\n");
    return;
}

static char *
command_str(Command *command, int32 *len) {
    StrBuilder str_builder = {0};

    for (int32 i = 0; i < command->argc; i += 1) {
        if (i > 0) {
            sb_append_byte(&str_builder, ' ');
        }
        SB_APPEND(&str_builder, command->argv[i], command->argvs_lens[i]);
    }
    return sb_steal_exact(&str_builder, len);
}

static void
command_vector_reserve(
    char ***items,
    int32 **item_lens,
    int32 *cap,
    int32 len,
    int32 extra
) {
    int32 needed;

    needed = len + extra + 1;
    if ((needed < len) || (needed >= MAXOF(*cap))) {
        error("Command has too many items.\n");
        fatal(EXIT_FAILURE);
    }
    if (*cap > needed) {
        return;
    }

    do {
        int32 oldcap = *cap;

        *cap += 16;
        *items = realloc2(*items, oldcap, *cap, SIZEOF(**items));
        *item_lens = realloc2(*item_lens, oldcap, *cap, SIZEOF(**item_lens));
    } while (*cap <= needed);

    return;
}

static void
command_push_owned_length(
    char ***items,
    int32 **item_lens,
    int32 *len,
    int32 *cap,
    char *argument,
    int32 argument_len
) {
    char *copy;

    if (argument_len < 0) {
        error("Command argument has invalid length.\n");
        fatal(EXIT_FAILURE);
    }

    command_vector_reserve(items, item_lens, cap, *len, 1);
    copy = malloc2(argument_len + 1);
    memcpy64(copy, argument, argument_len);
    copy[argument_len] = '\0';

    (*items)[*len] = copy;
    (*item_lens)[*len] = argument_len;
    *len += 1;
    (*items)[*len] = NULL;
    (*item_lens)[*len] = 0;
    return;
}

static void
command_push_length(Command *command, char *argument, int32 argument_len) {
    command_push_owned_length(&command->argv,
                              &command->argvs_lens,
                              &command->argc,
                              &command->cap,
                              argument,
                              argument_len);
    return;
}

static void
command_push(Command *command, char *argument) {
    command_push_length(command, argument, strlen32(argument));
    return;
}

#define COMMAND_PUSH_2(A, B) \
        command_push(A, B)
#define COMMAND_PUSH_3(A, B, B_LEN) \
        command_push_length(A, B, B_LEN)
#define COMMAND_PUSH(...) SELECT_ON_NUM_ARGS(COMMAND_PUSH_, __VA_ARGS__)

static void
command_env_push_length(
    Command *command,
    char *assignment,
    int32 assignment_len
) {
    command_push_owned_length(&command->env,
                              &command->env_lens,
                              &command->env_len,
                              &command->env_cap,
                              assignment,
                              assignment_len);
    return;
}

static void
command_env_push(Command *command, char *assignment) {
    command_env_push_length(command, assignment, strlen32(assignment));
    return;
}

#define COMMAND_ENV_PUSH_2(A, B) \
        command_env_push(A, B)
#define COMMAND_ENV_PUSH_3(A, B, B_LEN) \
        command_env_push_length(A, B, B_LEN)
#define COMMAND_ENV_PUSH(...) SELECT_ON_NUM_ARGS(COMMAND_ENV_PUSH_, __VA_ARGS__)

static void
command_push_split(Command *command, char *arguments, char *delimiters) {
    char *argument = arguments;

    for (;;) {
        int64 delimiter_count;
        int64 argument_len;
        int32 argument_len32;

        delimiter_count = (int64)strspn(argument, delimiters);
        argument += delimiter_count;
        if (*argument == '\0') {
            break;
        }

        argument_len = (int64)strcspn(argument, delimiters);
        if (argument_len >= MAXOF(argument_len32)) {
            error("Command argument is too long.\n");
            fatal(EXIT_FAILURE);
        }
        argument_len32 = (int32)argument_len;
        command_push_length(command, argument, argument_len32);
        argument += argument_len;
    }
    return;
}

static void
command_argv0_set(Command *command, char *argument) {
    int32 argument_len = strlen32(argument);

    ASSERT_MORE(command->argc, 0);
    free2(command->argv[0], command->argvs_lens[0] + 1);
    command->argv[0] = xstrdup(argument);
    command->argvs_lens[0] = argument_len;
    return;
}

static void
command_cwd_clear(Command *command) {
    if (command->cwd) {
        free2(command->cwd, command->cwd_len + 1);
    }
    command->cwd = NULL;
    command->cwd_len = 0;
    return;
}

static void
command_cwd_set(Command *command, char *cwd) {
    command_cwd_clear(command);
    if (cwd) {
        command->cwd_len = strlen32(cwd);
        command->cwd = xstrdup(cwd);
    }
    return;
}

static void
command_reset(Command *command) {
    for (int32 i = 0; i < command->argc; i += 1) {
        free2(command->argv[i], command->argvs_lens[i] + 1);
        command->argv[i] = NULL;
        command->argvs_lens[i] = 0;
    }
    command->argc = 0;
    if (command->argv) {
        command->argv[0] = NULL;
    }
    if (command->argvs_lens) {
        command->argvs_lens[0] = 0;
    }
    command_error_set(command, 0);
    command_result_free(&command->result);
    return;
}

static void
command_env_clear(Command *command) {
    for (int32 i = 0; i < command->env_len; i += 1) {
        free2(command->env[i], command->env_lens[i] + 1);
        command->env[i] = NULL;
        command->env_lens[i] = 0;
    }
    command->env_len = 0;
    if (command->env) {
        command->env[0] = NULL;
    }
    if (command->env_lens) {
        command->env_lens[0] = 0;
    }
    return;
}

static void
command_free(Command *command) {
    command_reset(command);
    command_env_clear(command);
    command_cwd_clear(command);

    free2(command->argv, command->cap*SIZEOF(*command->argv));
    free2(command->argvs_lens,
          command->cap*SIZEOF(*command->argvs_lens));
    free2(command->env, command->env_cap*SIZEOF(*command->env));
    free2(command->env_lens,
          command->env_cap*SIZEOF(*command->env_lens));

    command->argv = NULL;
    command->argvs_lens = NULL;
    command->env = NULL;
    command->env_lens = NULL;
    command->cap = 0;
    command->env_cap = 0;
    return;
}

static void
command_printf(Command *command, char *fmt, ...) {
    va_list ap;
    va_list ap2;
    int32 n;
    char *argument;

    va_start(ap, fmt);
    va_copy(ap2, ap);
    n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);

    if (n < 0) {
        va_end(ap2);
        error("Error formatting \"%s\".", fmt);
        fatal(EXIT_FAILURE);
    }

    argument = malloc2(n + 1);
    n = vsnprintf(argument, (size_t)n + 1, fmt, ap2);
    va_end(ap2);

    command_push_length(command, argument, n);

    free2(argument, n + 1);
    return;
}

static void
command_env_printf(Command *command, char *fmt, ...) {
    va_list ap;
    va_list ap2;
    int32 n;
    char *argument;

    va_start(ap, fmt);
    va_copy(ap2, ap);
    n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);

    if (n < 0) {
        va_end(ap2);
        error("Error formatting \"%s\".", fmt);
        fatal(EXIT_FAILURE);
    }

    argument = malloc2(n + 1);
    n = vsnprintf(argument, (size_t)n + 1, fmt, ap2);
    va_end(ap2);

    command_env_push_length(command, argument, n);

    free2(argument, n + 1);
    return;
}

#endif /* COMMAND_C */
