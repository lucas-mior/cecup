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

#if !defined(IPC_C)
#define IPC_C

#include "util.c"

#include "cecup.h"

static void
ipc_dispatch_log_internal(enum DataType type, char *format, va_list va_args) {
    Message *data;
    char buffer[8192];
    int64 n;
    int64 last_whitespace_index;
    int64 current_column;

    n = vsnprintf(buffer, SIZEOF(buffer), format, va_args);

    if ((n <= 0) || (n >= SIZEOF(buffer))) {
        error("Error in vsnprintf(%s) (n = %lld)\n", format, (llong)n);
        exit(EXIT_FAILURE);
    }

    last_whitespace_index = -1;
    current_column = 0;
    for (int64 i = 0; i < n; i += 1) {
        if ((buffer[i] == ' ') || (buffer[i] == '\t') || (buffer[i] == '\n')) {
            last_whitespace_index = i;
        }

        if (buffer[i] == '\n') {
            current_column = 0;
        } else {
            current_column += 1;
        }

        if (current_column > 120) {
            if (last_whitespace_index != -1) {
                buffer[last_whitespace_index] = '\n';
                current_column = i - last_whitespace_index;
                last_whitespace_index = -1;
            }
        }
    }

    g_mutex_lock(&cecup.ui_arena_mutex);
    data = xarena_push(cecup.ui_arena, ALIGN16(SIZEOF(Message)));
    memset64(data, 0, SIZEOF(Message));

    data->message_len = n;
    data->message = xarena_push(cecup.ui_arena, ALIGN16(n + 1));
    memcpy64(data->message, buffer, n + 1);
    g_mutex_unlock(&cecup.ui_arena_mutex);

    data->type = type;
    g_idle_add(update_ui_handler, data);
    return;
}

static void
ipc_dispatch_log(char *format, ...) {
    va_list va_args;

    va_start(va_args, format);
    ipc_dispatch_log_internal(DATA_TYPE_LOG, format, va_args);
    va_end(va_args);
    return;
}

static void
ipc_dispatch_log_error(char *format, ...) {
    va_list va_args;

    va_start(va_args, format);
    ipc_dispatch_log_internal(DATA_TYPE_LOG_ERROR, format, va_args);
    va_end(va_args);
    return;
}

static void
ipc_dispatch_progress(enum DataType type, double fraction) {
    Message *data;
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
    data = xarena_push(cecup.ui_arena, ALIGN16(SIZEOF(Message)));
    memset64(data, 0, SIZEOF(Message));
    g_mutex_unlock(&cecup.ui_arena_mutex);

    data->type = type;
    data->fraction = fraction;
    g_idle_add(update_ui_handler, data);
    return;
}

// Note: NEVER delete lines with // clang-format
// clang-format off
static void
ipc_dispatch_tree(int32 side,
                  enum CecupAction action, enum CecupReason reason,
                  char *path, char *link_target,
                  int64 size, int64 mtime) {
    // clang-format on
    Message *message;
    int64 target_len;

    g_mutex_lock(&cecup.ui_arena_mutex);
    message = xarena_push(cecup.ui_arena, ALIGN16(SIZEOF(Message)));
    memset64(message, 0, SIZEOF(Message));

    message->filepath_length = strlen64(path);
    g_mutex_lock(&cecup.row_arena_mutex);
    message->filepath
        = xarena_push(cecup.row_arena, ALIGN16(message->filepath_length + 1));
    memcpy64(message->filepath, path, message->filepath_length + 1);

    if (link_target) {
        target_len = strlen64(link_target);
        message->link_target_len = target_len;
        message->link_target
            = xarena_push(cecup.row_arena, ALIGN16(target_len + 1));
        memcpy64(message->link_target, link_target, target_len + 1);
    }
    g_mutex_unlock(&cecup.row_arena_mutex);
    g_mutex_unlock(&cecup.ui_arena_mutex);

    message->type = DATA_TYPE_TREE_ROW;
    message->side = side;
    message->action = action;
    message->reason = reason;
    message->size = size;
    message->mtime = mtime;
    g_idle_add(update_ui_handler, message);
    return;
}

#endif /* IPC_C */
