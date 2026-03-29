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

#if !defined(CECUP_H)
#define CECUP_H

#include <gtk/gtk.h>
#include <sys/stat.h>
#include "generic.c"
#include "arena.c"
#include "i18n.h"

#define EMOJI_NEW "➕"
#define EMOJI_LINK "🔗"
#define EMOJI_SYMLINK "↪️"
#define EMOJI_UPDATE "➡️"
#define EMOJI_EQUAL "🟰"
#define EMOJI_DELETE "❌"
#define EMOJI_IGNORE "💤"

// LEFT and RIGHT
#define L 0
#define R 1

#define MAX_PATH_LENGTH 2048
#define MAX_NAME_LENGTH 256

#define MESSAGES_BUF_SIZE 256

#define ENUM_BITFLAGS 0
#define ENUM_NAME Action
#define ENUM_PREFIX_ ACTION_
#define ENUM_FIELDS     \
    X(NEW)              \
    X(UPDATE)           \
    X(HARDLINK)         \
    X(SYMLINK)          \
    X(EQUAL)            \
    X(DELETED)          \
    X(DELETE)           \
    X(IGNORE)
#include "xenums.c"

#define ENUM_BITFLAGS 1
#define ENUM_NAME Reason
#define ENUM_PREFIX_ REASON_
#define ENUM_FIELDS     \
    X(NEW)              \
    X(SIZE)             \
    X(MTIME_NEWER)      \
    X(MTIME_OLDER)      \
    X(CTIME)            \
    X(OWNER)            \
    X(GROUP)            \
    X(PERM)             \
    X(HARDLINK)         \
    X(SYMLINK)          \
    X(HARDLINK_NOT_REGULAR) \
    X(HARDLINK_MISSING_LINK) \
    X(HARDLINK_NOT_MATCH) \
    X(EQUAL)            \
    X(IGNORED)          \
    X(MISSING)
#include "xenums.c"

#define ENUM_BITFLAGS 0
#define ENUM_NAME ColumnType
#define ENUM_PREFIX_ COLUMN_
#define ENUM_FIELDS     \
    X(ACTION)           \
    X(PATH)             \
    X(SIZE)             \
    X(MTIME)
#include "xenums.c"

#define ENUM_BITFLAGS 0
#define ENUM_NAME DataType
#define ENUM_PREFIX_ DATA_TYPE_
#define ENUM_FIELDS \
    X(LOG)                \
    X(LOG_ERROR)          \
    X(LOG_CMD)            \
    X(ROW_TRANSFER)       \
    X(ROW_REMOVE)         \
    X(ROW_RENAME)         \
    X(BATCH)              \
    X(IGNORE_PATTERN)     \
    X(ENABLE_BUTTONS)     \
    X(CLEAR_TREES)        \
    X(PROGRESS_PREVIEW)   \
    X(ADD_ROW)
#include "xenums.c"

#if !defined(error2)
#define error2(...) fprintf(stderr, __VA_ARGS__)
#endif

#define HASH_VALUE_TYPE int32
#define HASH_VALUE_FORMATTER "%d"
#define HASH_PADDING_TYPE uint32
#define HASH_DUPLICATE_KEYS 0
#define HASH_AUTO_RESIZE 1
#define HASH_TYPE fs_map
#include "hash.c"

#define HASH_VALUE_TYPE int32
#define HASH_VALUE_FORMATTER "%d"
#define HASH_PADDING_TYPE uint32
#define HASH_DUPLICATE_KEYS 1
#define HASH_AUTO_RESIZE 1
#define HASH_TYPE inode_map
#include "hash.c"

typedef struct Traversal {
    Arena *arena;
    char *base_path;
    int32 base_path_len;
    int32 file_count;

    struct Hash_fs_map *map;
    struct Hash_inode_map *inode_map;

    int32 ncapacity;
    int32 nfiles;

    struct stat *stats;
    char **paths;
    char **link_targets;
    char **patterns;

    int16 *paths_lens;
    int16 *link_targets_lens;
    int16 *patterns_lens;
    int16 *nlinks;

    int32 *row_ids;
} Traversal;

typedef struct TextInfo {
    int32 side;
    enum ColumnType type;
} TextInfo;

static char *action_emojis[] = {
    [ACTION_NEW]      = EMOJI_NEW,
    [ACTION_HARDLINK] = EMOJI_LINK,
    [ACTION_SYMLINK]  = EMOJI_SYMLINK,
    [ACTION_UPDATE]   = EMOJI_UPDATE,
    [ACTION_EQUAL]    = EMOJI_EQUAL,
    [ACTION_DELETED]  = EMOJI_DELETE,
    [ACTION_DELETE]   = EMOJI_DELETE,
    [ACTION_IGNORE]   = EMOJI_IGNORE,
};

static char *src_action_strings_file[] = {
    [ACTION_NEW]      = N_("Copy to backup"),
    [ACTION_HARDLINK] = N_("Create hardlink in backup"),
    [ACTION_SYMLINK]  = N_("Create symlink in backup"),
    [ACTION_UPDATE]   = N_("Update file in backup"),
    [ACTION_EQUAL]    = N_("Already identical"),
    [ACTION_DELETED]  = N_("Not found in original"),
    [ACTION_DELETE]   = "",
    [ACTION_IGNORE]   = N_("Skip"),
};

static char *src_action_strings_dir[] = {
    [ACTION_NEW]      = N_("Copy to backup"),
    [ACTION_HARDLINK] = N_("Create hardlink in backup"),
    [ACTION_SYMLINK]  = N_("Create symlink in backup"),
    [ACTION_UPDATE]   = N_("Update folder in backup"),
    [ACTION_EQUAL]    = N_("Already identical"),
    [ACTION_DELETED]  = N_("Not found in original"),
    [ACTION_DELETE]   = "",
    [ACTION_IGNORE]   = N_("Skip"),
};

static char *dst_action_strings_file[] = {
    [ACTION_NEW]      = N_("Copy from original"),
    [ACTION_HARDLINK] = N_("Create hardlink according to original"),
    [ACTION_SYMLINK]  = N_("Create symlink according to original"),
    [ACTION_UPDATE]   = N_("Update from original"),
    [ACTION_EQUAL]    = N_("Already identical"),
    [ACTION_DELETED]  = "",
    [ACTION_DELETE]   = N_("Remove from backup"),
    [ACTION_IGNORE]   = N_("Skip"),
};

static char *dst_action_strings_dir[] = {
    [ACTION_NEW]      = N_("Copy from original"),
    [ACTION_HARDLINK] = N_("Create hardlink according to original"),
    [ACTION_SYMLINK]  = N_("Create symlink according to original"),
    [ACTION_UPDATE]   = N_("Update from original"),
    [ACTION_EQUAL]    = N_("Already identical"),
    [ACTION_DELETED]  = "",
    [ACTION_DELETE]   = N_("Remove from backup"),
    [ACTION_IGNORE]   = N_("Skip"),
};

static char *reason_strings_file[] = {
    [REASON_EQUAL_BIT_IDX]                 = N_("Files are equal"),
    [REASON_IGNORED_BIT_IDX]               = N_("Matches an ignore pattern"),
    [REASON_MISSING_BIT_IDX]               = N_("File does not exist in the original folder"),
    [REASON_NEW_BIT_IDX]                   = N_("New file found in the original folder"),
    [REASON_HARDLINK_BIT_IDX]              = N_("Hardlinked file in the original folder"),
    [REASON_HARDLINK_NOT_REGULAR_BIT_IDX]  = N_("Backup item is not a regular file"),
    [REASON_HARDLINK_MISSING_LINK_BIT_IDX] = N_("Backup item is missing link target"),
    [REASON_HARDLINK_NOT_MATCH_BIT_IDX]    = N_("Link targets do not match"),
    [REASON_SYMLINK_BIT_IDX]               = N_("Symlink in the original folder"),
    [REASON_SIZE_BIT_IDX]                  = N_("File sizes differ"),
    [REASON_MTIME_NEWER_BIT_IDX]           = N_("File in the source is newer than backup"),
    [REASON_MTIME_OLDER_BIT_IDX]           = N_("File in the source is older than backup"),
    [REASON_CTIME_BIT_IDX]                 = N_("Metadata change times differ"),
    [REASON_OWNER_BIT_IDX]                 = N_("Owners differ"),
    [REASON_GROUP_BIT_IDX]                 = N_("Groups differ"),
    [REASON_PERM_BIT_IDX]                  = N_("Permissions differ"),
};

static char *reason_strings_dir[] = {
    [REASON_EQUAL_BIT_IDX]                 = N_("Folders are equal"),
    [REASON_IGNORED_BIT_IDX]               = N_("Matches an ignore pattern"),
    [REASON_MISSING_BIT_IDX]               = N_("Folder does not exist in the original folder"),
    [REASON_NEW_BIT_IDX]                   = N_("New folder found in the original folder"),
    [REASON_HARDLINK_BIT_IDX]              = N_("Hardlinked folder in the original folder"),
    [REASON_HARDLINK_NOT_REGULAR_BIT_IDX]  = N_("Backup item is not a regular file"),
    [REASON_HARDLINK_MISSING_LINK_BIT_IDX] = N_("Backup item is missing link target"),
    [REASON_HARDLINK_NOT_MATCH_BIT_IDX]    = N_("Link targets do not match"),
    [REASON_SYMLINK_BIT_IDX]               = N_("Symlink in the original folder"),
    [REASON_SIZE_BIT_IDX]                  = N_("Folder sizes differ"),
    [REASON_MTIME_NEWER_BIT_IDX]           = N_("Folder in the source is newer than backup"),
    [REASON_MTIME_OLDER_BIT_IDX]           = N_("Folder in the source is older than backup"),
    [REASON_CTIME_BIT_IDX]                 = N_("Metadata change times differ"),
    [REASON_OWNER_BIT_IDX]                 = N_("Owners differ"),
    [REASON_GROUP_BIT_IDX]                 = N_("Groups differ"),
    [REASON_PERM_BIT_IDX]                  = N_("Permissions differ"),
};

static char *colors[] = {
    [ACTION_NEW]      = "#D4EDDA",
    [ACTION_UPDATE]   = "#CCE5FF",
    [ACTION_HARDLINK] = "#E2D1F9",
    [ACTION_SYMLINK]  = "#FFD1F9",
    [ACTION_EQUAL]    = "#F0F0F0",
    [ACTION_IGNORE]   = "#FFF3CD",
    [ACTION_DELETE]   = "#F8D7DA",
    [ACTION_DELETED]  = "#F8D7DA",
};

enum RefreshType {
    REFRESH_FINAL = 1,
    REFRESH_PARTIAL = 1 << 1,
    REFRESH_FILTER_CHANGED = 1 << 2,
};

enum CecupColumn {
    COL_SELECTED = 0,
    COL_SRC_ACTION,
    COL_DST_ACTION,
    COL_SRC_PATH,
    COL_DST_PATH,
    COL_SIZE_TEXT,
    COL_SIZE_RAW,
    COL_MTIME_TEXT,
    COL_MTIME_RAW,
    COL_ROW_ID,
    NUM_COLS
};

typedef struct Message {
    enum DataType type;
    enum Action action;

    char *text;
    char *src_path;
    char *dst_path;
    char *link_target;
    char *ignore_pattern;
    char *old_path;
    char *new_path;

    int32 text_len;
    int32 path_len;
    int32 link_target_len;
    int32 ignore_pattern_len;
    int32 old_path_len;
    int32 new_path_len;

    int32 side;
    double fraction;

    bool is_dir;
    bool preview_clean;
} Message;

#define BATCH_SIZE 256

typedef struct MessageBatch {
    enum DataType type;
    int32 count;
    struct timespec time_last_flush;
    Message *messages[BATCH_SIZE];
} MessageBatch;

typedef struct Task {
    enum DataType type;
    enum Action action;
    int32 side;

    char *path;
    char *link_target;

    int32 path_len;
    int32 link_target_len;
} Task;

typedef struct TaskList {
    int32 count;
    int32 padding;
    Task *items[];
} TaskList;

#define CECUP_TYPE_ITEM_PROXY (cecup_item_proxy_get_type())
G_DECLARE_FINAL_TYPE(CecupItemProxy, cecup_item_proxy,
                     CECUP, ITEM_PROXY, GObject)

#define CECUP_TYPE_LIST_MODEL (cecup_list_model_get_type())
G_DECLARE_FINAL_TYPE(CecupListModel, cecup_list_model,
                     CECUP, LIST_MODEL, GObject)

static CecupListModel *cecup_list_model_new(void);
static int32 cecup_item_proxy_get_index(CecupItemProxy *proxy);

typedef struct IgnorePattern {
    char *str;
    int32 len;
    char *match_str;
    bool dir_only;
    bool has_slash;
} IgnorePattern;

static struct {
    GtkApplication *application;
    GtkWidget *gtk_window;

    GtkWidget *invert_button;
    GtkWidget *dir_entry[2];
    ulong src_entry_id;
    ulong dst_entry_id;
    char *src_base;
    char *dst_base;
    int32 src_base_len;
    int32 dst_base_len;

    GtkWidget *diff_entry;
    GtkWidget *term_entry;
    GtkWidget *search_entry;

    GtkWidget *preview_button;
    GtkWidget *stop_button;
    GtkWidget *sync_button;

    GtkWidget *ignore_button;

    GtkWidget *check_fs;
    GtkWidget *delete_ignored;
    GtkWidget *delete_after;
    
    GtkWidget *filter_new;
    GtkWidget *filter_hard;
    GtkWidget *filter_update;
    GtkWidget *filter_equal;
    GtkWidget *filter_delete;
    GtkWidget *filter_ignore;

    GListModel *store;
    GtkWidget *log_view;
    GtkTextBuffer *log_buffer;
    char ignore_path[MAX_PATH_LENGTH];
    char config_path[MAX_PATH_LENGTH];
    char *search_query;
    int32 search_query_len;
    uint32 search_timeout_id;
    GtkWidget *tree[2];
    GtkWidget *stats_label;

    GtkWidget *progress_preview;

    int32 *rows_src;
    int32 *rows_dst;
    uint8 *rows_selected;
    int32 rows_len;
    int32 rows_capacity;

    int32 *rows_visible;
    int32 rows_visible_len;

    enum CecupColumn sort_col;
    GtkSortType sort_order;
    uint32 refresh_id;

    Arena *arena;
    GMutex arena_mutex;

    pid_t child_pid;
    volatile bool stop_working;

    IgnorePattern *ignore_patterns;
    int32 ignore_count;
    int32 ignore_capacity;

    Traversal traversal_src;
    Traversal traversal_dst;
    char **transfers;
    int32 ntransfers;
    int32 transfers_capacity;
    bool preview_dirty;
} cecup;

typedef struct ThreadData {
    TaskList *tasks;
} ThreadData;

typedef struct CecupMenuItem {
    char *label;
    uint32 keyval;
    GdkModifierType mask;
    void (*callback)(GtkWidget *, void *);
    char *variant;
} CecupMenuItem;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"

static gboolean update_ui_handler(void * user_data);
static void free_task_list(TaskList *tasks);
static void save_config(void);
static void protect_interface_from_user(bool state);
static void log_internal(char *file, int line,
                         enum DataType type, char *format, ...);
static int32 item_add(int32 src_idx, int32 dst_idx);

#pragma clang diagnostic pop

#define LOG(...)        \
    log_internal(__FILE__, __LINE__, DATA_TYPE_LOG, __VA_ARGS__)
#define LOG_ERROR(...)  \
    log_internal(__FILE__, __LINE__, DATA_TYPE_LOG_ERROR, __VA_ARGS__)
#define LOG_CMD(...)    \
    log_internal(__FILE__, __LINE__, DATA_TYPE_LOG_CMD, __VA_ARGS__)

enum RsyncCharAction {
    RSYNC_CHAR0_ACTION_SEND = '<',
    RSYNC_CHAR0_ACTION_RECEIVE = '>',
    RSYNC_CHAR0_ACTION_CHANGE = 'c',
    RSYNC_CHAR0_ACTION_HARDLINK = 'h',
    RSYNC_CHAR0_ACTION_NO_UPDATE = '.',
    RSYNC_CHAR0_ACTION_MESSAGE = '*',
};

enum RsyncCharType {
    RSYNC_CHAR1_TYPE_FILE = 'f',
    RSYNC_CHAR1_TYPE_DIR = 'd',
    RSYNC_CHAR1_TYPE_SYMLINK = 'L',
    RSYNC_CHAR1_TYPE_DEVICE = 'D',
    RSYNC_CHAR1_TYPE_SPECIAL = 'S',
};

enum RsyncCharAttribute {
    RSYNC_CHAR_ATTR_NO_CHANGE = '.',
    RSYNC_CHAR_ATTR_ALL_SPACE_MEANS_ALL_UNCHANGED = ' ',
    RSYNC_CHAR_ATTR_NEW = '+',
    RSYNC_CHAR_ATTR_UNKNOWN = '?',
    RSYNC_CHAR_ATTR_CHECKSUM = 'c',
    RSYNC_CHAR_ATTR_SIZE = 's',
    RSYNC_CHAR_ATTR_TIME = 't',
    RSYNC_CHAR_ATTR_PERM = 'p',
    RSYNC_CHAR_ATTR_OWNER = 'o',
    RSYNC_CHAR_ATTR_GROUP = 'g',
    RSYNC_CHAR_ATTR_ACL = 'a',
    RSYNC_CHAR_ATTR_XATTR = 'x',
};

#define RSYNC_ITEMIZE_PLACEHOLDERS "YXcstpoguax"

#define RSYNC_MESSAGE_DELETING "*deleting   0 "

#define RSYNC_IGNORE_PRE_DIR  "[sender] hiding directory "
#define RSYNC_IGNORE_PRE_FILE "[sender] hiding file "
#define RSYNC_SHOW_PRE_FILE   "[sender] showing file "
#define RSYNC_SHOW_PRE_DIR    "[sender] showing directory "
#define RSYNC_IGNORE_INTER    " because of pattern "
#define RSYNC_WILDCARD        "*"
#define RSYNC_INCLUDE_DIRS    "*/"

#define RSYNC_HARDLINK " => "
#define RSYNC_SYMLINK " -> "

static char *problems[] = {
    "\n",
    /* "\\", */
    /* "\"", */
    /* "\'", */
    /* "<", */
    /* ">", */
    /* ":", */
    /* "|", */
    /* "?", */
    RSYNC_WILDCARD,
    RSYNC_HARDLINK,
    RSYNC_SYMLINK,
    RSYNC_IGNORE_PRE_FILE,
    RSYNC_IGNORE_PRE_DIR,
    RSYNC_IGNORE_INTER,
    RSYNC_SHOW_PRE_FILE,
    RSYNC_SHOW_PRE_DIR,
};

static void work_batch_flush(MessageBatch **batch_ptr);
static void work_batch_push(MessageBatch **batch_ptr, Message *message);
static void work_finalize(bool preview_clean);
static int64 work_traverse_fs(Traversal *traversal);
static void *work_traverse_fs_thread(void *user_data);
static void work_traverse_clean(Traversal *traversal);
static void work_cleanup(void);
static void __attribute__((noreturn)) work_preview_cancel_and_reset(void);
static void * work_preview(void *user_data);
static char * work_check_itemize_line(char *buf_output);
static bool work_rsync_run(char *files_from_filename, bool checksum, MessageBatch **batch_ptr);
static void * work_rsync(void *user_data);

static gboolean update_ui_handler(void *data);

static void on_menu_open_item(GtkWidget *m, void *data);
static void on_menu_copy_path(GtkWidget *m, void *data);
static void on_menu_apply(GtkWidget *m, void *data);
static void on_menu_diff(GtkWidget *m, void *data);
static void on_menu_rename(GtkWidget *m, void *data);
static void on_menu_delete(GtkWidget *m, void *data);

static void on_popover_closed(GtkWidget *popover, void *data);
static void execute_menu_item(GtkWidget *tree, CecupMenuItem *menu_item);

#endif /* CECUP_H */
