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

#include "cecup.h"
#include "util.c"
#include "ipc.c"

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_work 1
#elif !defined(TESTING_work)
#define TESTING_work 0
#endif

typedef struct FixFsThreadData {
    char *base_path;
    int64 file_count;
} FixFsThreadData;

#define HASH_VALUE_TYPE char*
#define HASH_VALUE_FORMATTER "%s"
#define HASH_PADDING_TYPE uint32
#define HASH_DUPLICATE_KEYS 1
#define HASH_TYPE map
#include "hash.c"

static Message *add_row_batch_messages[BATCH_SIZE];
static int32 add_row_batch_count = 0;

static bool
work_check_reshowed_dir(struct Hash_map *show_patterns_map,
                        char *src_path, char *dst_path) {
    char *path;
    char **pattern_ptr;
    char *show_pattern;

    if (src_path) {
        path = src_path;
    } else {
        path = dst_path;
    }

    if (path) {
        pattern_ptr = hash_lookup2_map(show_patterns_map, path);
        if (pattern_ptr) {
            show_pattern = *pattern_ptr;
            return !strcmp(show_pattern, RSYNC_INCLUDE_DIRS);
        }
    }
    return false;
}

static void
work_flush_add_rows(void) {
    MessageBatch *batch;
    int64 messages_size;

    if (add_row_batch_count == 0) {
        return;
    }

    messages_size = add_row_batch_count*SIZEOF(Message *);
    batch = xmalloc(SIZEOF(*batch) + messages_size);
    batch->type = DATA_TYPE_ADD_ROW;
    batch->count = add_row_batch_count;
    memcpy64(batch->messages, add_row_batch_messages, messages_size);

    g_idle_add(update_ui_handler, batch);
    add_row_batch_count = 0;
    return;
}

static bool
work_get_file_info(char *base, char **path,
                   int64 *mtime, int64 *size, bool *is_dir) {
    char full_path[MAX_PATH_LENGTH];
    struct stat stat;

    SNPRINTF(full_path, "%s/%s", base, *path);

    if (lstat(full_path, &stat) < 0) {
        if (errno != ENOENT) {
            error("Error in lstat(%s): %s.\n", full_path, strerror(errno));
        }
        *mtime = 0;
        *path = NULL;
        return false;
    } else {
        *mtime = (int64)stat.st_mtime;
        if (size) {
            *size = (int64)stat.st_size;
        }
        *is_dir = S_ISDIR(stat.st_mode);
        return true;
    }
}

static char *
work_check_itemize_line(char *buf_output, int64 *parsed_size) {
    char *size_str;
    char *endptr;
    int64 size_val;

    switch (buf_output[0]) {
    case RSYNC_CHAR0_ACTION_SEND:
    case RSYNC_CHAR0_ACTION_RECEIVE:
    case RSYNC_CHAR0_ACTION_CHANGE:
    case RSYNC_CHAR0_ACTION_HARDLINK:
    case RSYNC_CHAR0_ACTION_NO_UPDATE:
        break;
    default:
        return NULL;
    }

    switch (buf_output[1]) {
    case RSYNC_CHAR1_TYPE_FILE:
    case RSYNC_CHAR1_TYPE_DIR:
    case RSYNC_CHAR1_TYPE_SYMLINK:
    case RSYNC_CHAR1_TYPE_DEVICE:
    case RSYNC_CHAR1_TYPE_SPECIAL:
        break;
    default:
        return NULL;
    }

    for (int32 i = 2; i < strlen32(RSYNC_ITEMIZE_PLACEHOLDERS); i += 1) {
        switch (buf_output[i]) {
        case RSYNC_CHAR_ATTR_NO_CHANGE:
        case RSYNC_CHAR_ATTR_ALL_SPACE_MEANS_ALL_UNCHANGED:
        case RSYNC_CHAR_ATTR_NEW:
        case RSYNC_CHAR_ATTR_UNKNOWN:
        case RSYNC_CHAR_ATTR_CHECKSUM:
        case RSYNC_CHAR_ATTR_SIZE:
        case RSYNC_CHAR_ATTR_TIME:
        case RSYNC_CHAR_ATTR_PERM:
        case RSYNC_CHAR_ATTR_OWNER:
        case RSYNC_CHAR_ATTR_GROUP:
        case RSYNC_CHAR_ATTR_ACL:
        case RSYNC_CHAR_ATTR_XATTR:
            break;
        default:
            return NULL;
        }
    }

    if (buf_output[strlen32(RSYNC_ITEMIZE_PLACEHOLDERS)] != ' ') {
        return NULL;
    }

    size_str = buf_output + strlen32(RSYNC_ITEMIZE_PLACEHOLDERS) + 1;
    while (*size_str == ' ') {
        size_str += 1;
    }
    size_val = (int64)strtoll(size_str, &endptr, 10);

    if ((endptr == size_str) || (*endptr != ' ')) {
        return NULL;
    }

    if (parsed_size) {
        *parsed_size = size_val;
    }

    return endptr + 1;
}

static void
work_finalize(ThreadData *thread_data) {
    Message *message = xmalloc(SIZEOF(*message));
    memset64(message, 0, SIZEOF(*message));
    message->type = DATA_TYPE_ENABLE_BUTTONS;

    work_flush_add_rows();

    ipc_send_progress(DATA_TYPE_PROGRESS_PREVIEW, 1.0);

    if (thread_data) {
        if (thread_data->filtered && thread_data->relative_new) {
            int32 focus_length = strlen32(thread_data->relative_new);

            message->focus_len = focus_length;
            message->path_to_focus = xmalloc(focus_length + 1);
            memcpy64(message->path_to_focus, thread_data->relative_new,
                     focus_length + 1);
        }

        if (thread_data->relative_new) {
            XFREE(thread_data->relative_new);
        }
        if (thread_data->relative_old) {
            XFREE(thread_data->relative_old);
        }
        XFREE(thread_data);
    }

    g_idle_add(update_ui_handler, message);
    return;
}

static bool
work_did_attribute_change(char *buf_output) {
    for (int32 i = 2; i < strlen32(RSYNC_ITEMIZE_PLACEHOLDERS); i += 1) {
        if ((buf_output[i] != '.') && (buf_output[i] != ' ')) {
            return true;
        }
    }
    return false;
}

static void
work_add_row(enum CecupAction action, enum CecupReason reason,
             char *src_path, char *dst_path,
             char *link_target, char *ignore_pattern,
             int32 path_len,
             int64 src_size_raw, int64 src_mtime_raw,
             int64 dst_size_raw, int64 dst_mtime_raw,
             bool delete_excluded, bool is_dir) {
    int32 target_len;
    int32 pattern_len;
    Message *message = xmalloc(SIZEOF(*message));
    memset64(message, 0, SIZEOF(*message));

    message->type = DATA_TYPE_ADD_ROW;
    message->action = action;
    message->reason = reason;
    message->path_len = path_len;
    message->src_size = src_size_raw;
    message->src_mtime = src_mtime_raw;
    message->dst_size = dst_size_raw;
    message->dst_mtime = dst_mtime_raw;
    message->delete_excluded = delete_excluded;
    message->is_dir = is_dir;

    if (src_path) {
        message->src_path = xmalloc(path_len + 1);
        memcpy64(message->src_path, src_path, path_len + 1);
    }

    if (dst_path) {
        message->dst_path = xmalloc(path_len + 1);
        memcpy64(message->dst_path, dst_path, path_len + 1);
    }

    if (link_target) {
        target_len = strlen32(link_target);
        message->link_target_len = target_len;
        message->link_target = xmalloc(target_len + 1);
        memcpy64(message->link_target, link_target, target_len + 1);
    }

    if (ignore_pattern) {
        pattern_len = strlen32(ignore_pattern);
        message->ignore_pattern_len = pattern_len;
        message->ignore_pattern = xmalloc(pattern_len + 1);
        memcpy64(message->ignore_pattern, ignore_pattern, pattern_len + 1);
    }

    add_row_batch_messages[add_row_batch_count] = message;
    add_row_batch_count += 1;

    if (add_row_batch_count >= BATCH_SIZE) {
        work_flush_add_rows();
    }
    return;
}

static int64
work_fix_fs_recursive(char *base_path, char *relative) {
    DIR *dir;
    struct dirent *entry;
    char full_path[MAX_PATH_LENGTH];
    char **name_list;
    int32 count = 0;
    int32 capacity = 1024;
    int64 total_files = 0;
    bool renaming_problematic = false;

    if (cecup.stop_working) {
        return 0;
    }

    if (relative) {
        SNPRINTF(full_path, "%s/%s", base_path, relative);
    } else {
        SNPRINTF(full_path, "%s", base_path);
    }

    if ((dir = opendir(full_path)) == NULL) {
        error(_("Error opening directory %s: %s.\n"), full_path,
              strerror(errno));
        error(_("Warning: Problematic file names will not be renamed.\n"));
        error(_("This is only a problem if you have problematic filenames.\n"));
        error(_(
            "Problematic filenames are the ones that contain the strings:\n"));
        for (int32 i = 0; i < LENGTH(replacements); i += 1) {
            error("\"%s\" ", replacements[i].problem);
        }
        error("\n");
        return 0;
    }

    name_list = xmalloc(capacity*SIZEOF(char *));

    while ((entry = readdir(dir))) {
        char *d_name = entry->d_name;

        if (d_name[0] == '.'
            && (d_name[1] == '\0' || (d_name[1] == '.' && d_name[2] == '\0'))) {
            continue;
        }

        if (count >= capacity) {
            capacity *= 2;
            name_list = xrealloc(name_list, capacity*SIZEOF(char *));
        }

        name_list[count] = xstrdup(d_name);
        count += 1;
    }

    closedir(dir);

    for (int32 i = 0; i < count; i += 1) {
        char *d_name = name_list[i];
        char sub_rel[MAX_PATH_LENGTH];
        char old_full[MAX_PATH_LENGTH];
        char new_full[MAX_PATH_LENGTH];
        char new_name[MAX_PATH_LENGTH];
        int32 old_full_len;
        struct stat stat;
        bool changed = false;
        int64 name_len = strlen32(d_name);

        if (relative) {
            SNPRINTF(sub_rel, "%s/%s", relative, d_name);
        } else {
            SNPRINTF(sub_rel, "%s", d_name);
        }

        old_full_len = SNPRINTF(old_full, "%s/%s", base_path, sub_rel);

        if (old_full_len >= (MAX_PATH_LENGTH / 2)) {
            IPC_SEND_LOG_ERROR(_("Error: file path is too long:\n"));
            IPC_SEND_LOG_ERROR("%s\n", old_full);
            IPC_SEND_LOG_ERROR(_("Please fix your file system.\n"));
            cecup.stop_working = true;
            return 0;
        }

        if (isspace(d_name[0])) {
            IPC_SEND_LOG_ERROR(_("Error: there is a space in the start of the fileneme:\n"));
            IPC_SEND_LOG_ERROR("'%s'\n", full_path);
            IPC_SEND_LOG_ERROR(_("Please fix your file system.\n"));
            cecup.stop_working = true;
            return 0;
        }
        if (isspace(d_name[name_len - 1])) {
            IPC_SEND_LOG_ERROR(_("Error: there is space in the end of the fileneme:\n"));
            IPC_SEND_LOG_ERROR("'%s'\n", full_path);
            IPC_SEND_LOG_ERROR(_("Please fix your file system.\n"));
            cecup.stop_working = true;
            return 0;
        }

        if (lstat(old_full, &stat) < 0) {
            error("Error in lstat(%s): %s.\n", old_full, strerror(errno));
            XFREE(d_name);
            continue;
        }

        if (renaming_problematic) {
            int32 j = 0;
            int32 k = 0;
            while (k < name_len) {
                char *earliest_match = NULL;
                int32 replacement_index = -1;

                for (int32 ri = 0; ri < LENGTH(replacements); ri += 1) {
                    char *search = replacements[ri].problem;
                    int64 search_len = strlen32(search);
                    char *match;

                    if ((match = memmem64(&d_name[k], name_len - k,
                                          search, search_len))) {
                        if (earliest_match == NULL || match < earliest_match) {
                            earliest_match = match;
                            replacement_index = ri;
                        }
                    }
                }

                if (earliest_match) {
                    int64 prefix_len = (int64)(earliest_match - &d_name[k]);
                    char *replace_str = replacements[replacement_index].rename;
                    int64 replace_len = strlen32(replace_str);

                    if (prefix_len > 0) {
                        memcpy64(&new_name[j], &d_name[k], prefix_len);
                        j += prefix_len;
                        k += prefix_len;
                    }

                    memcpy64(&new_name[j], replace_str, replace_len);

                    j += replace_len;
                    k += strlen32(replacements[replacement_index].problem);
                    changed = true;
                } else {
                    int64 remaining = name_len - k;
                    memcpy64(&new_name[j], &d_name[k], remaining);
                    j += remaining;
                    k += remaining;
                }
            }
            new_name[j] = '\0';

            if (changed) {
                if (relative && relative[0]) {
                    SNPRINTF(new_full,
                             "%s/%s/%s", base_path, relative, new_name);
                } else {
                    SNPRINTF(new_full, "%s/%s", base_path, new_name);
                }

                if (renameat2(AT_FDCWD, old_full,
                              AT_FDCWD, new_full, RENAME_NOREPLACE) < 0) {
                    IPC_SEND_LOG_ERROR(_("Error renaming %s to %s: %s\n"),
                                       old_full, new_full, strerror(errno));
                } else {
                    IPC_SEND_LOG(_("Fixed: %s -> %s\n"), d_name, new_name);
                    if (S_ISDIR(stat.st_mode)) {
                        if (relative) {
                            SNPRINTF(sub_rel, "%s/%s", relative, new_name);
                        } else {
                            SNPRINTF(sub_rel, "%s", new_name);
                        }
                    }
                }
            }
        }

        if (S_ISDIR(stat.st_mode)) {
            total_files += work_fix_fs_recursive(base_path, sub_rel);
        } else {
            total_files += 1;
        }
        XFREE(d_name);
    }

    XFREE(name_list);
    return total_files;
}

static void *
work_fix_fs_thread_fn(void *user_data) {
    FixFsThreadData *data = user_data;
    data->file_count = work_fix_fs_recursive(data->base_path, NULL);
    return NULL;
}

static void *
work_fix_fs_worker(void *user_data) {
    ThreadData *thread_data = user_data;
    struct stat stat_src;
    struct stat stat_dst;
    FixFsThreadData src_fix = {cecup.src_base, 0};
    FixFsThreadData dst_fix = {cecup.dst_base, 0};

    if (stat(cecup.src_base, &stat_src) < 0) {
        error("Error in stat(%s): %s.\n", cecup.src_base, strerror(errno));
        fatal(EXIT_FAILURE);
    }
    if (stat(cecup.dst_base, &stat_dst) < 0) {
        error("Error in stat(%s): %s.\n", cecup.dst_base, strerror(errno));
        fatal(EXIT_FAILURE);
    }

    IPC_SEND_LOG(_("Checking for problematic names...\n"));
    if (stat_src.st_dev == stat_dst.st_dev) {
        work_fix_fs_thread_fn(&src_fix);
        work_fix_fs_thread_fn(&dst_fix);
    } else {
        GThread *t1 = g_thread_new("fix_src", work_fix_fs_thread_fn, &src_fix);
        GThread *t2 = g_thread_new("fix_dst", work_fix_fs_thread_fn, &dst_fix);
        g_thread_join(t1);
        g_thread_join(t2);
    }
    IPC_SEND_LOG(_("Name correction finished.\n"));

    {
        Message *message = xmalloc(SIZEOF(*message));
        memset64(message, 0, SIZEOF(*message));

        message->type = DATA_TYPE_ENABLE_BUTTONS;
        g_idle_add(update_ui_handler, message);
    }

    XFREE(thread_data);
    g_thread_exit(NULL);
}

static void
work_rsync_parse_line(char *buf_output, int32 line_len, ThreadData *thread_data,
                      struct Hash_map *show_patterns_map,
                      int64 *nfiles_processed, int64 nfiles_total,
                      char ***transfers, int32 *ntransfers,
                      int32 *transfers_capacity) {
    char *link_target = NULL;
    char *ignore_pattern = NULL;
    char *show_pattern = NULL;
    char *interlude;
    char *src_path;
    char *dst_path;
    int32 path_len;
    int64 src_size = 0;
    int64 src_mtime = 0;
    int64 dst_size = 0;
    int64 dst_mtime = 0;
    int64 parsed_size = 0;
    enum CecupAction action;
    enum CecupReason reason;

    char *itemize_parsed;
    char action_char;
    char type_char;
    bool is_dir = false;
    bool ignore_duplicate_dir = false;

    bool is_preview = thread_data->is_preview;
    bool delete_excluded = thread_data->delete_excluded;
    bool filtered = thread_data->filtered;

    if (DEBUGGING && !RUNNING_ON_VALGRIND) {
        error("%s\n", buf_output);
    }

    itemize_parsed = work_check_itemize_line(buf_output, &parsed_size);
    action_char = buf_output[0];
    type_char = buf_output[1];

    if ((src_path = BEGINS_WITH(buf_output, RSYNC_SHOW_PRE_DIR))) {
        char buffer[MAX_PATH_LENGTH];
        path_len = line_len - (int32)(src_path - buf_output);
        interlude = memmem64(src_path, path_len,
                             RSYNC_IGNORE_INTER, strlen32(RSYNC_IGNORE_INTER));
        ASSERT(interlude);

        path_len -= (int32)(&buf_output[line_len] - interlude);
        *interlude = '\0';
        if (*(interlude - 1) != '/') {
            memcpy64(buffer, src_path, path_len + 1);
            path_len += 1;
            buffer[path_len - 1] = '/';
            buffer[path_len] = '\0';
        }
        src_path = buffer;

        show_pattern = interlude + strlen32(RSYNC_IGNORE_INTER);
        hash_insert2_map(show_patterns_map,
                         src_path, xstrdup(show_pattern));
        return;
    }

    if ((src_path = BEGINS_WITH(buf_output, RSYNC_IGNORE_PRE_FILE))
         || (src_path = BEGINS_WITH(buf_output, RSYNC_IGNORE_PRE_DIR))) {
        path_len = line_len - (int32)(src_path - buf_output);
        dst_path = src_path;

        interlude = memmem64(src_path,
                             path_len,
                             RSYNC_IGNORE_INTER,
                             strlen32(RSYNC_IGNORE_INTER));
        ASSERT(interlude);
        *interlude = '\0';
        path_len = path_len - (int32)(&buf_output[line_len] - interlude);
        ignore_pattern = interlude + strlen32(RSYNC_IGNORE_INTER);

        work_get_file_info(cecup.src_base, &src_path,
                           &src_mtime, &src_size, &is_dir);
        work_get_file_info(cecup.dst_base, &dst_path,
                           &dst_mtime, &dst_size, &is_dir);

        *nfiles_processed += 1;
        if (((*nfiles_processed % 1000) == 0) && (nfiles_total > 0)) {
            ipc_send_progress(DATA_TYPE_PROGRESS_PREVIEW,
                              (double)*nfiles_processed / (double)nfiles_total);
        }

        if (is_preview && strcmp(ignore_pattern, RSYNC_WILDCARD)) {
            work_add_row(ACTION_IGNORE, REASON_IGNORED,
                         src_path, dst_path, NULL, ignore_pattern,
                         path_len,
                         src_size, src_mtime, dst_size, dst_mtime,
                         delete_excluded, is_dir);
        }
        return;
    }

    if ((dst_path = BEGINS_WITH(buf_output, RSYNC_MESSAGE_DELETING))) {

        while (*dst_path == ' ') {
            dst_path += 1;
        }
        src_path = dst_path;
        path_len = line_len - (int32)(src_path - buf_output);

        if (work_get_file_info(cecup.src_base, &src_path,
                               &src_mtime, &src_size, &is_dir)) {
            reason = REASON_IGNORED;
        } else {
            reason = REASON_MISSING;
        }

        work_get_file_info(cecup.dst_base, &dst_path,
                           &dst_mtime, &dst_size, &is_dir);

        *nfiles_processed += 1;
        if (((*nfiles_processed % 1000) == 0) && (nfiles_total > 0)) {
            ipc_send_progress(DATA_TYPE_PROGRESS_PREVIEW,
                              (double)*nfiles_processed / (double)nfiles_total);
        }

        /* if reason is REASON_IGNORED, then the file will also be detected
         * with the pattern RSYNC_IGNORE_PRE_FILE,
         * so we skip it here to avoid duplicates */
        if (is_preview && (reason == REASON_MISSING)) {
            work_add_row(ACTION_DELETE, reason,
                         src_path, dst_path, NULL, NULL,
                         path_len,
                         src_size, src_mtime, dst_size, dst_mtime,
                         delete_excluded, is_dir);
        }
        return;
    }

    if (itemize_parsed
        && ((action_char == RSYNC_CHAR0_ACTION_RECEIVE)
            || (action_char == RSYNC_CHAR0_ACTION_HARDLINK)
            || (action_char == RSYNC_CHAR0_ACTION_CHANGE))) {

        src_path = itemize_parsed;
        while (*src_path == ' ') {
            src_path += 1;
        }
        dst_path = src_path;

        path_len = line_len - (int32)(src_path - buf_output);

        if (action_char == RSYNC_CHAR0_ACTION_HARDLINK) {
            char *sep;

            if ((sep = memmem64(src_path, path_len,
                                RSYNC_HARDLINK_NOTATION,
                                strlen32(RSYNC_HARDLINK_NOTATION)))) {
                link_target = sep + strlen32(RSYNC_HARDLINK_NOTATION);
                *sep = '\0';
                path_len -= (int32)(&buf_output[line_len] - sep);
            }

            action = ACTION_HARDLINK;
        } else if (type_char == RSYNC_CHAR1_TYPE_SYMLINK) {
            char *sep;

            if ((sep = memmem64(src_path, path_len,
                                RSYNC_SYMLINK_NOTATION,
                                strlen32(RSYNC_SYMLINK_NOTATION)))) {
                link_target = sep + strlen32(RSYNC_SYMLINK_NOTATION);
                *sep = '\0';
                path_len -= (int32)(&buf_output[line_len] - sep);
            }

            action = ACTION_SYMLINK;
        } else if (buf_output[2] == '+') {
            action = ACTION_NEW;
        } else {
            action = ACTION_UPDATE;
        }


        if (work_did_attribute_change(buf_output)) {
            reason = (enum CecupReason)action;
        } else {
            action = ACTION_EQUAL;
            reason = REASON_EQUAL;
        }

        if (!is_preview
            && ((type_char == RSYNC_CHAR1_TYPE_FILE)
                || (type_char == RSYNC_CHAR1_TYPE_SYMLINK))
            && ((action_char == RSYNC_CHAR0_ACTION_RECEIVE)
                || (action_char == RSYNC_CHAR0_ACTION_CHANGE)
                || (action_char == RSYNC_CHAR0_ACTION_HARDLINK))) {

            if (*ntransfers >= *transfers_capacity) {
                if (*transfers_capacity == 0) {
                    *transfers_capacity = 256;
                } else {
                    *transfers_capacity *= 2;
                }
                *transfers = xrealloc(
                    *transfers, (*transfers_capacity)*SIZEOF(**transfers));
            }
            (*transfers)[*ntransfers] = xstrdup(src_path);
            *ntransfers += 1;
        }

        work_get_file_info(cecup.src_base, &src_path,
                           &src_mtime, NULL, &is_dir);
        work_get_file_info(cecup.dst_base, &dst_path,
                           &dst_mtime, &dst_size, &is_dir);

        src_size = parsed_size;
        dst_size = parsed_size;

        if (filtered) {
            ignore_duplicate_dir = work_check_reshowed_dir(show_patterns_map,
                                                           src_path, dst_path);
        }

        *nfiles_processed += 1;
        if (((*nfiles_processed % 1000) == 0) && (nfiles_total > 0)) {
            ipc_send_progress(DATA_TYPE_PROGRESS_PREVIEW,
                              (double)*nfiles_processed / (double)nfiles_total);
        }

        if (!(filtered
              && ((src_path && !strcmp(src_path, "./"))
                  || ignore_duplicate_dir))) {
            if (is_preview) {
                work_add_row(action, reason,
                             src_path, dst_path, link_target, NULL,
                             path_len,
                             src_size, src_mtime, dst_size, dst_mtime,
                             delete_excluded, is_dir);
            }
        }
        return;
    }

    if (itemize_parsed) {
        action = ACTION_UPDATE;
        reason = REASON_UPDATE;

        if (!work_did_attribute_change(buf_output)) {
            action = ACTION_EQUAL;
            reason = REASON_EQUAL;
        }

        src_path = itemize_parsed;
        while (*src_path == ' ') {
            src_path += 1;
        }
        dst_path = src_path;
        path_len = line_len - (int32)(src_path - buf_output);

        if (action_char == RSYNC_CHAR0_ACTION_HARDLINK) {
            char *sep;

            if ((sep = memmem64(src_path, path_len,
                                RSYNC_HARDLINK_NOTATION,
                                strlen32(RSYNC_HARDLINK_NOTATION)))) {
                link_target = sep + strlen32(RSYNC_HARDLINK_NOTATION);
                *sep = '\0';
                path_len -= (int32)(&buf_output[line_len] - sep);
            }

        } else if (type_char == RSYNC_CHAR1_TYPE_SYMLINK) {
            char *sep;

            if ((sep = memmem64(src_path, path_len,
                                RSYNC_SYMLINK_NOTATION,
                                strlen32(RSYNC_SYMLINK_NOTATION)))) {
                link_target = sep + strlen32(RSYNC_SYMLINK_NOTATION);
                *sep = '\0';
                path_len -= (int32)(&buf_output[line_len] - sep);
            }
        }

        work_get_file_info(cecup.src_base, &src_path,
                           &src_mtime, NULL, &is_dir);
        work_get_file_info(cecup.dst_base, &dst_path,
                           &dst_mtime, &dst_size, &is_dir);

        src_size = parsed_size;
        dst_size = parsed_size;

        if (filtered) {
            ignore_duplicate_dir = work_check_reshowed_dir(show_patterns_map,
                                                           src_path, dst_path);
        }

        *nfiles_processed += 1;
        if (((*nfiles_processed % 1000) == 0) && (nfiles_total > 0)) {
            ipc_send_progress(DATA_TYPE_PROGRESS_PREVIEW,
                              (double)*nfiles_processed / (double)nfiles_total);
        }

        if (!(filtered
              && ((src_path && !strcmp(src_path, "./"))
                  || ignore_duplicate_dir))) {
            if (is_preview) {
                work_add_row(action, reason,
                             src_path, dst_path, link_target, NULL,
                             path_len,
                             src_size, src_mtime, dst_size, dst_mtime,
                             delete_excluded, is_dir);
            }
        }
    } else {
        if (DEBUGGING && !RUNNING_ON_VALGRIND) {
            error("Rsync output not parsed:\n");
            error("%s\n", buf_output);
        }
        return;
    }
    return;
}

static void *
work_rsync(void *user_data) {
    ThreadData *thread_data = user_data;
    int64 nfiles_total = 0;
    int64 nfiles_processed = 0;

    int32 pipe_stdout[2];
    int32 pipe_stderr[2];
    struct pollfd pipes[2];
    pid_t child_pid;

    int64 buf_output_pos = 0;
    char *eol;
    char buf_output[MAX_PATH_LENGTH*2];
    char buf_error[MAX_PATH_LENGTH*2];

    char src_base_with_slash[MAX_PATH_LENGTH];
    char dst_base_with_slash[MAX_PATH_LENGTH];
    char *rsync_args[64];
    int32 a = 0;
    char cmd[MAX_PATH_LENGTH*2];

    char **transfers = NULL;
    int32 ntransfers = 0;
    int32 transfers_capacity = 0;
    char files_from_filename[] = "/tmp/cecup_XXXXXX";

    char old_recursive[MAX_PATH_LENGTH];
    char new_recursive[MAX_PATH_LENGTH];

    struct stat stat_src;
    struct stat stat_dst;
    bool same_fs = true;
    struct Hash_map *show_patterns_map = hash_create_map(10);

    if (stat(cecup.src_base, &stat_src) < 0) {
        IPC_SEND_LOG_ERROR("Error getting directory info from %s: %s.\n",
                           cecup.src_base, strerror(errno));
        g_thread_exit(NULL);
    }
    if (stat(cecup.dst_base, &stat_dst) < 0) {
        IPC_SEND_LOG_ERROR("Error getting directory info from %s: %s.\n",
                           cecup.dst_base, strerror(errno));
        g_thread_exit(NULL);
    }

    same_fs = (stat_src.st_dev == stat_dst.st_dev);

    if (thread_data->check_different_fs && same_fs) {
        Message *message = xmalloc(SIZEOF(*message));
        memset64(message, 0, SIZEOF(*message));

        message->type = DATA_TYPE_CLEAR_TREES;
        g_idle_add_full(G_PRIORITY_HIGH_IDLE, update_ui_handler, message, NULL);

        IPC_SEND_LOG_ERROR(
            _("Safety stop: Original and backup are on the same storage "
              "device.\n"
              "Check if the backup device is connected.\n"
              "To force backup on a folder in the same device, uncheck"
              " option \"Protect same drive sync\".\n"));

        work_finalize(thread_data);
        g_thread_exit(NULL);
    }

    if (cecup.changed_dirs) {
        FixFsThreadData src_fix = {cecup.src_base, 0};
        FixFsThreadData dst_fix = {cecup.dst_base, 0};

        IPC_SEND_LOG(_("Checking for problematic names and counting files...\n"));

        if (!same_fs) {
            GThread *t1 = g_thread_new("fix_src",
                                       work_fix_fs_thread_fn, &src_fix);
            GThread *t2 = g_thread_new("fix_dst",
                                       work_fix_fs_thread_fn, &dst_fix);
            g_thread_join(t1);
            g_thread_join(t2);
        } else {
            work_fix_fs_thread_fn(&src_fix);
            work_fix_fs_thread_fn(&dst_fix);
        }

        if (cecup.stop_working) {
            IPC_SEND_LOG_ERROR(_("Stop requested.\n"));
            work_finalize(thread_data);
            g_thread_exit(NULL);
        }

        IPC_SEND_LOG(_("Name correction finished.\n"));

        nfiles_total = src_fix.file_count;
        IPC_SEND_LOG(_("Found %lld files to analyse...\n"),
                     (llong)nfiles_total);

        cecup.changed_dirs = false;
    }

    if (thread_data->is_preview && !thread_data->filtered) {
        Message *message = xmalloc(SIZEOF(*message));
        memset64(message, 0, SIZEOF(*message));

        message->type = DATA_TYPE_CLEAR_TREES;
        g_idle_add_full(G_PRIORITY_HIGH_IDLE,
                        update_ui_handler, message, NULL);
    }

    xpipe(pipe_stdout);
    xpipe(pipe_stderr);

    rsync_args[a++] = "rsync";
    rsync_args[a++] = "--verbose";
    rsync_args[a++] = "--verbose";
    rsync_args[a++] = "--update";
    rsync_args[a++] = "--recursive";
    rsync_args[a++] = "--partial";
    rsync_args[a++] = "--progress";
    rsync_args[a++] = "--info=progress2";
    rsync_args[a++] = "--links";
    rsync_args[a++] = "--hard-links";
    rsync_args[a++] = "--out-format=%i %l %n%L";
    rsync_args[a++] = "--perms";
    rsync_args[a++] = "--times";
    rsync_args[a++] = "--owner";
    rsync_args[a++] = "--group";
    rsync_args[a++] = "--iconv=.,.";

    if (thread_data->delete_excluded) {
        rsync_args[a++] = "--delete-excluded";
    }
    if (thread_data->delete_after) {
        rsync_args[a++] = "--delete-after";
    }
    if (thread_data->is_preview) {
        rsync_args[a++] = "--dry-run";
    }

    if (access(cecup.ignore_path, F_OK) != -1) {
            rsync_args[a++] = "--exclude-from";
            rsync_args[a++] = cecup.ignore_path;
    }
    if (thread_data->filtered) {
        char *relative_old = thread_data->relative_old;
        char *relative_new = thread_data->relative_new;
        int32 len_old = thread_data->len_old;
        int32 len_new = thread_data->len_new;

        rsync_args[a++] = "--include";
        rsync_args[a++] = relative_old;

        if (relative_old[len_old - 1] == '/') {
            SNPRINTF(old_recursive, "%s**", relative_old);
            rsync_args[a++] = "--include";
            rsync_args[a++] = old_recursive;
        }

        rsync_args[a++] = "--include";
        rsync_args[a++] = relative_new;

        if (relative_new[len_new - 1] == '/') {
            SNPRINTF(new_recursive, "%s**", relative_new);
            rsync_args[a++] = "--include";
            rsync_args[a++] = new_recursive;
        }

        rsync_args[a++] = "--include="RSYNC_INCLUDE_DIRS;
        rsync_args[a++] = "--exclude="RSYNC_WILDCARD;
    }

    SNPRINTF(src_base_with_slash, "%s/", cecup.src_base);
    SNPRINTF(dst_base_with_slash, "%s/", cecup.dst_base);
    rsync_args[a++] = src_base_with_slash;
    rsync_args[a++] = dst_base_with_slash;
    rsync_args[a++] = NULL;

    STRING_FROM_ARRAY(cmd, " ", rsync_args, a);
    IPC_SEND_LOG_CMD("%s\n", cmd);

    switch (child_pid = fork()) {
    case -1:
        error("Error forking: %s.\n", strerror(errno));
        exit(EXIT_FAILURE);
    case 0:
        if (setpgid(0, 0) < 0) {
            error("Error setpgid: %s.\n", strerror(errno));
            fatal(EXIT_FAILURE);
        }
        putenv("LC_ALL=C.UTF-8");

        XCLOSE(&pipe_stdout[0]);
        XCLOSE(&pipe_stderr[0]);

        xdup2(pipe_stdout[1], STDOUT_FILENO);
        xdup2(pipe_stderr[1], STDERR_FILENO);

        XCLOSE(&pipe_stderr[1]);
        XCLOSE(&pipe_stdout[1]);

        execvp(rsync_args[0], rsync_args);
        error("Error executing\n%s\n%s.\n", cmd, strerror(errno));
        _exit(EXIT_FAILURE);
    default:
        cecup.child_pid = child_pid;
        XCLOSE(&pipe_stderr[1]);
        XCLOSE(&pipe_stdout[1]);
        break;
    }

    pipes[0].fd = pipe_stdout[0];
    pipes[1].fd = pipe_stderr[0];
    pipes[0].events = POLLIN;
    pipes[1].events = POLLIN;

    do {
        int64 r;

        pipes[0].revents = 0;
        pipes[1].revents = 0;

        switch (poll(pipes, 2, -1)) {
        case -1:
            if (errno != EINTR) {
                error("Error in poll: %s.\n", strerror(errno));
                fatal(EXIT_FAILURE);
            }
            continue;
        case 0:
            continue;
        default:
            break;
        }

        if (pipes[0].revents & POLLERR) {
            pipes[0].fd = -1;
            goto read_error_pipe;
        }
        if (pipes[0].revents & POLLHUP) {
            pipes[0].fd = -1;
        }
        if (!(pipes[0].revents & POLLIN)) {
            goto read_error_pipe;
        }

        if (DEBUGGING) {
            memset64(buf_output, 0, SIZEOF(buf_output));
        }
        r = read64(pipe_stdout[0], buf_output + buf_output_pos,
                   SIZEOF(buf_output) - buf_output_pos - 1);
        if (r <= 0) {
            if (r < 0) {
                IPC_SEND_LOG_ERROR("Error reading stdout pipe: %s.\n",
                                   strerror(errno));
                pipes[0].fd = -1;
            }
            goto read_error_pipe;
        }
        buf_output_pos += (int64)r;

        while ((eol = memchr64(buf_output, '\n', buf_output_pos))
                || (eol = memchr64(buf_output, '\r', buf_output_pos))) {
            int64 line_len = (int64)(eol - buf_output);
            int64 remaining;

            *eol = '\0';
            work_rsync_parse_line(buf_output, (int32)line_len, thread_data,
                                  show_patterns_map,
                                  &nfiles_processed, nfiles_total,
                                  &transfers, &ntransfers, &transfers_capacity);

            remaining = buf_output_pos - (line_len + 1);
            if (remaining > 0) {
                memmove64(buf_output, eol + 1, remaining);
            }
            buf_output_pos = remaining;
            if (buf_output_pos <= 0) {
                break;
            }
        }

        if (buf_output_pos >= (SIZEOF(buf_output) - 1)) {
            buf_output[buf_output_pos] = '\0';
            IPC_SEND_LOG("%s\n", buf_output);
            buf_output_pos = 0;
        }

    read_error_pipe:
        if (pipes[1].revents & POLLERR) {
            pipes[1].fd = -1;
            continue;
        }
        if (pipes[1].revents & POLLHUP) {
            pipes[1].fd = -1;
        }
        if (!(pipes[1].revents & POLLIN)) {
            continue;
        }

        r = read64(pipe_stderr[0], buf_error, SIZEOF(buf_error));
        if (r <= 0) {
            if (r < 0) {
                IPC_SEND_LOG_ERROR("Error reading stderr pipe: %s.\n",
                                   strerror(errno));
                pipes[1].fd = -1;
            }
            continue;
        }
        buf_error[r] = '\0';
        IPC_SEND_LOG_ERROR("%s", buf_error);

    } while ((pipes[0].fd >= 0) || (pipes[1].fd >= 0));

    if (waitpid(child_pid, NULL, 0) < 0) {
        IPC_SEND_LOG_ERROR("Error waiting for child: %s.\n", strerror(errno));
    }
    cecup.child_pid = 0;

    XCLOSE(&pipe_stderr[0]);
    XCLOSE(&pipe_stdout[0]);

    if (cecup.stop_working) {
        IPC_SEND_LOG_ERROR(_("Stop requested.\n"));
        for (int32 i = 0; i < ntransfers; i += 1) {
            XFREE(transfers[i]);
        }
        XFREE(transfers);
        work_finalize(thread_data);
        g_thread_exit(NULL);
    }

    if (ntransfers <= 0) {
        for (uint32 i = 0; i < hash_length(show_patterns_map); i += 1) {
            XFREE(show_patterns_map->array[i].value);
        }
        hash_destroy_map(show_patterns_map);
        work_finalize(thread_data);
        g_thread_exit(NULL);
    }

    {
        int files_from_fd;
        if ((files_from_fd = mkstemp(files_from_filename)) < 0) {
            error("Error in mkstemp: %s.\n", strerror(errno));
            fatal(EXIT_FAILURE);
        }
        for (int32 i = 0; i < ntransfers; i += 1) {
            char *file = transfers[i];
            int64 w;
            int64 written = 0;
            int64 left = strlen32(file);

            while ((w = write64(files_from_fd, &file[written], left)) > 0) {
                written += w;
                left -= w;
                if (left <= 0) {
                    break;
                }
            }
            if (w < 0) {
                error("Error writing to %s: %s.\n",
                      files_from_filename, strerror(errno));
                fatal(EXIT_FAILURE);
            }

            write64(files_from_fd, "\n", 1);
        }
        XCLOSE(&files_from_fd);
    }

    a = 0;
    rsync_args[a++] = "rsync";
    rsync_args[a++] = "--verbose";
    rsync_args[a++] = "--recursive";
    rsync_args[a++] = "--partial";
    rsync_args[a++] = "--progress";
    rsync_args[a++] = "--info=progress2";
    rsync_args[a++] = "--checksum";
    rsync_args[a++] = "--perms";
    rsync_args[a++] = "--times";
    rsync_args[a++] = "--owner";
    rsync_args[a++] = "--group";
    rsync_args[a++] = "--files-from";
    rsync_args[a++] = files_from_filename;
    rsync_args[a++] = "--iconv=.,.";
    rsync_args[a++] = src_base_with_slash;
    rsync_args[a++] = dst_base_with_slash;
    rsync_args[a++] = NULL;

    IPC_SEND_LOG(_("Verifying transfers with checksum...\n"));
    STRING_FROM_ARRAY(cmd, " ", rsync_args, a);
    IPC_SEND_LOG_CMD("%s\n", cmd);

    xpipe(pipe_stdout);
    xpipe(pipe_stderr);

    switch (child_pid = fork()) {
    case -1:
        error("Error forking: %s.\n", strerror(errno));
        fatal(EXIT_FAILURE);
    case 0:
        setpgid(0, 0);
        putenv("LC_ALL=C.UTF-8");

        XCLOSE(&pipe_stderr[0]);
        XCLOSE(&pipe_stdout[0]);

        xdup2(pipe_stdout[1], STDOUT_FILENO);
        xdup2(pipe_stderr[1], STDERR_FILENO);

        XCLOSE(&pipe_stderr[1]);
        XCLOSE(&pipe_stdout[1]);

        execvp("rsync", rsync_args);
        _exit(EXIT_FAILURE);
    default:
        cecup.child_pid = child_pid;
        XCLOSE(&pipe_stderr[1]);
        XCLOSE(&pipe_stdout[1]);
        break;
    }

    pipes[0].fd = pipe_stdout[0];
    pipes[1].fd = pipe_stderr[0];
    pipes[0].events = POLLIN;
    pipes[1].events = POLLIN;

    do {
        int64 r;

        pipes[0].revents = 0;
        pipes[1].revents = 0;

        switch (poll(pipes, 2, -1)) {
        case -1:
            if (errno != EINTR) {
                error("Error in poll: %s.\n", strerror(errno));
                fatal(EXIT_FAILURE);
            }
            continue;
        case 0:
            continue;
        default:
            break;
        }

        if (pipes[0].revents & POLLERR) {
            pipes[0].fd = -1;
            goto read_error_pipe2;
        }
        if (pipes[0].revents & POLLHUP) {
            pipes[0].fd = -1;
        }
        if (!(pipes[0].revents & POLLIN)) {
            goto read_error_pipe2;
        }

        r = read64(pipe_stdout[0], buf_output, SIZEOF(buf_output));
        if (r <= 0) {
            if (r < 0) {
                IPC_SEND_LOG_ERROR("Error reading stdout pipe: %s.\n",
                                   strerror(errno));
                pipes[1].fd = -1;
            }
            goto read_error_pipe2;
        }
        if (DEBUGGING) {
            IPC_SEND_LOG("%s", buf_output);
        }

    read_error_pipe2:
        if (pipes[1].revents & POLLERR) {
            pipes[1].fd = -1;
            continue;
        }
        if (pipes[1].revents & POLLHUP) {
            pipes[1].fd = -1;
        }
        if (!(pipes[1].revents & POLLIN)) {
            continue;
        }

        r = read64(pipe_stderr[0], buf_error, SIZEOF(buf_error));
        if (r <= 0) {
            if (r < 0) {
                IPC_SEND_LOG_ERROR("Error reading stderr pipe: %s.\n",
                                   strerror(errno));
                pipes[1].fd = -1;
            }
            continue;
        }
        buf_error[r] = '\0';
        IPC_SEND_LOG_ERROR("%s", buf_error);

    } while ((pipes[0].fd >= 0) || (pipes[1].fd >= 0));

    if (waitpid(child_pid, NULL, 0) < 0) {
        IPC_SEND_LOG_ERROR("Error waiting for rsync: %s.\n", strerror(errno));
    }
    xunlink(files_from_filename);
    cecup.child_pid = 0;
    XCLOSE(&pipe_stderr[0]);
    XCLOSE(&pipe_stdout[0]);

    for (int32 i = 0; i < ntransfers; i += 1) {
        XFREE(transfers[i]);
    }
    XFREE(transfers);

    if (thread_data->is_preview) {
        IPC_SEND_LOG(_("Analysis complete. Review the list and click Apply.\n"));
    }

    for (uint32 i = 0; i < hash_length(show_patterns_map); i += 1) {
        XFREE(show_patterns_map->array[i].value);
    }
    hash_destroy_map(show_patterns_map);
    work_finalize(thread_data);
    g_thread_exit(NULL);
}

static void *
work_rsync_bulk(void *user_data) {
    TaskList *tasks = user_data;
    bool has_transfers = false;
    int32 pipe_stdout[2];
    int32 pipe_stderr[2];
    struct pollfd pipes[2];
    pid_t child_pid;
    char dst_base_with_slash[MAX_PATH_LENGTH];
    char *rsync_args[32];
    int32 a = 0;
    int32 buf_output_pos = 0;
    char files_from_filename[] = "/tmp/cecup_XXXXXX";
    int files_from_fd;
    char cmd[MAX_PATH_LENGTH*2];

    for (int32 i = 0; i < tasks->count; i += 1) {
        Task *task = tasks->items[i];
        char full_path[MAX_PATH_LENGTH];
        pid_t child_rm;
        int child_status;
        bool removed = false;

        if (task->action != ACTION_DELETE) {
            has_transfers = true;
            continue;
        }

        if (cecup.stop_working) {
            IPC_SEND_LOG_ERROR(_("Stop requested.\n"));
            free_task_list(tasks);
            work_finalize(NULL);
            g_thread_exit(NULL);
        }

        if (task->side == L) {
            SNPRINTF(full_path, "%s/%s", cecup.src_base, task->path);
        } else {
            SNPRINTF(full_path, "%s/%s", cecup.dst_base, task->path);
        }
        switch (child_rm = fork()) {
        case -1:
            error("Error forking: %s.\n", strerror(errno));
            fatal(EXIT_FAILURE);
        case 0: {
            char cmd_rm[MAX_PATH_LENGTH];
            char *args_rm[] = {
                "rm",
                "-rf",
                full_path,
                NULL,
            };

            STRING_FROM_ARRAY(cmd_rm, " ", args_rm, LENGTH(args_rm));

            execvp(args_rm[0], args_rm);
            error("Error executing\n%s\n%s.\n", cmd_rm, strerror(errno));
            _exit(EXIT_FAILURE);
        }
        default:
            cecup.child_pid = child_rm;
            if (waitpid(child_rm, &child_status, 0) < 0) {
                IPC_SEND_LOG_ERROR("Error waiting for child: %s.\n",
                                   strerror(errno));
            } else if (WIFEXITED(child_status)) {
                removed = !WEXITSTATUS(child_status);
            }
            cecup.child_pid = 0;
            break;
        }

        if (removed) {
            Message *message = xmalloc(SIZEOF(*message));
            memset64(message, 0, SIZEOF(*message));

            message->path_len = task->path_len;
            message->src_path = xmalloc(task->path_len + 1);
            memcpy64(message->src_path, task->path, task->path_len + 1);

            message->type = DATA_TYPE_REMOVE_ROW;
            g_idle_add(update_ui_handler, message);
        }
    }

    if (!has_transfers) {
        work_finalize(NULL);
        free_task_list(tasks);
        g_thread_exit(NULL);
    }

    xpipe(pipe_stdout);
    xpipe(pipe_stderr);

    SNPRINTF(dst_base_with_slash, "%s/", cecup.dst_base);

    if ((files_from_fd = mkstemp(files_from_filename)) < 0) {
        error("Error in mkstemp: %s.\n", strerror(errno));
        fatal(EXIT_FAILURE);
    }

    for (int32 i = 0; i < tasks->count; i += 1) {
        Task *task = tasks->items[i];
        switch (task->action) {
        case ACTION_DELETE:
        case ACTION_DELETED:
        case ACTION_IGNORE:
        case ACTION_EQUAL:
            continue;
        case ACTION_HARDLINK:
            write64(files_from_fd, task->link_target, task->link_target_len);
            write64(files_from_fd, "\n", 1);
            __attribute__((fallthrough));
        case ACTION_NEW:
        case ACTION_UPDATE:
        case ACTION_SYMLINK:
        case ACTION_LAST:
        default:
            write64(files_from_fd, task->path, task->path_len);
            write64(files_from_fd, "\n", 1);
        }
    }
    XCLOSE(&files_from_fd);

    rsync_args[a++] = "rsync";
    rsync_args[a++] = "--verbose";
    rsync_args[a++] = "--update";
    rsync_args[a++] = "--checksum";
    rsync_args[a++] = "--recursive";
    rsync_args[a++] = "--partial";
    rsync_args[a++] = "--progress";
    rsync_args[a++] = "--info=progress2";
    rsync_args[a++] = "--links";
    rsync_args[a++] = "--hard-links";
    rsync_args[a++] = "--out-format=%i %l %n%L";
    rsync_args[a++] = "--perms";
    rsync_args[a++] = "--times";
    rsync_args[a++] = "--owner";
    rsync_args[a++] = "--group";
    rsync_args[a++] = "--files-from";
    rsync_args[a++] = files_from_filename;
    rsync_args[a++] = "--iconv=.,.";
    rsync_args[a++] = cecup.src_base;
    rsync_args[a++] = dst_base_with_slash;
    rsync_args[a++] = NULL;

    STRING_FROM_ARRAY(cmd, " ", rsync_args, a);
    IPC_SEND_LOG_CMD("%s\n", cmd);

    switch (child_pid = fork()) {
    case -1:
        error("Error forking: %s.\n", strerror(errno));
        fatal(EXIT_FAILURE);
    case 0:
        if (setpgid(0, 0) < 0) {
            error("Error setpgid: %s.\n", strerror(errno));
            fatal(EXIT_FAILURE);
        }
        putenv("LC_ALL=C.UTF-8");

        XCLOSE(&pipe_stderr[0]);
        XCLOSE(&pipe_stdout[0]);

        xdup2(pipe_stdout[1], STDOUT_FILENO);
        xdup2(pipe_stderr[1], STDERR_FILENO);

        XCLOSE(&pipe_stderr[1]);
        XCLOSE(&pipe_stdout[1]);

        execvp(rsync_args[0], rsync_args);
        error("Error executing\n%s\n%s.\n", cmd, strerror(errno));
        _exit(EXIT_FAILURE);
    default:
        cecup.child_pid = child_pid;
        XCLOSE(&pipe_stderr[1]);
        XCLOSE(&pipe_stdout[1]);
        break;
    }

    pipes[0].fd = pipe_stdout[0];
    pipes[1].fd = pipe_stderr[0];
    pipes[0].events = POLLIN;
    pipes[1].events = POLLIN;

    do {
        int64 r;
        char *eol;
        char buf_output_bulk[MAX_PATH_LENGTH*2];
        char buf_error_bulk[MAX_PATH_LENGTH*2];

        pipes[0].revents = 0;
        pipes[1].revents = 0;

        switch (poll(pipes, 2, -1)) {
        case -1:
            if (errno != EINTR) {
                error("Error in poll: %s.\n", strerror(errno));
                fatal(EXIT_FAILURE);
            }
            continue;
        case 0:
            continue;
        default:
            break;
        }

        if (pipes[0].revents & POLLERR) {
            pipes[0].fd = -1;
            goto read_error_pipe;
        }
        if (pipes[0].revents & POLLHUP) {
            pipes[0].fd = -1;
        }
        if (!(pipes[0].revents & POLLIN)) {
            goto read_error_pipe;
        }

        r = read64(pipe_stdout[0], buf_output_bulk + buf_output_pos,
                   SIZEOF(buf_output_bulk) - 1 - buf_output_pos);
        if (r <= 0) {
            if (r < 0) {
                IPC_SEND_LOG_ERROR("Error reading stdout pipe: %s.\n",
                                   strerror(errno));
                pipes[0].fd = -1;
            }
            goto read_error_pipe;
        }
        buf_output_pos += (int32)r;

        while ((eol = memchr64(buf_output_bulk, '\n', buf_output_pos))
               || (eol = memchr64(buf_output_bulk, '\r', buf_output_pos))) {
            int32 line_len = (int32)(eol - buf_output_bulk);
            int32 remaining;
            char *filename;

            *eol = '\0';

            IPC_SEND_LOG("%s\n", buf_output_bulk);

            if ((filename = work_check_itemize_line(buf_output_bulk, NULL))) {
                int32 path_len;
                char *sep;
                Message *message = xmalloc(SIZEOF(*message));
                memset64(message, 0, SIZEOF(*message));

                while (*filename == ' ') {
                    filename += 1;
                }

                path_len = (int32)(eol - filename);

                if ((sep = memmem64(filename, path_len,
                                    RSYNC_HARDLINK_NOTATION,
                                    strlen32(RSYNC_HARDLINK_NOTATION)))) {
                    *sep = '\0';
                    path_len = (int32)(sep - filename);
                } else if ((sep = memmem64(filename, path_len,
                                           RSYNC_SYMLINK_NOTATION,
                                           strlen32(RSYNC_SYMLINK_NOTATION)))) {
                    *sep = '\0';
                    path_len = (int32)(sep - filename);
                }

                message->path_len = path_len;
                message->src_path = xmalloc(path_len + 1);
                memcpy64(message->src_path, filename, path_len + 1);

                message->type = DATA_TYPE_REMOVE_ROW;
                g_idle_add(update_ui_handler, message);
            }

            remaining = buf_output_pos - (line_len + 1);
            if (remaining > 0) {
                memmove64(buf_output_bulk, eol + 1, remaining);
            }
            buf_output_pos = remaining;
            if (buf_output_pos <= 0) {
                break;
            }
        }

    read_error_pipe:
        if (pipes[1].revents & POLLERR) {
            pipes[1].fd = -1;
            continue;
        }
        if (pipes[1].revents & POLLHUP) {
            pipes[1].fd = -1;
        }
        if (!(pipes[1].revents & POLLIN)) {
            continue;
        }

        r = read64(pipe_stderr[0], buf_error_bulk, SIZEOF(buf_error_bulk) - 1);
        if (r <= 0) {
            if (r < 0) {
                IPC_SEND_LOG_ERROR("Error reading stderr pipe: %s.\n",
                                   strerror(errno));
                pipes[1].fd = -1;
            }
            continue;
        }
        buf_error_bulk[r] = '\0';
        IPC_SEND_LOG_ERROR("%s", buf_error_bulk);

    } while ((pipes[0].fd >= 0) || (pipes[1].fd >= 0));

    if (waitpid(child_pid, NULL, 0) < 0) {
        IPC_SEND_LOG_ERROR("Error waiting for child: %s.\n", strerror(errno));
    }
    xunlink(files_from_filename);
    cecup.child_pid = 0;

    XCLOSE(&pipe_stdout[0]);
    XCLOSE(&pipe_stderr[0]);

    work_finalize(NULL);
    free_task_list(tasks);
    if (cecup.stop_working) {
        IPC_SEND_LOG_ERROR(_("Stop requested.\n"));
    }
    g_thread_exit(NULL);
}

#if TESTING_work
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "aux.c"
#include "on.c"

int
main(void) {
    ASSERT(true);
    exit(EXIT_SUCCESS);
}

#endif

#endif
