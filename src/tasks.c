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

        free2(task->path, task->path_len + 1);
        free2(task, SIZEOF(*task));
    }

    free2(tasks, STRUCT_ARRAY_SIZE(tasks, Task *, tasks->count));
    return;
}

static TaskList *
get_target_tasks(int8 side, char *clicked_path, enum Action clicked_action) {
    TaskList *tasks;
    int64 tasks_size;
    int32 count;

    tasks_size = STRUCT_ARRAY_SIZE(tasks, Task *, cecup.rows_len);
    count = 0;
    tasks = malloc2(tasks_size);
    memset64(tasks, 0, tasks_size);

    for (int32 row_id = 0; row_id < cecup.rows_len; row_id += 1) {
        char *filepath;
        int32 path_len;
        enum Action action;
        enum Action actions[2];
        enum Reason reason;
        int32 idx;
        Task *task;

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

        task = malloc2(SIZEOF(*task));
        memset64(task, 0, SIZEOF(*task));

        task->path_len = path_len;
        task->path = malloc2(path_len + 1);
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
        tasks = realloc_flex(tasks, cecup.rows_len, count, SIZEOF(Task *));
        tasks->count = count;

        task = malloc2(SIZEOF(*task));
        memset64(task, 0, SIZEOF(*task));

        task->path_len = strlen32(clicked_path);
        task->path = malloc2(task->path_len + 1);
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
        tasks = realloc_flex(tasks, cecup.rows_len, count, SIZEOF(Task *));
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
#include "assert.c"

int
main(void) {
    TaskList *tasks;
    int32 n = 3;

    memset64(&cecup, 0, SIZEOF(cecup));

    // 1. Setup Mock Environment
    cecup.rows_len = n;
    cecup.rows_selected = malloc2(n * SIZEOF(uint8));
    cecup.rows[L] = malloc2(n * SIZEOF(int32));
    cecup.rows[R] = malloc2(n * SIZEOF(int32));

    for (int32 side = 0; side < 2; side += 1) {
        Traversal *t = &cecup.traversal[side];
        t->stats = malloc2(n * SIZEOF(struct stat));
        t->paths = malloc2(n * SIZEOF(char *));
        t->paths_lens = malloc2(n * SIZEOF(int32));
        t->patterns = malloc2(n * SIZEOF(char *));
        t->symlink_targets = malloc2(n * SIZEOF(char *));
        t->map = hash_create_fs_map(INITIAL_CAPACITY, "t->map");

        memset64(t->stats, 0, n * SIZEOF(struct stat));
        memset64(t->patterns, 0, n * SIZEOF(char *));
        memset64(t->symlink_targets, 0, n * SIZEOF(char *));

        for (int32 i = 0; i < n; i += 1) {
            t->paths[i] = malloc2(20);
            snprintf(t->paths[i], 20, "file_%d.txt", i);
            t->paths_lens[i] = (int16)strlen32(t->paths[i]);
            t->stats[i].st_ino = (ino_t)(100 + i);
            t->stats[i].st_mode = S_IFREG | 0644;
            hash_insert_fs_map(t->map, t->paths[i], t->paths_lens[i], i);
        }
    }

    for (int32 i = 0; i < n; i += 1) {
        cecup.rows[L][i] = i;
        cecup.rows[R][i] = i;
        cecup.rows_selected[i] = (i < 2); // Select first two
    }

    // 2. Test row selection loop
    tasks = get_target_tasks(L, NULL, ACTION_EQUAL);
    ASSERT(tasks != NULL);
    ASSERT_EQUAL(tasks->count, 2);
    task_list_free(tasks);

    // 3. Test ACTION_HARDLINK logic via fallback
    // We MUST deselect rows to trigger the fallback block where 'clicked_action' is used
    for (int32 i = 0; i < n; i += 1) {
        cecup.rows_selected[i] = false;
    }

    // Test successful inode lookup in fallback
    {
        char *path_to_find = cecup.traversal[L].paths[0];
        tasks = get_target_tasks(L, path_to_find, ACTION_HARDLINK);
        ASSERT_EQUAL(tasks->count, 1);
        ASSERT(tasks->items[0]->action == ACTION_HARDLINK);
        ASSERT_EQUAL(tasks->items[0]->inode, 100);
        task_list_free(tasks);
    }

    // Test fallback with path not in map (inode remains 0)
    tasks = get_target_tasks(L, "missing.txt", ACTION_HARDLINK);
    ASSERT_EQUAL(tasks->count, 1);
    ASSERT_EQUAL(tasks->items[0]->inode, 0);
    task_list_free(tasks);

    // 4. Cleanup
    task_list_free(NULL);
    for (int32 side = 0; side < 2; side += 1) {
        Traversal *t = &cecup.traversal[side];
        for (int32 i = 0; i < n; i += 1) {
            free2(t->paths[i], 20);
        }
        free2(t->stats, n * SIZEOF(struct stat));
        free2(t->paths, n * SIZEOF(char *));
        free2(t->paths_lens, n * SIZEOF(int32));
        free2(t->patterns, n * SIZEOF(char *));
        free2(t->symlink_targets, n * SIZEOF(char *));
        hash_destroy_fs_map(t->map);
    }
    free2(cecup.rows_selected, n * SIZEOF(uint8));
    free2(cecup.rows[L], n * SIZEOF(int32));
    free2(cecup.rows[R], n * SIZEOF(int32));

    exit(EXIT_SUCCESS);
}

#endif /* TESTING_tasks */

#endif /* TASKS_C */
