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

#if !defined(TASKS_C)
#define TASKS_C

#include "cecup.h"
#include "item.c"

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_tasks 1
#elif !defined(TESTING_tasks)
#define TESTING_tasks 0
#endif
#if !defined(TESTING)
#define TESTING 0
#endif

static void
task_list_free(TaskList *tasks) {
    if (tasks == NULL) {
        return;
    }

    for (int32 i = 0; i < tasks->count; i += 1) {
        Task *task = tasks->items[i];

        free(task->path, task->path_len + 1);
        free(task, SIZEOF(*task));
    }

    free(tasks, STRUCT_ARRAY_SIZE(tasks, Task *, tasks->count));
    return;
}

static TaskList *
get_target_tasks(int8 side, char *clicked_path, enum Action clicked_action) {
    TaskList *tasks;
    int64 tasks_size;
    int32 count;

    tasks_size = STRUCT_ARRAY_SIZE(tasks, Task *, cecup.rows_len);
    count = 0;
    tasks = xmalloc(tasks_size);
    memset64(tasks, 0, tasks_size);

    for (int32 i = 0; i < cecup.rows_len; i += 1) {
        int32 row_id;
        char *filepath;
        int32 path_len;
        enum Action action;
        enum Action actions[2];
        enum Reason reason;
        int32 idx;
        Task *task;

        row_id = i;
        if (!(cecup.rows_selected[row_id])) {
            continue;
        }

        item_get_actions_reasons(row_id, &actions[L], &actions[R], &reason);
        filepath = item_path_side(row_id, side);
        path_len = item_path_len_side(row_id, side);
        action = actions[side];

        if (filepath == NULL) {
            continue;
        }

        task = xmalloc(SIZEOF(*task));
        memset64(task, 0, SIZEOF(*task));

        task->path_len = path_len;
        task->path = xmalloc(path_len + 1);
        memcpy64(task->path, filepath, path_len + 1);

        if (action == ACTION_HARDLINK) {
            idx = cecup.rows[side][row_id];
            task->inode = cecup.traversal[side].stats[idx].st_ino;
        }

        task->action = action;
        task->side = side;

        tasks->items[count] = task;
        count += 1;
    }

    if ((count == 0) && clicked_path) {
        Task *task;
        int32 idx;

        count = 1;
        // TODO: In the first branch, xrealloc is used. If it fails, xrealloc typically aborts based
        // on your util.c conventions. If it doesn't abort, 'tasks' could be leaked if it returns
        // NULL. Be sure your 'xrealloc' implementation correctly handles potential failures.
        tasks = xrealloc(tasks, STRUCT_ARRAY_SIZE(tasks, Task *, count));
        tasks->count = count;

        task = xmalloc(SIZEOF(*task));
        memset64(task, 0, SIZEOF(*task));

        task->path_len = strlen32(clicked_path);
        task->path = xmalloc(task->path_len + 1);
        memcpy64(task->path, clicked_path, task->path_len + 1);

        if (clicked_action == ACTION_HARDLINK) {
            if (hash_lookup_fs_map(cecup.traversal[side].map, task->path, task->path_len, &idx)) {
                task->inode = cecup.traversal[side].stats[idx].st_ino;
            }
        }

        task->action = clicked_action;
        task->side = side;
        tasks->items[0] = task;
    } else {
        // TODO: Similar to the above, ensure xrealloc handles failures correctly to prevent leaking
        // the initial 'tasks' allocation if shrinking fails.
        tasks = xrealloc(tasks, STRUCT_ARRAY_SIZE(tasks, Task *, count));
        tasks->count = count;
    }

    return tasks;
}

#if (0 == TESTING_tasks) && TESTING
static inline void
tasks_functions_sink(void) {
    (void)tasks_functions_sink;
    return;
}
#endif

#if TESTING_tasks
#include <assert.h>
#include <string.h>
#include "work.c"

int
main(void) {
    ASSERT(true);
    exit(EXIT_SUCCESS);
}

#endif /* TESTING_tasks */

#endif /* TASKS_C */
