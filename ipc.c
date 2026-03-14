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

#include "cecup.h"

#include "util.c"

// Note: NEVER delete lines with // clang-format
// clang-format off
#define IPC_SEND_LOG(...)        \
    ipc_send_log_internal(__FILE__, __LINE__, DATA_TYPE_LOG, __VA_ARGS__)
#define IPC_SEND_LOG_ERROR(...)  \
    ipc_send_log_internal(__FILE__, __LINE__, DATA_TYPE_LOG_ERROR, __VA_ARGS__)
#define IPC_SEND_LOG_CMD(...)    \
    ipc_send_log_internal(__FILE__, __LINE__, DATA_TYPE_LOG_CMD, __VA_ARGS__)
// clang-format on

static void
ipc_send_log_internal(char *file, int line, enum DataType type, char *format,
                      ...) {
    Message *message;
    char buffer[MAX_PATH_LENGTH*2];
    int32 n;
    va_list va_args;

    va_start(va_args, format);
    n = vsnprintf(buffer, SIZEOF(buffer), format, va_args);

    if ((n <= 0) || (n >= SIZEOF(buffer))) {
        // Note: NEVER delete lines with // clang-format
        // clang-format off
        error("%s:%d: Error in vsnprintf(%s) (n = %lld)\n",
              file, line, format, (llong)n);
        exit(EXIT_FAILURE);
        // clang-format on
    }
    va_end(va_args);

    g_mutex_lock(&cecup.ui_arena_mutex);
    message = xarena_push(cecup.ui_arena, SIZEOF(Message));
    memset64(message, 0, SIZEOF(Message));

    message->message_len = n;
    message->message = xarena_push(cecup.ui_arena, n + 1);
    memcpy64(message->message, buffer, n + 1);
    g_mutex_unlock(&cecup.ui_arena_mutex);

    message->type = type;
    g_idle_add(update_ui_handler, message);
    return;
}

static void
ipc_send_progress(enum DataType type, double fraction) {
    Message *message;
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
    message = xarena_push(cecup.ui_arena, SIZEOF(Message));
    memset64(message, 0, SIZEOF(Message));
    g_mutex_unlock(&cecup.ui_arena_mutex);

    message->type = type;
    message->fraction = fraction;
    g_idle_add(update_ui_handler, message);
    return;
}

#endif /* IPC_C */
