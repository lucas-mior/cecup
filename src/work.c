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

#include <fts.h>
#include <gtk/gtk.h>
#include <ctype.h>
#include <sys/wait.h>
#include <dirent.h>
#include <poll.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>

#include "cecup.h"
#include "aux.c"
#include "util.c"
#include "update.c"
#include "ignore_patterns.c"
#include "work_rsync.c"

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_work 1
#elif !defined(TESTING_work)
#define TESTING_work 0
#endif

static void
work_finalize(bool preview_clean) {
    Message *message;

    message = xmalloc(SIZEOF(*message));
    memset64(message, 0, SIZEOF(*message));
    message->type = MSG_ENABLE_BUTTONS;
    message->preview_clean = preview_clean;

    update_progress_bar(MSG_PROGRESS, 1.0);

    g_idle_add(update_ui_handler, message);
    return;
}

static int32
work_traverse_fs(Traversal *traversal) {
    int64 file_count = 0;
    int32 file_count_return = 0;
    char *paths[2];
    FTS *fts_handle;
    FTSENT *ent;

    if (cecup.stop_working) {
        return 0;
    }

    paths[0] = traversal->base_path;
    paths[1] = NULL;

    if ((fts_handle = fts_open(paths, FTS_PHYSICAL | FTS_NOCHDIR, NULL)) == NULL) {
        if (cecup.stop_working == false) {
            error(_("Error walking directory %s: %s.\n"), traversal->base_path, strerror(errno));
        }
        return 0;
    }

    while ((ent = fts_read(fts_handle))) {
        char *d_name;
        int32 name_len;
        int32 old_full_len;
        char *path;
        int32 path_len;
        int32 is_dir;
        char *link_target;
        int32 link_target_len;
        char *matched_pattern;
        int32 matched_pattern_len;

        if (cecup.stop_working) {
            break;
        }

        if (ent->fts_info == FTS_DP || ent->fts_info == FTS_DOT) {
            continue;
        }

        if (ent->fts_info == FTS_ERR
            || ent->fts_info == FTS_NS
            || ent->fts_info == FTS_DNR) {
            continue;
        }

        d_name = ent->fts_name;
        name_len = (int32)ent->fts_namelen;
        old_full_len = (int32)ent->fts_pathlen;

        if (old_full_len >= (MAX_PATH_LENGTH / 2)) {
            LOG_ERROR(_("Error: file path is too long:\n"));
            LOG_ERROR("%s\n", ent->fts_path);
            LOG_ERROR(_("Please fix your file system.\n"));
            cecup.stop_working = true;
            break;
        }

        if (name_len > 0) {
            // TODO: Undefined behavior risk. `isspace()` expects an `int` representable as an
            // `unsigned char` or `EOF`. Passing a potentially negative `char` (like a non-ASCII
            // UTF-8 byte) causes UB on systems where `char` is signed. Cast to `(unsigned char)`
            // first.
            if (isspace((uchar)d_name[0])) {
                LOG_ERROR(_("Error: there is a space in the start of the fileneme:\n"));
                LOG_ERROR("'%s'\n", ent->fts_path);
                LOG_ERROR(_("Please fix your file system.\n"));
                cecup.stop_working = true;
                break;
            }

            if (isspace((uchar)d_name[name_len - 1])) {
                LOG_ERROR(_("Error: there is space in the end of the fileneme:\n"));
                LOG_ERROR("'%s'\n", ent->fts_path);
                LOG_ERROR(_("Please fix your file system.\n"));
                cecup.stop_working = true;
                break;
            }
        }

        for (int32 i = 0; i < LENGTH(problems); i += 1) {
            char *problem;
            int64 problem_len;

            problem = problems[i];
            problem_len = strlen32(problem);

            if (memmem64(d_name, name_len, problem, problem_len)) {
                LOG_ERROR(_("Error: filename contains problematic characters/patterns:\n"));
                LOG_ERROR("'%s'\n", ent->fts_path);
                LOG_ERROR(_("Please fix your file system.\n"));
                cecup.stop_working = true;
                break;
            }
        }

        if (ent->fts_info != FTS_D) {
            if (file_count >= MAXOF(file_count_return)) {
                LOG_ERROR("More than %lld files found.\n", MAXOF(file_count_return));
                LOG_ERROR("Please work in smaller subdirs.\n");
                cecup.stop_working = true;
            }
            file_count += 1;
        }

        if (cecup.stop_working) {
            break;
        }

        is_dir = (ent->fts_info == FTS_D);

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

            if (is_dir && (path[path_len] != '/')) {
                path_len += 1;
                path[path_len - 1] = '/';
                path[path_len] = '\0';
            }
        }

        link_target = NULL;
        link_target_len = 0;

        if ((ent->fts_info == FTS_SL) || (ent->fts_info == FTS_SLNONE)) {
            char target[MAX_PATH_LENGTH];
            int64 target_len;

            // TODO: Buffer overflow risk. `readlink` does not append a null terminator. If the link
            // target is exactly `MAX_PATH_LENGTH` bytes long, `target_len` will equal
            // `MAX_PATH_LENGTH`. The subsequent `target[target_len] = '\0'` will write one byte
            // past the end of the `target` array. Use `SIZEOF(target) - 1` instead.
            if ((target_len = readlink(ent->fts_path, target, SIZEOF(target) - 1)) < 0) {
                LOG_ERROR("Error in readlink(%s): %s.\n", ent->fts_path, strerror(errno));
            } else {
                target[target_len] = '\0';
                link_target = xarena_push(traversal->arena, target_len + 1);
                memcpy64(link_target, target, target_len + 1);
                link_target_len = (int32)target_len;
            }
        }

        if ((ent->fts_info == FTS_F) && (ent->fts_statp->st_nlink > 1)) {
            char inode_str[32];
            int32 n;
            int32 *first_idx_ptr;

            n = ITOA(inode_str, (long)ent->fts_statp->st_ino);
            if ((first_idx_ptr = hash_lookup_inode_map(traversal->inode_map, inode_str, n))) {
                int32 first_idx = *first_idx_ptr;

                link_target = traversal->paths[first_idx];
                link_target_len = traversal->paths_lens[first_idx];

                if (traversal->link_targets[first_idx] == NULL) {
                    traversal->link_targets[first_idx] = path;
                    traversal->link_targets_lens[first_idx] = (int16)path_len;
                }
                traversal->nlinks[first_idx] += 1;
            } else {
                hash_insert_inode_map(traversal->inode_map, inode_str, n, traversal->nfiles);
            }
        }

        matched_pattern = NULL;
        matched_pattern_len = 0;
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
                    }
                }
            }
        }

        traversal_push(traversal, ent->fts_statp,
                       path, path_len,
                       link_target, link_target_len,
                       matched_pattern, matched_pattern_len);
    }

    if (fts_close(fts_handle) < 0) {
        LOG_ERROR("Error in fts_close: %s.\n", strerror(errno));
    }

    for (int32 i = 0; i < traversal->nfiles; i += 1) {
        if (S_ISREG(traversal->stats[i].st_mode) && (traversal->stats[i].st_nlink > 1)) {
            char inode_str[64];
            int32 *first_idx_ptr;
            int32 n;

            n = ITOA(inode_str, (long)traversal->stats[i].st_ino);
            if ((first_idx_ptr = hash_lookup_inode_map(traversal->inode_map, inode_str, n))) {
                traversal->nlinks[i] = traversal->nlinks[*first_idx_ptr];
            }
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
work_traverse_clean(Traversal *traversal) {
    int32 capacity;

    capacity = traversal->ncapacity;

    free(traversal->stats, capacity*SIZEOF(*(traversal->stats)));
    free(traversal->patterns, capacity*SIZEOF(*(traversal->patterns)));
    free(traversal->link_targets, capacity*SIZEOF(*(traversal->link_targets)));
    free(traversal->paths, capacity*SIZEOF(*(traversal->paths)));

    free(traversal->paths_lens, capacity*SIZEOF(*(traversal->paths_lens)));
    free(traversal->link_targets_lens, capacity*SIZEOF(*(traversal->link_targets_lens)));
    free(traversal->patterns_lens, capacity*SIZEOF(*(traversal->patterns_lens)));
    free(traversal->nlinks, capacity*SIZEOF(*(traversal->nlinks)));
    free(traversal->row_ids, capacity*SIZEOF(*(traversal->row_ids)));

    arena_reset(traversal->arena);
    hash_zero_fs_map(traversal->map);
    hash_zero_inode_map(traversal->inode_map);

    {
        Arena *arena_save = traversal->arena;
        struct Hash_fs_map *map_save = traversal->map;
        struct Hash_inode_map *inode_map_save = traversal->inode_map;

        memset64(traversal, 0, SIZEOF(*traversal));

        traversal->arena = arena_save;
        traversal->map = map_save;
        traversal->inode_map = inode_map_save;
    }
    return;
}

static void
work_cleanup(void) {
    g_mutex_lock(&cecup.arena_mutex);

    work_traverse_clean(&cecup.traversal_src);
    work_traverse_clean(&cecup.traversal_dst);

    free(cecup.transfers, cecup.transfers_capacity*SIZEOF(*cecup.transfers));
    hash_zero_transfer_set(cecup.transfer_set);
    cecup.transfers = NULL;
    cecup.ntransfers = 0;
    cecup.transfers_capacity = 0;

    cecup.rows_len = 0;

    g_mutex_unlock(&cecup.arena_mutex);
    return;
}

static void __attribute__((noreturn))
work_preview_cancel_and_reset(void) {
    work_cleanup();
    work_finalize(false);
    g_thread_exit(NULL);
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
    check_different_fs = gtk_check_button_get_active(GTK_CHECK_BUTTON(cecup.check_fs));

    update_progress_state(_("Analyzing changes"),
                          _("Traversing file systems for differences..."));

    clock_gettime(CLOCK_MONOTONIC_RAW, &t0_work);
    work_cleanup();

    {
        struct stat stat_src;
        struct stat stat_dst;

        if (stat(cecup.src_base, &stat_src) < 0) {
            LOG_ERROR("Error getting directory info from %s: %s.\n",
                      cecup.src_base, strerror(errno));
            work_preview_cancel_and_reset();
        }
        if (stat(cecup.dst_base, &stat_dst) < 0) {
            LOG_ERROR("Error getting directory info from %s: %s.\n",
                      cecup.dst_base, strerror(errno));
            work_preview_cancel_and_reset();
        }

        same_fs = (stat_src.st_dev == stat_dst.st_dev);
    }

    if (check_different_fs && same_fs) {
        LOG_ERROR(
            _("Safety stop: Original and backup are on the same storage "
              "device.\n"
              "Check if the backup device is connected.\n"
              "To force backup on a folder in the same device, uncheck"
              " option \"Protect same drive sync\".\n"));

        work_preview_cancel_and_reset();
    }

    ignore_patterns_load();

    cecup.traversal_src.base_path = cecup.src_base;
    cecup.traversal_src.base_path_len = cecup.src_base_len;

    cecup.traversal_dst.base_path = cecup.dst_base;
    cecup.traversal_dst.base_path_len = cecup.dst_base_len;

    LOG(_("Traversing file systems...\n"));
    if (!same_fs) {
        GThread *t1;
        GThread *t2;

        t2 = g_thread_new("traversal_dst", work_traverse_fs_thread, &cecup.traversal_dst);
        t1 = g_thread_new("traversal_src", work_traverse_fs_thread, &cecup.traversal_src);

        g_thread_join(t1);
        g_thread_join(t2);
    } else {
        work_traverse_fs_thread(&cecup.traversal_src);
        work_traverse_fs_thread(&cecup.traversal_dst);
    }

    if (cecup.stop_working) {
        LOG_ERROR(_("Stop requested.\n"));
        work_preview_cancel_and_reset();
    }

    LOG(_("File system traversal finished.\n"));

    nfiles_total = cecup.traversal_src.file_count + cecup.traversal_dst.file_count;
    LOG(_("Found %lld files to analyse...\n"), (llong)nfiles_total);

    for (uint32 i = 0; i < cecup.traversal_src.map->capacity; i += 1) {
        Bucket_fs_map *bucket_src = &(cecup.traversal_src.map->array[i]);
        int32 src_idx;
        int32 *dst_idx_ptr;
        int32 dst_idx;
        char *link_target_src;
        int32 link_target_src_len;
        int32 path_len;
        int32 row_id;
        enum Action action_src;
        enum Action action_dst;
        enum Reason reason;

        if ((int64)bucket_src->key <= 0) {
            continue;
        }

        src_idx = bucket_src->value;
        link_target_src = cecup.traversal_src.link_targets[src_idx];
        link_target_src_len = cecup.traversal_src.link_targets_lens[src_idx];
        path_len = cecup.traversal_src.paths_lens[src_idx];

        if ((dst_idx_ptr = hash_lookup_fs_map(cecup.traversal_dst.map, bucket_src->key, path_len))) {
            dst_idx = *dst_idx_ptr;
        } else {
            dst_idx = -1;
        }

        row_id = item_add(src_idx, dst_idx);
        item_get_actions_reasons(row_id, &action_src, &action_dst, &reason);

        if (!aux_is_root(bucket_src->key)
            && (action_src != ACTION_EQUAL) && (action_src != ACTION_IGNORE)) {
            if (cecup.ntransfers >= (cecup.transfers_capacity - 1)) {
                int32 old_capacity = cecup.transfers_capacity;

                if (old_capacity <= 0) {
                    cecup.transfers_capacity = 256;
                } else {
                    cecup.transfers_capacity *= 2;
                }

                cecup.transfers = xrealloc2(cecup.transfers,
                                            old_capacity, cecup.transfers_capacity,
                                            SIZEOF(*cecup.transfers));
                cecup.transfers_lens = xrealloc2(cecup.transfers_lens,
                                                 old_capacity, cecup.transfers_capacity,
                                                 SIZEOF(*cecup.transfers_lens));
            }
            if (action_src == ACTION_HARDLINK) {
                if (hash_insert_transfer_set(cecup.transfer_set,
                                             link_target_src, link_target_src_len)) {
                    cecup.transfers[cecup.ntransfers] = link_target_src;
                    cecup.transfers_lens[cecup.ntransfers] = link_target_src_len;
                    cecup.ntransfers += 1;
                }
            }
            if (hash_insert_transfer_set(cecup.transfer_set, bucket_src->key, path_len)) {
                cecup.transfers[cecup.ntransfers] = bucket_src->key;
                cecup.transfers_lens[cecup.ntransfers] = path_len;
                cecup.ntransfers += 1;
            }
        }

        nfiles_processed += 1;
        if ((nfiles_processed % 1000) == 0) {
            update_progress_bar(MSG_PROGRESS,
                                (double)nfiles_processed / (double)nfiles_total);
        }
    }

    for (uint32 i = 0; i < cecup.traversal_dst.map->capacity; i += 1) {
        Bucket_fs_map *bucket_dst = &(cecup.traversal_dst.map->array[i]);
        int32 dst_idx;
        int32 path_len;

        if ((int64)bucket_dst->key <= 0) {
            continue;
        }

        dst_idx = bucket_dst->value;
        path_len = cecup.traversal_dst.paths_lens[dst_idx];

        if (hash_lookup_fs_map(cecup.traversal_src.map, bucket_dst->key, path_len) == NULL) {
            item_add(-1, dst_idx);
        }

        nfiles_processed += 1;
        if ((nfiles_processed % 1000) == 0) {
            update_progress_bar(MSG_PROGRESS, (double)nfiles_processed / (double)nfiles_total);
        }
    }

    clock_gettime(CLOCK_MONOTONIC_RAW, &t1_work);
    PRINT_TIMINGS(nfiles_total, t0_work, t1_work);
    work_finalize(true);
    g_thread_exit(NULL);
    return NULL;
}

#if TESTING_work

#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>

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
    char buf1[] = ">f+++++++++ some/file.txt";
    char buf2[] = "cd+++++++++ some/dir/";
    char buf3[] = ".f...p..... some/file.txt";
    char buf4[] = "invalid line";
    char temp_dir[] = "/tmp/cecup_work_test_XXXXXX";
    char src_dir[MAX_PATH_LENGTH];
    char dst_dir[MAX_PATH_LENGTH];
    char *parsed;
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

    (void)work_rsync;
    (void)work_preview;

    parsed = work_check_itemize_line(buf1);
    ASSERT(parsed);
    ASSERT_EQUAL(parsed, "some/file.txt");

    parsed = work_check_itemize_line(buf2);
    ASSERT(parsed);
    ASSERT_EQUAL(parsed, "some/dir/");

    parsed = work_check_itemize_line(buf3);
    ASSERT(parsed);
    ASSERT_EQUAL(parsed, "some/file.txt");

    parsed = work_check_itemize_line(buf4);
    ASSERT_NULL(parsed);

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
    g_mutex_init(&cecup.arena_mutex);

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

    cecup.traversal_src.arena = arena_create(1024*1024);
    cecup.traversal_src.map = hash_create_fs_map(1024);
    cecup.traversal_src.inode_map = hash_create_inode_map(1024);
    cecup.traversal_src.base_path = src_dir;
    cecup.traversal_src.base_path_len = strlen32(src_dir);

    cecup.traversal_dst.arena = arena_create(1024*1024);
    cecup.traversal_dst.map = hash_create_fs_map(1024);
    cecup.traversal_dst.inode_map = hash_create_inode_map(1024);
    cecup.traversal_dst.base_path = dst_dir;
    cecup.traversal_dst.base_path_len = strlen32(dst_dir);

    work_traverse_fs(&cecup.traversal_src);
    work_traverse_fs(&cecup.traversal_dst);

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

        for (int32 j = 0; j < cecup.traversal_src.nfiles; j += 1) {
            if (strcmp(cecup.traversal_src.paths[j], expected_path) == 0) {
                src_idx = j;
                break;
            }
        }

        for (int32 j = 0; j < cecup.traversal_dst.nfiles; j += 1) {
            if (strcmp(cecup.traversal_dst.paths[j], expected_path) == 0) {
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

    work_traverse_clean(&cecup.traversal_src);
    arena_destroy(cecup.traversal_src.arena);

    work_traverse_clean(&cecup.traversal_dst);
    arena_destroy(cecup.traversal_dst.arena);

    if (cecup.rows_capacity > 0) {
        free(cecup.rows_src, cecup.rows_capacity*SIZEOF(*(cecup.rows_src)));
        free(cecup.rows_dst, cecup.rows_capacity*SIZEOF(*(cecup.rows_dst)));
        free(cecup.rows_selected, cecup.rows_capacity*SIZEOF(uint8));
        free(cecup.rows_visible, cecup.rows_capacity*SIZEOF(*(cecup.rows_visible)));
    }
    
    g_mutex_clear(&cecup.arena_mutex);

    {
        char cmd[MAX_PATH_LENGTH];

        SNPRINTF(cmd, "rm -rf %s", temp_dir);
        system(cmd);
    }

    exit(EXIT_SUCCESS);
}

#endif

#endif /* WORK_C */
