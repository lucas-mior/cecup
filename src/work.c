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

#if !defined(WORK_C)
#define WORK_C

#include <gtk/gtk.h>
#include <ctype.h>
#include <sys/wait.h>
#include <dirent.h>
#include <poll.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <fts.h>
#include <pthread.h>

#include "cecup.h"
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

static void __attribute((noreturn))
work_finalize(bool preview_clean) {
    update_progress_bar(1.0);

    {
        Message *message = malloc2(SIZEOF(*message));
        memset64(message, 0, SIZEOF(*message));

        message->type = MSG_WORK_FINISHED;
        message->preview_clean = preview_clean;

        g_idle_add(update_ui_handler, message);
    }
    pthread_exit(NULL);
}

static int32
work_traverse_fs(Traversal *traversal) {
    int64 file_count = 0;
    int32 file_count_return = 0;
    char *paths[2];
    FTS *fts_handle;
    FTSENT *ent;
    struct timespec time_last_report = {0};

    if (cecup.stop_working) {
        return 0;
    }

    paths[0] = traversal->base_path;
    paths[1] = NULL;

    if ((fts_handle = fts_open(paths, FTS_PHYSICAL | FTS_NOCHDIR, NULL)) == NULL) {
        if (cecup.stop_working == false) {
            LOG_ERROR(_("Error walking directory %s: %s.\n"), paths[0], strerror(errno));
        }
        return 0;
    }

    while ((ent = fts_read(fts_handle))) {
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

        if (cecup.stop_working) {
            break;
        }

        switch (ent->fts_info) {
        case FTS_D:
            is_dir = true;
            break;
        case FTS_DC:
            continue;
        case FTS_DEFAULT:
            continue;
        case FTS_DOT:
            continue;
        case FTS_DP:
            continue;
        case FTS_ERR:
            LOG_ERROR(_("Error while traversing file system: %s.\n"), strerror(ent->fts_errno));
            continue;
        case FTS_DNR:
            LOG_ERROR(_("Directory '%s' is unreadable.\n"), ent->fts_path);
            continue;
        case FTS_NS:
            LOG_ERROR(_("Failed to get file information for %s: %s.\n"),
                      ent->fts_path, strerror(ent->fts_errno));
            continue;
        case FTS_F:
            break;
        case FTS_INIT:
            continue;
        case FTS_NSOK:
            continue;
        case FTS_SL:
            break;
        case FTS_SLNONE:
            continue;
        case FTS_W:
            continue;
        default:
            continue;
        }

        d_name = ent->fts_name;
        name_len = (int32)ent->fts_namelen;
        old_full_len = (int32)ent->fts_pathlen;

        if (old_full_len >= (MAX_PATH_LENGTH / 2)) {
            LOG_ERROR(_("Error: file path is too long:\n"));
            LOG_ERROR("%s\n", ent->fts_path);
            LOG_ERROR(_("Please fix your file system.\n"));
            stop_working(true);
            break;
        }

        if (name_len > 0) {
            if (isspace((uchar)d_name[0])) {
                LOG_ERROR(_("Error: there is a space in the start of the filename:\n"));
                LOG_ERROR("'%s'\n", ent->fts_path);
                LOG_ERROR(_("Please fix your file system.\n"));
                stop_working(true);
                break;
            }

            if (isspace((uchar)d_name[name_len - 1])) {
                LOG_ERROR(_("Error: there is space in the end of the filename:\n"));
                LOG_ERROR("'%s'\n", ent->fts_path);
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
                LOG_ERROR("'%s'\n", ent->fts_path);
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

        if (cecup.stop_working) {
            break;
        }

        {
            char *path_tmp;

            path_tmp = ent->fts_path + traversal->base_path_len;
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
                                            cecup.ignore_patterns, cecup.ignore_count);
            if (pattern) {
                matched_pattern = pattern->str;
                matched_pattern_len = pattern->len;
                if (is_dir) {
                    if (fts_set(fts_handle, ent, FTS_SKIP) < 0) {
                        error("Error in fts_set(FTS_SKIP): %s.\n", strerror(errno));
                        fatal(EXIT_FAILURE);
                    }
                }
            }
        }

        if ((ent->fts_info == FTS_SL) || (ent->fts_info == FTS_SLNONE)) {
            symlink_target_len = traversal_symlink_get(traversal, ent->fts_path, &symlink_target);
        }

        if ((ent->fts_info == FTS_F)
             && (ent->fts_statp->st_nlink > 1)
             && !matched_pattern) {
            traversal_add_link(traversal, *(ent->fts_statp), path, path_len);
        }

        traversal_push(traversal, ent->fts_statp,
                       path, path_len,
                       symlink_target, symlink_target_len,
                       matched_pattern, matched_pattern_len);

        if ((file_count % 4096) == 0) {
            struct timespec time_now;

            clock_gettime(CLOCK_MONOTONIC_COARSE, &time_now);
            if (((time_now.tv_sec - time_last_report.tv_sec) > 1)
                    || ((time_now.tv_nsec - time_last_report.tv_nsec) > 1000*1000*100)) {
                LOG("Found %lld files... %s\r", (llong)file_count, ent->fts_path);

                clock_gettime(CLOCK_MONOTONIC_COARSE, &time_last_report);
            }
        }
    }

    if (fts_close(fts_handle) < 0) {
        LOG_ERROR(_("Error in fts_close: %s.\n"), strerror(errno));
    }

    for (uint32 i = 0; i < traversal->inode_map->capacity; i += 1) {
        Bucket_inode_map *bucket = &traversal->inode_map->array[i];
        HardLinks hard_links = bucket->value;

        if (bucket->slot_state != HASH_SLOT_USED) {
            continue;
        }

        qsort(hard_links.names, (size_t)hard_links.count, SIZEOF(char *), compare_names);
        for (int32 k = 0; k < hard_links.count; k += 1) {
            // TODO: use better sorting method to avoid having to get the lengths again.
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

    hash_zero_transfer_set(cecup.transfer_set);
    hash_zero_deletion_set(cecup.deletion_set);
    cecup.ntransfers = 0;
    cecup.ndeletions = 0;

    cecup.rows_len = 0;

    xpthread_mutex_unlock(&cecup.arena_mutex);
    return;
}

static void __attribute((noreturn))
work_preview_cancel_and_reset(void) {
    work_cleanup();
    work_finalize(false);
}

static void *
work_preview(void *user_data) {
    int64 nfiles_total;
    int64 nfiles_processed = 0;
    bool same_fs;
    bool check_different_fs;
    struct timespec t0_work;
    struct timespec t1_work;

    (void)user_data;
    check_different_fs = gtk_check_button_get_active(GTK_CHECK_BUTTON(cecup.check_fs_button));

    update_progress_info(_("Analyzing changes"),
                         _("Traversing file systems for differences..."));

    clock_gettime(CLOCK_MONOTONIC_RAW, &t0_work);
    work_cleanup();

    {
        struct stat stat_src;
        struct stat stat_dst;

        if (stat(cecup.src_base, &stat_src) < 0) {
            LOG_ERROR(_("Error getting directory info from %s: %s.\n"),
                      cecup.src_base, strerror(errno));
            work_preview_cancel_and_reset();
        }
        if (stat(cecup.dst_base, &stat_dst) < 0) {
            LOG_ERROR(_("Error getting directory info from %s: %s.\n"),
                      cecup.dst_base, strerror(errno));
            work_preview_cancel_and_reset();
        }

        same_fs = (stat_src.st_dev == stat_dst.st_dev);
    }

    if (check_different_fs && same_fs) {
        LOG_ERROR(_("Safety stop: Original and backup are on the same storage device.\n"
                    "Check if the backup device is connected.\n"
                    "To force backup on a folder in the same device, uncheck"
                    " option \"Protect same drive sync\".\n"));

        work_preview_cancel_and_reset();
    }

    ignore_patterns_load();

    cecup.traversal[L].base_path = cecup.src_base;
    cecup.traversal[L].base_path_len = cecup.src_base_len;

    cecup.traversal[R].base_path = cecup.dst_base;
    cecup.traversal[R].base_path_len = cecup.dst_base_len;

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

    if (cecup.stop_working) {
        LOG_ERROR(_("Stop requested.\n"));
        work_preview_cancel_and_reset();
    }

    LOG(_("File system traversal finished.\n"));

    nfiles_total = cecup.traversal[L].file_count + cecup.traversal[R].file_count;
    LOG(_("Found %lld files to analyse...\n"), (llong)nfiles_total);

    for (uint32 i = 0; i < cecup.traversal[L].map->capacity; i += 1) {
        Bucket_fs_map *bucket_src = &(cecup.traversal[L].map->array[i]);
        int32 src_idx;
        int32 dst_idx;
        int32 path_len;
        int32 row_id;
        enum Action action_src;
        enum Action action_dst;
        enum Reason reason;

        if ((bucket_src->key == (char *)HASH_SLOT_FREE)
            || (bucket_src->key == (char *)HASH_SLOT_DELETED)) {
            continue;
        }

        src_idx = bucket_src->value;
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
                    if (hash_insert_transfer_set(cecup.transfer_set,
                                                 hard_links.names[j], hard_links.names_lens[j])) {
                        cecup.transfers[cecup.ntransfers] = hard_links.names[j];
                        cecup.transfers_lens[cecup.ntransfers] = hard_links.names_lens[j];
                        cecup.ntransfers += 1;
                    }
                }
            }

            if (hash_insert_transfer_set(cecup.transfer_set, bucket_src->key, path_len)) {
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

        if ((bucket_dst->key == (char *)HASH_SLOT_FREE)
            || (bucket_dst->key == (char *)HASH_SLOT_DELETED)) {
            continue;
        }

        dst_idx = bucket_dst->value;
        path_len = cecup.traversal[R].paths_lens[dst_idx];

        if (!hash_lookup_fs_map(cecup.traversal[L].map, bucket_dst->key, path_len, &src_idx)) {
            item_add(-1, dst_idx);
            if (cecup.delete_after) {
                if ((cecup.ndeletions + 1) >= cecup.deletions_capacity) {
                    int32 old_capacity = cecup.deletions_capacity;
                    if (cecup.deletions_capacity == 0) {
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

                if (hash_insert_deletion_set(cecup.deletion_set, bucket_dst->key, path_len)) {
                    cecup.deletions[cecup.ndeletions] = bucket_dst->key;
                    cecup.deletions_lens[cecup.ndeletions] = path_len;
                    cecup.ndeletions += 1;
                }
            }
        }

        nfiles_processed += 1;
        if ((nfiles_total < 4096) || (nfiles_processed % 4096) == 0) {
            update_progress_bar((double)nfiles_processed / (double)nfiles_total);
        }
    }

    clock_gettime(CLOCK_MONOTONIC_RAW, &t1_work);
    PRINT_TIMINGS(nfiles_total, t0_work, t1_work);
    work_finalize(true);
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

#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>

#include "assert.c"
#include "arena.c"

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
    char temp_dir[] = "/tmp/cecup_work_test_XXXXXX";
    char src_dir[MAX_PATH_LENGTH];
    char dst_dir[MAX_PATH_LENGTH];
    IgnorePattern pattern;
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

    ASSERT(mkdtemp(temp_dir));

    SNPRINTF(src_dir, "%s/src", temp_dir);
    SNPRINTF(dst_dir, "%s/dst", temp_dir);

    mkdir(src_dir, 0755);
    mkdir(dst_dir, 0755);

    for (int32 i = 0; i < LENGTH(test_entries); i += 1) {
        TestEntry *entry;
        char path_src[MAX_PATH_LENGTH];
        char path_dst[MAX_PATH_LENGTH];

        entry = &test_entries[i];
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

    memset64(&cecup, 0, SIZEOF(cecup));

    cecup.arena = arena_create(SIZEMB(64), "cecup.arena");
    pthread_mutex_init(&cecup.arena_mutex, NULL);

    cecup.transfer_set = hash_create_transfer_set(1024, "cecup.transfer_set");
    cecup.deletion_set = hash_create_deletion_set(1024, "cecup.deletion_set");

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

    cecup.src_base = src_dir;
    cecup.src_base_len = strlen32(src_dir);
    cecup.dst_base = dst_dir;
    cecup.dst_base_len = strlen32(dst_dir);

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
            if (strcmp(cecup.traversal[L].paths[j], expected_path) == 0) {
                src_idx = j;
                break;
            }
        }

        for (int32 j = 0; j < cecup.traversal[R].nfiles; j += 1) {
            if (strcmp(cecup.traversal[R].paths[j], expected_path) == 0) {
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
        cecup.ignore_patterns = NULL;
        cecup.ignore_count = 0;
        cecup.check_fs_button = gtk_check_button_new();
        cecup.progress_bar = gtk_progress_bar_new();
        cecup.stop_working = false;

        xpthread_create(&cecup.work_thread, NULL, work_preview, NULL);
        xpthread_join(&cecup.work_thread, NULL);

        ASSERT_MORE(cecup.traversal[L].file_count, 0);
        ASSERT_MORE(cecup.traversal[R].file_count, 0);
    }

    {
        cecup.stop_working = true;
        cecup.ntransfers = 42;

        xpthread_create(&cecup.work_thread, NULL, work_preview, NULL);
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
    if (cecup.transfer_set != NULL) {
        hash_destroy_transfer_set(cecup.transfer_set);
    }
    if (cecup.deletion_set != NULL) {
        hash_destroy_deletion_set(cecup.deletion_set);
    }

    traversal_free(&cecup.traversal[L]);
    traversal_free(&cecup.traversal[R]);

    if (cecup.rows_capacity > 0) {
        free2(cecup.rows[L], cecup.rows_capacity*SIZEOF(*(cecup.rows[L])));
        free2(cecup.rows[R], cecup.rows_capacity*SIZEOF(*(cecup.rows[R])));
        free2(cecup.rows_selected, cecup.rows_capacity*SIZEOF(uint8));
        free2(cecup.rows_visible, cecup.rows_capacity*SIZEOF(*(cecup.rows_visible)));
    }

    arena_destroy(cecup.arena);

    {
        char cmd[MAX_PATH_LENGTH];

        SNPRINTF(cmd, "rm -rf %s", temp_dir);
        system(cmd);
    }

    exit(EXIT_SUCCESS);
}

#endif

#endif /* WORK_C */
