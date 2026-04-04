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
    TaskList *test_tasks_free;
    Task *task_item;
    TaskList *target_tasks;
    TaskList *clicked_tasks;
    char *dummy_path;
    int32 dummy_path_len;

    dummy_path = "dummy.txt";
    dummy_path_len = strlen32(dummy_path);

    test_tasks_free = xmalloc(STRUCT_ARRAY_SIZE(test_tasks_free, Task *, 1));
    test_tasks_free->count = 1;

    task_item = xmalloc(SIZEOF(*task_item));
    memset64(task_item, 0, SIZEOF(*task_item));
    task_item->path_len = dummy_path_len;
    task_item->path = xmalloc(dummy_path_len + 1);
    memcpy64(task_item->path, dummy_path, dummy_path_len + 1);
    test_tasks_free->items[0] = task_item;

    task_list_free(NULL);
    task_list_free(test_tasks_free);

    cecup.rows_len = 2;
    cecup.rows_selected = xmalloc(2 * SIZEOF(uint8));
    cecup.rows_selected[0] = true;
    cecup.rows_selected[1] = false;

    cecup.rows[L] = xmalloc(2 * SIZEOF(int32));
    cecup.rows[R] = xmalloc(2 * SIZEOF(int32));
    cecup.rows[L][0] = 0;
    cecup.rows[R][0] = 0;
    cecup.rows[L][1] = 1;
    cecup.rows[R][1] = 1;

    cecup.traversal[L].stats = xmalloc(2 * SIZEOF(struct stat));
    cecup.traversal[R].stats = xmalloc(2 * SIZEOF(struct stat));
    memset64(cecup.traversal[L].stats, 0, 2 * SIZEOF(struct stat));
    memset64(cecup.traversal[R].stats, 0, 2 * SIZEOF(struct stat));

    cecup.traversal[L].paths = xmalloc(2 * SIZEOF(char *));
    cecup.traversal[R].paths = xmalloc(2 * SIZEOF(char *));
    cecup.traversal[L].paths[0] = "test1.txt";
    cecup.traversal[R].paths[0] = "test1.txt";
    cecup.traversal[L].paths[1] = "test2.txt";
    cecup.traversal[R].paths[1] = "test2.txt";

    cecup.traversal[L].paths_lens = xmalloc(2 * SIZEOF(int32));
    cecup.traversal[R].paths_lens = xmalloc(2 * SIZEOF(int32));
    cecup.traversal[L].paths_lens[0] = strlen32("test1.txt");
    cecup.traversal[R].paths_lens[0] = strlen32("test1.txt");
    cecup.traversal[L].paths_lens[1] = strlen32("test2.txt");
    cecup.traversal[R].paths_lens[1] = strlen32("test2.txt");

    cecup.traversal[L].patterns = xmalloc(2 * SIZEOF(char *));
    cecup.traversal[R].patterns = xmalloc(2 * SIZEOF(char *));
    cecup.traversal[L].patterns[0] = NULL;
    cecup.traversal[R].patterns[0] = NULL;
    cecup.traversal[L].patterns[1] = NULL;
    cecup.traversal[R].patterns[1] = NULL;

    cecup.traversal[L].symlink_targets = xmalloc(2 * SIZEOF(char *));
    cecup.traversal[R].symlink_targets = xmalloc(2 * SIZEOF(char *));
    cecup.traversal[L].symlink_targets[0] = NULL;
    cecup.traversal[R].symlink_targets[0] = NULL;
    cecup.traversal[L].symlink_targets[1] = NULL;
    cecup.traversal[R].symlink_targets[1] = NULL;

    cecup.delete_after = false;
    cecup.delete_ignored = false;

    target_tasks = get_target_tasks(L, NULL, ACTION_EQUAL);
    ASSERT(target_tasks != NULL);
    ASSERT(target_tasks->count == 1);
    ASSERT(target_tasks->items[0]->action == ACTION_EQUAL);

    task_list_free(target_tasks);

    cecup.rows_selected[0] = false;
    clicked_tasks = get_target_tasks(L, "clicked.txt", ACTION_UPDATE);
    ASSERT(clicked_tasks != NULL);
    ASSERT(clicked_tasks->count == 1);
    ASSERT(clicked_tasks->items[0]->action == ACTION_UPDATE);

    task_list_free(clicked_tasks);

    exit(EXIT_SUCCESS);
}

#endif /* TESTING_tasks */

#endif /* TASKS_C */
