#if !defined(IPC_C)
#define IPC_C

#include "util.c"

#include "cecup.h"

static void
dispatch_log_internal(enum DataType type, char *format, va_list va_args) {
    UIUpdateData *data;
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
    data = xarena_push(cecup.ui_arena, ALIGN16(SIZEOF(UIUpdateData)));
    memset64(data, 0, SIZEOF(UIUpdateData));

    data->message_len = n;
    data->message = xarena_push(cecup.ui_arena, ALIGN16(n + 1));
    memcpy64(data->message, buffer, n + 1);
    g_mutex_unlock(&cecup.ui_arena_mutex);

    data->type = type;
    g_idle_add(update_ui_handler, data);
    return;
}

static void
dispatch_log(char *format, ...) {
    va_list va_args;

    va_start(va_args, format);
    dispatch_log_internal(DATA_TYPE_LOG, format, va_args);
    va_end(va_args);
    return;
}

static void
dispatch_log_error(char *format, ...) {
    va_list va_args;

    va_start(va_args, format);
    dispatch_log_internal(DATA_TYPE_LOG_ERROR, format, va_args);
    va_end(va_args);
    return;
}

#endif /* IPC_C */
