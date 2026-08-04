// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CECUP_H)
#define CECUP_H

#include "gtk_include.h"
#include "cbase.h"

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

static char *
side_name(int32 side) {
    if (side == L) {
        return "L";
    } else if (side == R) {
        return "R";
    } else {
        error("side must be L=%d or R=%d, but it is side=%d\n", L, R, side);
        fatal(EXIT_FAILURE);
    }
}

_Static_assert((!L) == R, "!L must be R");
_Static_assert((!R) == L, "!R must be L");
_Static_assert(SIZEOF(pid_t) <= SIZEOF(atomic_int),
               "pid_t must fit in atomic_int");

#define MAX_PATH_LENGTH 2048
#define MAX_NAME_LENGTH 256

#define INITIAL_CAPACITY 1024

#define ENUM_BITFLAGS 0
#define ENUM_NAME Action
#define ENUM_PREFIX_ ACTION_
#define ENUM_FIELDS     \
    X(ACTION_NEW)              \
    X(ACTION_UPDATE)           \
    X(ACTION_HARDLINK)         \
    X(ACTION_SYMLINK)          \
    X(ACTION_EQUAL)            \
    X(ACTION_DELETED)          \
    X(ACTION_DELETE)           \
    X(ACTION_IGNORE)
#include "xenums.c"

enum TraversalState {
    TRAVERSAL_STATE_PRESENT,
    TRAVERSAL_STATE_UNKNOWN,
    TRAVERSAL_STATE_UNKNOWN_SUBTREE,
};

#define ENUM_BITFLAGS 1
#define ENUM_NAME Reason
#define ENUM_PREFIX_ REASON_
#define ENUM_FIELDS     \
    X(REASON_NEW)              \
    X(REASON_SIZE)             \
    X(REASON_MTIME_NEWER)      \
    X(REASON_MTIME_OLDER)      \
    X(REASON_CTIME)            \
    X(REASON_OWNER)            \
    X(REASON_GROUP)            \
    X(REASON_PERM)             \
    X(REASON_TYPE)             \
    X(REASON_HARDLINK)         \
    X(REASON_SYMLINK)          \
    X(REASON_SYMLINK_NOT)       \
    X(REASON_SYMLINK_NO_TARGET) \
    X(REASON_SYMLINK_NOT_MATCH) \
    X(REASON_HARDLINK_NOT_REGULAR) \
    X(REASON_HARDLINK_MISSING_LINK) \
    X(REASON_HARDLINK_NOT_MATCH) \
    X(REASON_EQUAL)            \
    X(REASON_IGNORED)          \
    X(REASON_MISSING)
#include "xenums.c"

#define ENUM_BITFLAGS 0
#define ENUM_NAME ColumnType
#define ENUM_PREFIX_ COLUMN_
#define ENUM_FIELDS     \
    X(COLUMN_ACTION)           \
    X(COLUMN_PATH)             \
    X(COLUMN_SIZE)             \
    X(COLUMN_MTIME)
#include "xenums.c"

#define ENUM_BITFLAGS 0
#define ENUM_NAME CecupColumn
#define ENUM_PREFIX_ COL_
#define ENUM_FIELDS \
    X(COL_SELECTED)     \
    X(COL_SRC_ACTION)   \
    X(COL_DST_ACTION)   \
    X(COL_SRC_PATH)     \
    X(COL_DST_PATH)     \
    X(COL_SRC_SIZE)     \
    X(COL_DST_SIZE)     \
    X(COL_SRC_MTIME)    \
    X(COL_DST_MTIME)
#include "xenums.c"

#define ENUM_BITFLAGS 0
#define ENUM_NAME MsgType
#define ENUM_PREFIX_ MSG_
#define ENUM_FIELDS \
    X(MSG_LOG)                \
    X(MSG_LOG_ERROR)          \
    X(MSG_LOG_CMD)            \
    X(MSG_BATCH_ROW_TRANSFER) \
    X(MSG_BATCH_ROW_REMOVE)   \
    X(MSG_BATCH_ROW_RENAME)   \
    X(MSG_IGNORE_PATTERN)     \
    X(MSG_WORK_FINISHED)      \
    X(MSG_CLEAR_TREES)        \
    X(MSG_PROGRESS)
#include "xenums.c"

#if !defined(error2)
#define error2(...) fprintf(stderr, __VA_ARGS__)
#endif

#define HASH_KEY_TYPE char
#define HASH_VALUE_TYPE int32
#define HASH_VALUE_FORMATTER "%d"
#define HASH_PADDING_TYPE uint32
#define HASH_DUPLICATE_KEYS 0
#define HASH_TYPE fs_map
#include "hash.c"

typedef struct HardLinks {
    uint64 aggregate_hash_lo;
    uint64 aggregate_hash_hi;
    int32 count;
    int32 capacity;
    char **names;
    int32 *names_lens;
} HardLinks;

typedef struct FileID {
    dev_t device;
    ino_t inode;
} FileID;

static FileID
file_id_from_stat(struct stat *stat) {
    FileID file_id = {0};

    file_id.device = stat->st_dev;
    file_id.inode = stat->st_ino;

    return file_id;
}

#define HASH_KEY_TYPE FileID
#define HASH_KEY_FIXED_LEN 1
#define HASH_VALUE_TYPE HardLinks
#define HASH_PADDING_TYPE uint32
#define HASH_DUPLICATE_KEYS 0
#define HASH_TYPE inode_map
#include "hash.c"

#define HASH_KEY_TYPE char
#define HASH_DUPLICATE_KEYS 1
#define HASH_TYPE actions_set
#include "hash.c"

typedef struct Traversal {
    Arena *arena;
    char *base_path;
    int32 base_path_len;
    int32 file_count;
    int32 unknown_count;

    bool root_unknown;

    struct Hash_fs_map *map;
    struct Hash_inode_map *inode_map;

    int32 capacity;
    int32 nfiles;

    struct stat *stats;
    char **paths;
    char **symlink_targets;
    char **patterns;

    int16 *paths_lens;
    int16 *symlink_targets_lens;
    int16 *patterns_lens;

    int32 *row_ids;
    int8 *states;
} Traversal;

typedef struct TextInfo {
    enum ColumnType type;
    int8 side;
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

static char **action_strings_dir[2] = {
    [L] = src_action_strings_dir,
    [R] = dst_action_strings_dir,
};

static char **action_strings_file[2] = {
    [L] = src_action_strings_file,
    [R] = dst_action_strings_file,
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
    [REASON_SYMLINK_NO_TARGET_BIT_IDX]     = N_("Could not resolve link to check"),
    [REASON_SYMLINK_NOT_BIT_IDX]           = N_("Destination is not a symlink"),
    [REASON_SYMLINK_NOT_MATCH_BIT_IDX]     = N_("Symbolic link targets do not match"),
    [REASON_SIZE_BIT_IDX]                  = N_("File sizes differ"),
    [REASON_MTIME_NEWER_BIT_IDX]           = N_("File in the source is newer than backup"),
    [REASON_MTIME_OLDER_BIT_IDX]           = N_("File in the source is older than backup"),
    [REASON_CTIME_BIT_IDX]                 = N_("Metadata change times differ"),
    [REASON_OWNER_BIT_IDX]                 = N_("Owners differ"),
    [REASON_GROUP_BIT_IDX]                 = N_("Groups differ"),
    [REASON_PERM_BIT_IDX]                  = N_("Permissions differ"),
    [REASON_TYPE_BIT_IDX]                  = N_("Item types differ"),
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
    [REASON_SYMLINK_NO_TARGET_BIT_IDX]     = N_("Could not resolve link to check"),
    [REASON_SYMLINK_NOT_BIT_IDX]           = N_("Destination is not a symlink"),
    [REASON_SYMLINK_NOT_MATCH_BIT_IDX]     = N_("Symbolic link targets do not match"),
    [REASON_SIZE_BIT_IDX]                  = N_("Folder sizes differ"),
    [REASON_MTIME_NEWER_BIT_IDX]           = N_("Folder in the source is newer than backup"),
    [REASON_MTIME_OLDER_BIT_IDX]           = N_("Folder in the source is older than backup"),
    [REASON_CTIME_BIT_IDX]                 = N_("Metadata change times differ"),
    [REASON_OWNER_BIT_IDX]                 = N_("Owners differ"),
    [REASON_GROUP_BIT_IDX]                 = N_("Groups differ"),
    [REASON_PERM_BIT_IDX]                  = N_("Permissions differ"),
    [REASON_TYPE_BIT_IDX]                  = N_("Item types differ"),
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

typedef struct Message {
    enum MsgType type;
    enum Action action;

    double fraction;

    char *text;
    char *src_path;
    char *dst_path;

    int32 text_len;
    int32 src_path_len;
    int32 dst_path_len;

    int8 side;
    bool preview_clean;
} Message;

#define BATCH_SIZE 256

typedef struct MessageBatch {
    enum MsgType type;
    int8 side;
    struct timespec time_last_flush;

    char **paths;
    int32 *paths_lens;

    char **dst_paths;
    int32 *dst_paths_lens;

    int32 count;
    int32 capacity;
} MessageBatch;

typedef struct Task {
    enum Action action;
    int32 path_len;

    char *path;
    FileID file_id;

    int8 side;
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
    GtkWidget *browse_button[2];
    ulong entry_id[2];
    char *base[2];
    int32 base_len[2];

    GtkWidget *diff_entry;
    GtkWidget *term_entry;
    GtkWidget *search_entry;

    GtkWidget *preview_button;
    GtkWidget *stop_button;
    GtkWidget *sync_button;

    GtkWidget *ignore_button;

    GtkWidget *check_fs_button;
    GtkWidget *delete_ignored_button;
    GtkWidget *delete_after_button;
    GtkWidget *unselect_button;
    GtkWidget *select_visible_button;
    bool delete_ignored;
    bool delete_after;

    GtkWidget *filter_new;
    GtkWidget *filter_link;
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

    GtkWidget *progress_bar;

    int32 *rows[2];
    bool *rows_selected;
    int32 rows_len;
    int32 rows_capacity;

    int32 *rows_visible;
    int32 rows_visible_len;

    enum CecupColumn sort_col;
    GtkSortType sort_order;

    Arena *arena;
    pthread_mutex_t arena_mutex;

    // this is only needed for killing child transfers when the window is destroyed
    atomic_int child_pid;
    pthread_t work_thread;
    atomic_bool work_thread_done;
    bool work_thread_started;
    bool work_thread_joined;
    bool window_destroying;

    atomic_bool stop_working;

    IgnorePattern *ignore_patterns;
    int32 ignore_count;
    int32 ignore_capacity;

    Traversal traversal[2];

    struct Hash_actions_set *actions_set;

    char **transfers;
    int32 *transfers_lens;
    int32 ntransfers;
    int32 transfers_capacity;

    char **deletions;
    int32 *deletions_lens;
    int32 ndeletions;
    int32 deletions_capacity;

    bool preview_dirty;
} cecup = {0};

enum UpdateRowsType {
    UPDATE_ROWS_COMPLETE,
    UPDATE_ROWS_SORT,
    UPDATE_ROWS_SELECT,
    UPDATE_ROWS_FILTER_OUT,
};

typedef struct RowCache {
    int32 row_id;
    enum Action src_action;
    enum Action dst_action;
    union {
        char *ptr;
        int64 i64;
    } key;
} RowCache;

typedef struct ThreadData {
    TaskList *tasks;
    bool check_different_fs;
} ThreadData;

enum TransferBackend {
    TRANSFER_BACKEND_RSYNC,
    TRANSFER_BACKEND_MANUAL,
};

typedef struct TransferMetadataPolicy {
    bool compare_dir_ctime;
    bool compare_owner;
    bool compare_group;
    bool compare_perm;

    bool preserve_mtime;
    bool preserve_owner;
    bool preserve_group;
    bool preserve_perm;
    bool preserve_symlink;
    bool preserve_hardlink;
    bool preserve_special;
    bool preserve_extended;
} TransferMetadataPolicy;

static bool
transfer_backend_parse(char *backend, enum TransferBackend *result) {
    if (backend == NULL) {
        return false;
    }

    if (strequal(backend, "rsync")) {
        *result = TRANSFER_BACKEND_RSYNC;
        return true;
    }
    if (strequal(backend, "manual")) {
        *result = TRANSFER_BACKEND_MANUAL;
        return true;
    }

    return false;
}

static enum TransferBackend
transfer_backend_platform_default(void) {
#if OS_LINUX
    return TRANSFER_BACKEND_RSYNC;
#else
    return TRANSFER_BACKEND_MANUAL;
#endif
}

static enum TransferBackend
transfer_backend_selected_silent(void) {
    enum TransferBackend backend;
    char *backend_name;

    backend_name = getenv("CECUP_TRANSFER_BACKEND");
    if (transfer_backend_parse(backend_name, &backend)) {
        return backend;
    }

    return transfer_backend_platform_default();
}

static TransferMetadataPolicy
transfer_metadata_policy_for_backend(enum TransferBackend backend) {
    TransferMetadataPolicy policy = {0};

    policy.compare_perm = true;
    policy.preserve_mtime = true;
    policy.preserve_perm = true;
    policy.preserve_symlink = true;
    policy.preserve_hardlink = true;

    switch (backend) {
    case TRANSFER_BACKEND_RSYNC:
        policy.compare_dir_ctime = true;
        policy.compare_owner = true;
        policy.compare_group = true;
        policy.preserve_owner = true;
        policy.preserve_group = true;
        break;
    case TRANSFER_BACKEND_MANUAL:
        break;
    default:
        error("Invalid transfer backend %u.\n", backend);
        break;
    }

    return policy;
}

static TransferMetadataPolicy
transfer_metadata_policy_current(void) {
    enum TransferBackend backend;

    backend = transfer_backend_selected_silent();
    return transfer_metadata_policy_for_backend(backend);
}

typedef struct CecupMenuItem {
    char *label;
    uint32 keyval;
    GdkModifierType mask;
    void (*callback)(GtkWidget *, void *);
    char *variant;
    char *tooltip;
} CecupMenuItem;

#if CC_CLANG
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#endif

static gboolean update_ui_handler(void * user_data);
static void task_list_free(TaskList *tasks);
static void save_config(void);
static void aux_protect_interface_from_user(bool state);
static void log_internal(char *file, int line, char *func,
                         enum MsgType type, char *format, ...);
static int32 item_add(int32 src_idx, int32 dst_idx);

#if CC_CLANG
#pragma clang diagnostic pop
#endif

#define LOG(...)        \
    log_internal(__FILE__, __LINE__, (char *)__func__, MSG_LOG, __VA_ARGS__)
#define LOG_ERROR(...)  \
    log_internal(__FILE__, __LINE__, (char *)__func__, MSG_LOG_ERROR, __VA_ARGS__)
#define LOG_CMD(...)    \
    log_internal(__FILE__, __LINE__, (char *)__func__, MSG_LOG_CMD, __VA_ARGS__)

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

#define RSYNC_DUPLICATE       "removing duplicate"
#define RSYNC_IGNORE_PRE_DIR  "[sender] hiding directory "
#define RSYNC_IGNORE_PRE_FILE "[sender] hiding file "
#define RSYNC_SHOW_PRE_FILE   "[sender] showing file "
#define RSYNC_SHOW_PRE_DIR    "[sender] showing directory "
#define RSYNC_IGNORE_INTER    " because of pattern "
#define RSYNC_WILDCARD        "*"
#define RSYNC_INCLUDE_DIRS    "*/"

#define RSYNC_HARDLINK " => "
#define RSYNC_SYMLINK " -> "

static char *filename_problems[] = {
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

static void work_finalize(ThreadData *thread_data, bool preview_clean);
static int32 work_traverse_fs(Traversal *traversal);
static void *work_traverse_fs_thread(void *user_data);
static void traversal_clean(Traversal *traversal);
static void work_cleanup(void);
static void __attribute__((noreturn)) work_preview_cancel_and_reset(ThreadData *thread_data);
static void *work_preview(void *user_data);
static bool work_transfer_action_is_transfer(enum Action action);
static char *work_transfer_backend_name(enum TransferBackend backend);
static enum TransferBackend work_transfer_backend_current(void);
static void *work_transfer(void *user_data);
static char *work_rsync_itemize_skip(char *buf_output, int32 line_len);
static bool work_rsync_run(char *files_from_filename, int32 nfiles_total,
                           bool checksum, MessageBatch **batch_ptr);
static void *work_rsync(void *user_data);

static void stop_working(bool state);
static bool work_should_stop(void);
static void work_thread_done_set(bool state);
static bool work_thread_is_done(void);
static bool work_thread_is_active(void);
static void work_thread_join_once(void);
static void work_thread_start(void *(*function)(void *), void *data);
static void child_pid_set(pid_t pid);
static pid_t child_pid_get(void);
static void child_pid_signal(int32 signal_number);

static gboolean update_ui_handler(void *data);

static void
disable_dbus_warning(void) {
    if (setenv("GTK_IM_MODULE", "gtk-im-context-simple", true) < 0) {
        error("Error in setenv: %s.\n", strerror(errno));
        fatal(EXIT_FAILURE);
    }
    return;
}

#endif /* CECUP_H */
