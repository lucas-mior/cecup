// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(WORK_C)
#define WORK_C

#include "cecup.h"
#include "cbase.h"
#include "util.c"
#include "update.c"
#include "traversal.c"
#include "ignore_patterns.c"

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_work 1
#elif !defined(TESTING_work)
#define TESTING_work 0
#endif
#if !defined(TESTING)
#define TESTING 0
#endif

static void __attribute__((noreturn))
work_finalize(ThreadData *thread_data, bool preview_clean) {
    update_progress_bar(1.0);

    {
        Message *message = malloc2(SIZEOF(*message));
        *message = (Message){0};

        message->type = MSG_WORK_FINISHED;
        message->preview_clean = preview_clean;

        g_idle_add(update_ui_handler, message);
    }

    free2(thread_data, SIZEOF(*thread_data));
    work_thread_done_set(true);
    pthread_exit(NULL);
}

static void
work_traverse_unknown_record(
    Traversal *traversal,
    FsWalkEntry *entry,
    enum TraversalState state,
    bool known_dir
) {
    char *path_tmp;
    int32 path_len;
    char path[MAX_PATH_LENGTH];

    if (entry->path_len >= MAX_PATH_LENGTH) {
        LOG_ERROR(_("Traversal error path is too long: %s.\n"),
                  entry->path);
        traversal_root_unknown_record(traversal);
        return;
    }

    path_tmp = entry->path + traversal->base_path_len;
    path_len = (int32)entry->path_len - traversal->base_path_len;

    if (path_tmp[0] == '/') {
        path_tmp += 1;
        path_len -= 1;
    }

    if (path_len == 0) {
        traversal_root_unknown_record(traversal);
        return;
    }

    memcpy64(path, path_tmp, path_len + 1);
    normalize(path, &path_len);

    if (known_dir && (path[path_len - 1] != '/')) {
        if ((path_len + 1) >= MAX_PATH_LENGTH) {
            LOG_ERROR(_("Traversal error path is too long: %s.\n"),
                      entry->path);
            traversal_root_unknown_record(traversal);
            return;
        }

        path_len += 1;
        path[path_len - 1] = '/';
        path[path_len] = '\0';
    }

    traversal_unknown_record(traversal, path, path_len, state);
    return;
}

static int32
work_traverse_fs(Traversal *traversal) {
    int64 file_count = 0;
    int32 file_count_return = 0;
    FsWalk fs_walk;
    FsWalkEntry *entry;
    struct timespec time_last_report = {0};

    if (work_should_stop()) {
        return 0;
    }

    if (!fs_walk_open(&fs_walk, traversal->base_path)) {
        LOG_ERROR(_("Error walking directory %s: %s.\n"),
                  traversal->base_path, strerror(errno));
        traversal_root_unknown_record(traversal);
        return 0;
    }

    errno = 0;
    while ((entry = fs_walk_read(&fs_walk))) {
        char *d_name;
        int32 name_len;
        int32 old_full_len;
        char *path;
        int32 path_len;
        int32 is_dir = false;
        char *symlink_target = NULL;
        int32 symlink_target_len = 0;
        char *matched_pattern = NULL;
        int32 matched_pattern_len = 0;

        if (work_should_stop()) {
            break;
        }

        switch (entry->info) {
        case FS_WALK_PRE_DIR:
            is_dir = true;
            break;
        case FS_WALK_CYCLE:
            continue;
        case FS_WALK_DEFAULT:
            continue;
        case FS_WALK_DOT:
            continue;
        case FS_WALK_POST_DIR:
            continue;
        case FS_WALK_ERROR:
            LOG_ERROR(_("Error while traversing file system: %s.\n"),
                      strerror(entry->error));
            work_traverse_unknown_record(traversal, entry,
                                         TRAVERSAL_STATE_UNKNOWN_SUBTREE,
                                         false);
            continue;
        case FS_WALK_DIR_UNREADABLE:
            LOG_ERROR(_("Directory '%s' is unreadable.\n"), entry->path);
            work_traverse_unknown_record(traversal, entry,
                                         TRAVERSAL_STATE_UNKNOWN_SUBTREE,
                                         true);
            continue;
        case FS_WALK_STAT_ERROR:
            LOG_ERROR(_("Failed to get file information for %s: %s.\n"),
                      entry->path, strerror(entry->error));
            work_traverse_unknown_record(traversal, entry,
                                         TRAVERSAL_STATE_UNKNOWN_SUBTREE,
                                         false);
            continue;
        case FS_WALK_FILE:
            break;
        case FS_WALK_INIT:
            continue;
        case FS_WALK_STAT_OK:
            continue;
        case FS_WALK_SYMLINK:
            break;
        case FS_WALK_SYMLINK_BROKEN:
            continue;
        case FS_WALK_WHITEOUT:
            continue;
        default:
            continue;
        }

        d_name = entry->name;
        name_len = entry->name_len;
        old_full_len = entry->path_len;

        if (old_full_len >= (MAX_PATH_LENGTH / 2)) {
            LOG_ERROR(_("Error: file path is too long:\n"));
            LOG_ERROR("%s\n", entry->path);
            LOG_ERROR(_("Please fix your file system.\n"));
            stop_working(true);
            break;
        }

        if (name_len > 0) {
            if (isspace((uchar)d_name[0])) {
                LOG_ERROR(_("Error: there is a space in the start of the filename:\n"));
                LOG_ERROR("'%s'\n", entry->path);
                LOG_ERROR(_("Please fix your file system.\n"));
                stop_working(true);
                break;
            }

            if (isspace((uchar)d_name[name_len - 1])) {
                LOG_ERROR(_("Error: there is space in the end of the filename:\n"));
                LOG_ERROR("'%s'\n", entry->path);
                LOG_ERROR(_("Please fix your file system.\n"));
                stop_working(true);
                break;
            }
        }

        for (int32 i = 0; i < LENGTH(filename_problems); i += 1) {
            char *problem;
            int64 problem_len;

            problem = filename_problems[i];
            problem_len = strlen32(problem);

            if (memmem64(d_name, name_len, problem, problem_len)) {
                LOG_ERROR(_("Error: filename contains problematic characters/patterns:\n"));
                LOG_ERROR("'%s'\n", entry->path);
                LOG_ERROR(_("Please fix your file system.\n"));
                stop_working(true);
                break;
            }
        }

        file_count += 1;
        if (file_count >= MAXOF(file_count_return)) {
            LOG_ERROR(_("More than %lld files found.\n"), MAXOF(file_count_return));
            LOG_ERROR(_("Please work in smaller subdirs.\n"));
            stop_working(true);
        }

        if (work_should_stop()) {
            break;
        }

        {
            char *path_tmp;

            path_tmp = entry->path + traversal->base_path_len;
            path_len = old_full_len - traversal->base_path_len;

            if (path_tmp[0] == '/') {
                path_tmp += 1;
                path_len -= 1;
            }

            if (path_len == 0) {
                path_tmp = ".";
                path_len = 1;
            }

            path = xarena_push(traversal->arena, path_len + is_dir + 1);
            memcpy64(path, path_tmp, path_len + 1);

            if (is_dir && (path[path_len - 1] != '/')) {
                path_len += 1;
                path[path_len - 1] = '/';
                path[path_len] = '\0';
            }
        }

        {
            IgnorePattern *pattern;

            pattern = ignore_patterns_match(path, path_len, is_dir,
                                            cecup.ignore_patterns,
                                            cecup.ignore_count);
            if (pattern) {
                matched_pattern = pattern->str;
                matched_pattern_len = pattern->len;
                if (is_dir) {
                    if (fs_walk_skip(&fs_walk, entry) < 0) {
                        error("Error in fs_walk_skip: %s.\n", strerror(errno));
                        fatal(EXIT_FAILURE);
                    }
                }
            }
        }

        if (entry->info == FS_WALK_SYMLINK) {
            symlink_target_len = traversal_symlink_get(traversal, entry->path,
                                                        &symlink_target);
        }

        if ((entry->info == FS_WALK_FILE)
             && (entry->stat->st_nlink > 1)
             && !matched_pattern) {
            traversal_add_link(traversal, *(entry->stat), path, path_len);
        }

        traversal_push(traversal, entry->stat,
                       path, path_len,
                       symlink_target, symlink_target_len,
                       matched_pattern, matched_pattern_len);

        if ((file_count % 4096) == 0) {
            struct timespec time_now;
            int64 seconds;
            int64 nanos;

            time_monotonic_coarse(&time_now);
            seconds = time_now.tv_sec - time_last_report.tv_sec;
            nanos = time_now.tv_nsec - time_last_report.tv_nsec;

            if ((seconds >= 1) || (nanos > MILLIS_AS_NANOS(100))) {
                LOG("Found %lld files... %s\r", file_count, entry->path);
                time_monotonic_coarse(&time_last_report);
            }
        }

        errno = 0;
    }

    if (errno) {
        LOG_ERROR(_("Error in fs_walk_read(%s): %s.\n"),
                  traversal->base_path, strerror(errno));
        traversal_root_unknown_record(traversal);
    }
    if (fs_walk_close(&fs_walk) < 0) {
        LOG_ERROR(_("Error in fs_walk_close: %s.\n"), strerror(errno));
        traversal_root_unknown_record(traversal);
    }

    for (uint32 i = 0; i < traversal->inode_map->capacity; i += 1) {
        Bucket_inode_map *bucket = &traversal->inode_map->array[i];
        HardLinks hard_links = bucket->value;
        int8 slot_state = traversal->inode_map->slot_states[i];

        if (slot_state != HASH_SLOT_USED) {
            continue;
        }

        qsort64(hard_links.names, hard_links.count, SIZEOF(char *), compare_names);
        for (int32 k = 0; k < hard_links.count; k += 1) {
            // TODO: use smarter sorting mechanism so that we don't have to call strlen32
            // on every link (we already know the lengths, but they are out of sync after
            // qsort64 with compare_names().
            hard_links.names_lens[k] = strlen32(hard_links.names[k]);
        }
    }

    file_count_return = (int32)file_count;
    return file_count_return;
}

static void *
work_traverse_fs_thread(void *user_data) {
    Traversal *data = user_data;
    data->file_count = work_traverse_fs(data);
    return NULL;
}

static void
work_cleanup(void) {
    xpthread_mutex_lock(&cecup.arena_mutex);

    traversal_clean(&cecup.traversal[L]);
    traversal_clean(&cecup.traversal[R]);

    hash_zero_actions_set(cecup.actions_set);
    cecup.ntransfers = 0;
    cecup.ndeletions = 0;

    cecup.rows_len = 0;

    xpthread_mutex_unlock(&cecup.arena_mutex);
    return;
}

static void __attribute__((noreturn))
work_preview_cancel_and_reset(ThreadData *thread_data) {
    work_cleanup();
    work_finalize(thread_data, false);
}

static void *
work_preview(void *user_data) {
    int64 nfiles_total;
    int64 nfiles_processed = 0;
    bool same_fs;
    struct timespec t0_work;
    struct timespec t1_work;
    ThreadData *thread_data = user_data;

    update_progress_info(_("Analyzing changes"),
                         _("Traversing file systems for differences..."));

    time_monotonic_precise(&t0_work);
    work_cleanup();

    {
        struct stat stat_src;
        struct stat stat_dst;

        if (stat(cecup.base[L], &stat_src) < 0) {
            LOG_ERROR(_("Error getting directory info from %s: %s.\n"),
                      cecup.base[L], strerror(errno));
            work_preview_cancel_and_reset(thread_data);
        }
        if (stat(cecup.base[R], &stat_dst) < 0) {
            LOG_ERROR(_("Error getting directory info from %s: %s.\n"),
                      cecup.base[R], strerror(errno));
            work_preview_cancel_and_reset(thread_data);
        }

        same_fs = (stat_src.st_dev == stat_dst.st_dev);
    }

    if (thread_data->check_different_fs && same_fs) {
        LOG_ERROR(_("Safety stop: Original and backup are on the same storage device.\n"
                    "Check if the backup device is connected.\n"
                    "To force backup on a folder in the same device, uncheck"
                    " option \"Protect same drive sync\".\n"));

        work_preview_cancel_and_reset(thread_data);
    }

    ignore_patterns_load();

    cecup.traversal[L].base_path = cecup.base[L];
    cecup.traversal[L].base_path_len = cecup.base_len[L];

    cecup.traversal[R].base_path = cecup.base[R];
    cecup.traversal[R].base_path_len = cecup.base_len[R];

    LOG(_("Traversing file systems...\n"));
    if (!same_fs) {
        pthread_t t1;
        pthread_t t2;

        xpthread_create(&t1, NULL, work_traverse_fs_thread, &cecup.traversal[R]);
        xpthread_create(&t2, NULL, work_traverse_fs_thread, &cecup.traversal[L]);

        xpthread_join(&t1, NULL);
        xpthread_join(&t2, NULL);
    } else {
        work_traverse_fs_thread(&cecup.traversal[L]);
        work_traverse_fs_thread(&cecup.traversal[R]);
    }

    if (work_should_stop()) {
        LOG_ERROR(_("Stop requested.\n"));
        work_preview_cancel_and_reset(thread_data);
    }

    LOG(_("File system traversal finished.\n"));
    if (cecup.delete_after && (cecup.traversal[L].unknown_count > 0)) {
        LOG_ERROR(_("Some source paths could not be fully traversed. "
                    "Delete-after will preserve matching backup paths.\n"));
    }

    nfiles_total = cecup.traversal[L].file_count + cecup.traversal[R].file_count;
    LOG(_("Found %lld files to analyse...\n"), nfiles_total);

    for (uint32 i = 0; i < cecup.traversal[L].map->capacity; i += 1) {
        Bucket_fs_map *bucket_src = &(cecup.traversal[L].map->array[i]);
        int32 src_idx;
        int32 dst_idx;
        int32 path_len;
        int32 row_id;
        enum Action action_src;
        enum Action action_dst;
        enum Reason reason;

        if (work_should_stop()) {
            LOG_ERROR(_("Stop requested.\n"));
            work_preview_cancel_and_reset(thread_data);
        }

        if ((bucket_src->key == (char *)HASH_SLOT_FREE)
            || (bucket_src->key == (char *)HASH_SLOT_DELETED)) {
            continue;
        }

        src_idx = bucket_src->value;
        if (cecup.traversal[L].states[src_idx] != TRAVERSAL_STATE_PRESENT) {
            continue;
        }
        path_len = cecup.traversal[L].paths_lens[src_idx];

        if (!(hash_lookup_fs_map(cecup.traversal[R].map, bucket_src->key, path_len, &dst_idx))) {
            dst_idx = -1;
        }

        row_id = item_add(src_idx, dst_idx);
        item_get_actions_reasons(row_id, &action_src, &action_dst, &reason);

        if (!aux_is_root(bucket_src->key)
            && (action_src != ACTION_EQUAL) && (action_src != ACTION_IGNORE)) {
            HardLinks hard_links = {0};
            item_hardlink_side(row_id, L, &hard_links);

            while ((cecup.ntransfers + hard_links.count + 1) >= cecup.transfers_capacity) {
                int32 old_capacity = cecup.transfers_capacity;
                if (cecup.transfers_capacity == 0) {
                    cecup.transfers_capacity = INITIAL_CAPACITY;
                }
                cecup.transfers_capacity *= 2;
                cecup.transfers = realloc2(cecup.transfers,
                                           old_capacity, cecup.transfers_capacity,
                                           SIZEOF(*cecup.transfers));
                cecup.transfers_lens = realloc2(cecup.transfers_lens,
                                                old_capacity, cecup.transfers_capacity,
                                                SIZEOF(*cecup.transfers_lens));
            }

            if ((action_src == ACTION_HARDLINK) && (hard_links.count > 1)) {
                for (int32 j = 0; j < hard_links.count; j += 1) {
                    if (hash_insert_actions_set(cecup.actions_set,
                                                 hard_links.names[j], hard_links.names_lens[j])) {
                        cecup.transfers[cecup.ntransfers] = hard_links.names[j];
                        cecup.transfers_lens[cecup.ntransfers] = hard_links.names_lens[j];
                        cecup.ntransfers += 1;
                    }
                }
            }

            if (hash_insert_actions_set(cecup.actions_set, bucket_src->key, path_len)) {
                cecup.transfers[cecup.ntransfers] = bucket_src->key;
                cecup.transfers_lens[cecup.ntransfers] = path_len;
                cecup.ntransfers += 1;
            }
        }

        nfiles_processed += 1;
        if ((nfiles_total < 4096) || (nfiles_processed % 4096) == 0) {
            update_progress_bar((double)nfiles_processed / (double)nfiles_total);
        }
    }

    for (uint32 i = 0; i < cecup.traversal[R].map->capacity; i += 1) {
        Bucket_fs_map *bucket_dst = &(cecup.traversal[R].map->array[i]);
        int32 dst_idx;
        int32 src_idx;
        int32 path_len;

        if (work_should_stop()) {
            LOG_ERROR(_("Stop requested.\n"));
            work_preview_cancel_and_reset(thread_data);
        }

        if ((bucket_dst->key == (char *)HASH_SLOT_FREE)
            || (bucket_dst->key == (char *)HASH_SLOT_DELETED)) {
            continue;
        }

        dst_idx = bucket_dst->value;
        path_len = cecup.traversal[R].paths_lens[dst_idx];

        if (!hash_lookup_fs_map(cecup.traversal[L].map,
                                bucket_dst->key, path_len, &src_idx)) {
            if (!traversal_path_is_unknown(&cecup.traversal[L], bucket_dst->key, path_len)) {
                item_add(-1, dst_idx);
                if (cecup.delete_after) {
                    if ((cecup.ndeletions + 1) >= cecup.deletions_capacity) {
                        int32 old_capacity = cecup.deletions_capacity;
                        if (cecup.deletions_capacity <= 0) {
                            cecup.deletions_capacity = INITIAL_CAPACITY;
                        }
                        cecup.deletions_capacity *= 2;
                        cecup.deletions = realloc2(cecup.deletions,
                                                   old_capacity, cecup.deletions_capacity,
                                                   SIZEOF(*cecup.deletions));
                        cecup.deletions_lens = realloc2(cecup.deletions_lens,
                                                        old_capacity, cecup.deletions_capacity,
                                                        SIZEOF(*cecup.deletions_lens));
                    }

                    if (hash_insert_actions_set(cecup.actions_set,
                                                bucket_dst->key, path_len)) {
                        cecup.deletions[cecup.ndeletions] = bucket_dst->key;
                        cecup.deletions_lens[cecup.ndeletions] = path_len;
                        cecup.ndeletions += 1;
                    }
                }
            }
        }

        nfiles_processed += 1;
        if ((nfiles_total < 4096) || (nfiles_processed % 4096) == 0) {
            update_progress_bar((double)nfiles_processed / (double)nfiles_total);
        }
    }

    time_monotonic_precise(&t1_work);
    PRINT_TIMINGS(nfiles_total, t0_work, t1_work);
    work_finalize(thread_data, true);
}

#if 0 == TESTING_work
static inline void
work_functions_sink(void) {
    (void)work_cleanup;
    (void)work_finalize;
    (void)work_preview;
}
#endif

#if TESTING_work
#include "gtk_include.h"

#define CBASE_IMPLEMENT
#include "cbase.h"

typedef struct TestEntry {
    char *name;
    char *target;

    bool src_missing;
    bool src_dir;
    bool src_symlink;
    bool src_hardlink;

    bool dst_missing;
    bool dst_dir;
    bool dst_symlink;
    bool dst_hardlink;

    bool diff_size;
    bool diff_mtime_newer;
    bool diff_mtime_older;
    bool diff_perm;

    enum Action expected_src_action;
    enum Reason expected_reason_mask;
} TestEntry;

static void
create_test_file(char *path, char *content) {
    int32 fd;
    int32 len;

    if ((fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644)) < 0) {
        error("Error creating test file: %s\n", path);
    }
    len = strlen32(content);
    write64(fd, content, len);
    close(fd);
    return;
}

int
main(void) {
    char temp_dir[MAX_PATH_LENGTH];
    char src_dir[MAX_PATH_LENGTH];
    char dst_dir[MAX_PATH_LENGTH];
    IgnorePattern pattern;
    bool supports_symlink;
    static TestEntry test_entries[] = {
        {
            .name = "equal_file.txt",
            .expected_src_action = ACTION_EQUAL,
            .expected_reason_mask = REASON_EQUAL,
        },
        {
            .name = "new_file.txt",
            .dst_missing = true,
            .expected_src_action = ACTION_NEW,
            .expected_reason_mask = REASON_NEW,
        },
        {
            .name = "missing_file.txt",
            .src_missing = true,
            .expected_src_action = ACTION_IGNORE,
            .expected_reason_mask = REASON_MISSING,
        },
        {
            .name = "update_size.txt",
            .diff_size = true,
            .expected_src_action = ACTION_UPDATE,
            .expected_reason_mask = REASON_SIZE,
        },
        {
            .name = "update_mtime_newer.txt",
            .diff_mtime_newer = true,
            .expected_src_action = ACTION_UPDATE,
            .expected_reason_mask = REASON_MTIME_NEWER,
        },
        {
            .name = "update_mtime_older.txt",
            .diff_mtime_older = true,
            .expected_src_action = ACTION_IGNORE,
            .expected_reason_mask = REASON_MTIME_OLDER,
        },
        {
            .name = "update_perm.txt",
            .diff_perm = true,
            .expected_src_action = ACTION_UPDATE,
            .expected_reason_mask = REASON_PERM,
        },
        {
            .name = "equal_dir",
            .src_dir = true,
            .dst_dir = true,
            .expected_src_action = ACTION_EQUAL,
            .expected_reason_mask = REASON_EQUAL,
        },
        {
            .name = "new_dir",
            .src_dir = true,
            .dst_missing = true,
            .expected_src_action = ACTION_NEW,
            .expected_reason_mask = REASON_NEW,
        },
        {
            .name = "symlink_new",
            .target = "equal_file.txt",
            .src_symlink = true,
            .dst_missing = true,
            .expected_src_action = ACTION_SYMLINK,
            .expected_reason_mask = REASON_SYMLINK | REASON_NEW,
        },
        {
            .name = "symlink_equal",
            .target = "equal_file.txt",
            .src_symlink = true,
            .dst_symlink = true,
            .expected_src_action = ACTION_EQUAL,
            .expected_reason_mask = REASON_EQUAL,
        },
        {
            .name = "symlink_diff",
            .target = "equal_file.txt",
            .src_symlink = true,
            .dst_symlink = true,
            .diff_size = true,
            .expected_src_action = ACTION_SYMLINK,
            .expected_reason_mask = REASON_SYMLINK,
        },
        {
            .name = "ignored_file.txt",
            .expected_src_action = ACTION_IGNORE,
            .expected_reason_mask = REASON_IGNORED,
        },
    };

    if (!gtk_init_check()) {
        exit(EXIT_SUCCESS);
    }

    test_make_temp_dir(temp_dir, SIZEOF(temp_dir), "work");
    supports_symlink = test_symlink_supported(temp_dir);

    SNPRINTF(src_dir, "%s/src", temp_dir);
    SNPRINTF(dst_dir, "%s/dst", temp_dir);

    mkdir(src_dir, 0755);
    mkdir(dst_dir, 0755);

    for (int32 i = 0; i < LENGTH(test_entries); i += 1) {
        TestEntry *entry;
        char path_src[MAX_PATH_LENGTH];
        char path_dst[MAX_PATH_LENGTH];

        entry = &test_entries[i];
        if (!supports_symlink && (entry->src_symlink || entry->dst_symlink)) {
            continue;
        }

        SNPRINTF(path_src, "%s/%s", src_dir, entry->name);
        SNPRINTF(path_dst, "%s/%s", dst_dir, entry->name);

        if (!entry->src_missing) {
            if (entry->src_dir) {
                mkdir(path_src, 0755);
            } else if (entry->src_symlink) {
                symlink(entry->target, path_src);
            } else if (entry->src_hardlink) {
                char target_path[MAX_PATH_LENGTH];

                SNPRINTF(target_path, "%s/%s", src_dir, entry->target);
                if (link(target_path, path_src) < 0) {
                    error("Error linking %s to %s: %s.\n",
                          target_path, path_src, strerror(errno));
                }
            } else {
                create_test_file(path_src, "content");
            }
        }

        if (!entry->dst_missing) {
            if (entry->dst_dir) {
                mkdir(path_dst, 0755);
            } else if (entry->dst_symlink) {
                char *target;

                if (entry->diff_size) {
                    target = "wrong_target";
                } else {
                    target = entry->target;
                }

                symlink(target, path_dst);
            } else if (entry->dst_hardlink) {
                char target_path[MAX_PATH_LENGTH];
                char *target;

                if (entry->diff_size) {
                    target = "wrong_target";
                } else {
                    target = entry->target;
                }

                SNPRINTF(target_path, "%s/%s", dst_dir, target);
                if (link(target_path, path_dst) < 0) {
                    error("Error linking %s to %s: %s.\n",
                          target_path, path_dst, strerror(errno));
                }
            } else {
                if (entry->diff_size) {
                    create_test_file(path_dst, "content_different_size");
                } else {
                    create_test_file(path_dst, "content");
                }
            }
        }

        if (!entry->src_missing && !entry->src_symlink) {
            struct timeval tv[2];

            tv[0].tv_sec = 1600000000;
            tv[0].tv_usec = 0;
            tv[1].tv_sec = 1600000000;
            tv[1].tv_usec = 0;
            utimes(path_src, tv);

            if (entry->diff_perm) {
                chmod(path_src, 0777);
            }
        }

        if (!entry->dst_missing && !entry->dst_symlink) {
            struct timeval tv[2];

            tv[0].tv_sec = 1600000000;
            tv[0].tv_usec = 0;

            if (entry->diff_mtime_newer) {
                tv[1].tv_sec = 1500000000;
            } else if (entry->diff_mtime_older) {
                tv[1].tv_sec = 1700000000;
            } else {
                tv[1].tv_sec = 1600000000;
            }

            tv[1].tv_usec = 0;
            utimes(path_dst, tv);

            if (entry->diff_perm) {
                chmod(path_dst, 0644);
            }
        }
    }

    xpthread_mutex_init(&cecup.arena_mutex, NULL);
    cecup.arena = arena_create(SIZEMB(64), "cecup.arena");

    cecup.actions_set = hash_create_actions_set(1024, "cecup.actions_set");

    cecup.rows_capacity = INITIAL_CAPACITY;
    cecup.rows[L] = malloc2(cecup.rows_capacity * SIZEOF(*(cecup.rows[L])));
    cecup.rows[R] = malloc2(cecup.rows_capacity * SIZEOF(*(cecup.rows[R])));
    cecup.rows_selected = malloc2(cecup.rows_capacity * SIZEOF(*(cecup.rows_selected)));
    cecup.rows_visible = malloc2(cecup.rows_capacity * SIZEOF(*(cecup.rows_visible)));

    pattern.str = "ignored_file.txt";
    pattern.len = strlen32(pattern.str);
    pattern.match_str = pattern.str;
    pattern.dir_only = false;
    pattern.has_slash = false;

    cecup.ignore_patterns = &pattern;
    cecup.ignore_count = 1;

    cecup.base[L] = src_dir;
    cecup.base_len[L] = strlen32(src_dir);
    cecup.base[R] = dst_dir;
    cecup.base_len[R] = strlen32(dst_dir);

    traversal_allocate(&cecup.traversal[L], L);
    cecup.traversal[L].base_path = src_dir;
    cecup.traversal[L].base_path_len = strlen32(src_dir);

    traversal_allocate(&cecup.traversal[R], R);
    cecup.traversal[R].base_path = dst_dir;
    cecup.traversal[R].base_path_len = strlen32(dst_dir);

    work_traverse_fs(&cecup.traversal[L]);
    work_traverse_fs(&cecup.traversal[R]);

    for (int32 i = 0; i < LENGTH(test_entries); i += 1) {
        TestEntry *entry;
        int32 src_idx;
        int32 dst_idx;
        char expected_path[MAX_PATH_LENGTH];
        int32 name_len;
        int32 row_id;
        enum Action action_src;
        enum Action action_dst;
        enum Reason reason;

        entry = &test_entries[i];
        if (!supports_symlink && (entry->src_symlink || entry->dst_symlink)) {
            continue;
        }

        src_idx = -1;
        dst_idx = -1;

        name_len = strlen32(entry->name);
        memcpy64(expected_path, entry->name, name_len);
        if (entry->src_dir || entry->dst_dir) {
            expected_path[name_len] = '/';
            expected_path[name_len + 1] = '\0';
        } else {
            expected_path[name_len] = '\0';
        }

        for (int32 j = 0; j < cecup.traversal[L].nfiles; j += 1) {
            if (strequal(cecup.traversal[L].paths[j], expected_path)) {
                src_idx = j;
                break;
            }
        }

        for (int32 j = 0; j < cecup.traversal[R].nfiles; j += 1) {
            if (strequal(cecup.traversal[R].paths[j], expected_path)) {
                dst_idx = j;
                break;
            }
        }

        if (entry->src_missing) {
            ASSERT_EQUAL(src_idx, -1);
        } else {
            ASSERT_MORE_EQUAL(src_idx, 0);
        }

        if (entry->dst_missing) {
            ASSERT_EQUAL(dst_idx, -1);
        } else {
            ASSERT_MORE_EQUAL(dst_idx, 0);
        }

        row_id = item_add(src_idx, dst_idx);
        item_get_actions_reasons(row_id, &action_src, &action_dst, &reason);

        ASSERT(action_src == entry->expected_src_action);
        ASSERT((reason & entry->expected_reason_mask) == entry->expected_reason_mask);
    }

    {
        pthread_t pt_traverse;

        xpthread_create(&pt_traverse, NULL, work_traverse_fs_thread, &cecup.traversal[L]);
        xpthread_join(&pt_traverse, NULL);
        ASSERT_MORE(cecup.traversal[L].file_count, 0);
    }

    {
        ThreadData *thread_data = malloc2(SIZEOF(*thread_data));
        *thread_data = (ThreadData){0};

        cecup.ignore_patterns = NULL;
        cecup.ignore_count = 0;
        cecup.progress_bar = gtk_progress_bar_new();
        stop_working(false);
        work_thread_done_set(false);

        xpthread_create(&cecup.work_thread, NULL, work_preview, thread_data);
        xpthread_join(&cecup.work_thread, NULL);

        ASSERT_MORE(cecup.traversal[L].file_count, 0);
        ASSERT_MORE(cecup.traversal[R].file_count, 0);
    }

    {
        ThreadData *thread_data = malloc2(SIZEOF(*thread_data));
        *thread_data = (ThreadData){0};

        stop_working(true);
        work_thread_done_set(false);
        cecup.ntransfers = 42;

        xpthread_create(&cecup.work_thread, NULL, work_preview, thread_data);
        xpthread_join(&cecup.work_thread, NULL);

        ASSERT_EQUAL(cecup.ntransfers, 0);
    }

    if (cecup.transfers_capacity > 0) {
        free2(cecup.transfers, cecup.transfers_capacity * SIZEOF(*(cecup.transfers)));
        free2(cecup.transfers_lens, cecup.transfers_capacity * SIZEOF(*(cecup.transfers_lens)));
    }
    if (cecup.deletions_capacity > 0) {
        free2(cecup.deletions, cecup.deletions_capacity * SIZEOF(*(cecup.deletions)));
        free2(cecup.deletions_lens, cecup.deletions_capacity * SIZEOF(*(cecup.deletions_lens)));
    }
    if (cecup.actions_set != NULL) {
        hash_destroy_actions_set(cecup.actions_set);
    }

    traversal_free(&cecup.traversal[L]);
    traversal_free(&cecup.traversal[R]);

    if (cecup.rows_capacity > 0) {
        free2(cecup.rows[L], cecup.rows_capacity * SIZEOF(*(cecup.rows[L])));
        free2(cecup.rows[R], cecup.rows_capacity * SIZEOF(*(cecup.rows[R])));
        free2(cecup.rows_selected, cecup.rows_capacity * SIZEOF(uint8));
        free2(cecup.rows_visible,
              cecup.rows_capacity * SIZEOF(*(cecup.rows_visible)));
    }

    arena_destroy(cecup.arena);

    test_remove_tree(temp_dir);

    exit(EXIT_SUCCESS);
}

#endif

#endif /* WORK_C */
