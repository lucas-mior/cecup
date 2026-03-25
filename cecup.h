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
#define ENUM_NAME CecupAction
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
#define ENUM_NAME CecupReason
#define ENUM_PREFIX_ REASON_
#define ENUM_FIELDS     \
    X(NEW)              \
    X(SIZE)             \
    X(MTIME)            \
    X(CTIME)            \
    X(OWNER)            \
    X(GROUP)            \
    X(PERM)             \
    X(HARDLINK)         \
    X(SYMLINK)          \
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

#if !defined(error2)
#define error2(...) fprintf(stderr, __VA_ARGS__)
#endif

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

static char *src_action_strings[] = {
    [ACTION_NEW]      = N_("Copy to backup"),
    [ACTION_HARDLINK] = N_("Create hardlink in backup"),
    [ACTION_SYMLINK]  = N_("Create symlink in backup"),
    [ACTION_UPDATE]   = N_("Update file in backup"),
    [ACTION_EQUAL]    = N_("Already identical"),
    [ACTION_DELETED]  = N_("Not found in original"),
    [ACTION_DELETE]   = "",
    [ACTION_IGNORE]   = N_("Skip"),
};

static char *dst_action_strings[] = {
    [ACTION_NEW]      = N_("Copy from original"),
    [ACTION_HARDLINK] = N_("Create hardlink according to original"),
    [ACTION_SYMLINK]  = N_("Create symlink according to original"),
    [ACTION_UPDATE]   = N_("Update from original"),
    [ACTION_EQUAL]    = N_("Already identical"),
    [ACTION_DELETED]  = "",
    [ACTION_DELETE]   = N_("Remove from backup"),
    [ACTION_IGNORE]   = N_("Skip"),
};

static char *reason_strings[] = {
    [REASON_EQUAL_BIT_IDX]    = N_("Files have the same size and modification time"),
    [REASON_IGNORED_BIT_IDX]  = N_("Matches an ignore pattern"),
    [REASON_MISSING_BIT_IDX]  = N_("File does not exist in the original folder"),
    [REASON_NEW_BIT_IDX]      = N_("New file found in the original folder"),
    [REASON_HARDLINK_BIT_IDX] = N_("Hardlinked file in the original folder"),
    [REASON_SYMLINK_BIT_IDX]  = N_("Symlink in the original folder"),
    [REASON_SIZE_BIT_IDX]     = N_("File sizes differ"),
    [REASON_MTIME_BIT_IDX]    = N_("Modification times differ"),
    [REASON_CTIME_BIT_IDX]    = N_("Metadata change times differ"),
    [REASON_OWNER_BIT_IDX]    = N_("Owners differ"),
    [REASON_GROUP_BIT_IDX]    = N_("Groups differ"),
    [REASON_PERM_BIT_IDX]     = N_("Permissions differ"),
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
    COL_ROW_PTR,
    NUM_COLS
};

typedef struct CecupRow {
    enum CecupAction src_action;
    enum CecupAction dst_action;
    enum CecupReason reason;

    /* relative paths.
     * must NOT have a slash at the beginning.
     * must have a slash at the end if a directory.
     */
    char *src_path;
    char *dst_path;

    /* might be relative or absolute. */
    char *link_target;

    char *ignore_pattern;

    int32 path_len;
    int32 link_target_len;
    int32 ignore_pattern_len;

    char src_size_text[16];
    char dst_size_text[16];
    char src_mtime_text[32];
    char dst_mtime_text[32];
    int64 src_size_raw;
    int64 dst_size_raw;
    int64 src_mtime_raw;
    int64 dst_mtime_raw;

    bool selected;
} CecupRow;

enum DataType {
    DATA_TYPE_LOG,
    DATA_TYPE_LOG_ERROR,
    DATA_TYPE_LOG_CMD,
    DATA_TYPE_TREE_UPDATE,
    DATA_TYPE_REMOVE_ROW,
    DATA_TYPE_ENABLE_BUTTONS,
    DATA_TYPE_CLEAR_TREES,
    DATA_TYPE_PROGRESS_PREVIEW,
    DATA_TYPE_ADD_ROW,
};

typedef struct Message {
    enum DataType type;
    enum CecupAction action;
    enum CecupReason reason;

    char *message;
    char *src_path;
    char *dst_path;
    char *link_target;
    char *ignore_pattern;
    char *path_to_focus;

    int32 message_len;
    int32 path_len;
    int32 link_target_len;
    int32 ignore_pattern_len;
    int32 focus_len;

    int64 src_size;
    int64 src_mtime;
    int64 dst_size;
    int64 dst_mtime;

    int32 side;
    double fraction;

    bool delete_excluded;
    bool is_dir;
} Message;

typedef struct MessageBatch {
    enum DataType type;
    int32 count;
    Message *messages[];
} MessageBatch;

typedef struct Task {
    enum DataType type;
    enum CecupAction action;
    enum CecupReason reason;
    int32 side;

    char *message;
    char *path;
    char *link_target;
    char *ignore_pattern;

    int32 message_len;
    int32 path_len;
    int32 link_target_len;
    int32 ignore_pattern_len;
} Task;

typedef struct TaskList {
    int32 count;
    int32 padding;
    Task *items[];
} TaskList;

#define CECUP_TYPE_ROW_PROXY (cecup_row_proxy_get_type())
G_DECLARE_FINAL_TYPE(CecupRowProxy, cecup_row_proxy, CECUP, ROW_PROXY, GObject)

#define CECUP_TYPE_LIST_MODEL (cecup_list_model_get_type())
G_DECLARE_FINAL_TYPE(CecupListModel, cecup_list_model, CECUP, LIST_MODEL, GObject)

static struct {
    GtkApplication *application;
    GtkWidget *gtk_window;

    GtkWidget *invert_button;
    GtkWidget *src_entry;
    GtkWidget *dst_entry;
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
    GtkWidget *delete_excluded;
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
    uint32 search_timeout_id;
    GtkWidget *tree[2];
    GtkWidget *stats_label;

    GtkWidget *progress_preview;

    CecupRow **rows;
    int32 rows_len;
    int32 rows_capacity;

    CecupRow **rows_visible;
    int32 rows_visible_len;

    enum CecupColumn sort_col;
    GtkSortType sort_order;
    uint32 refresh_id;

    Arena *arena;
    GMutex arena_mutex;

    pid_t child_pid;
    volatile bool stop_working;
} cecup;

typedef struct ThreadData {
    bool is_preview;
    bool check_different_fs;
    bool delete_excluded;
    bool delete_after;
    bool filtered;
    char *relative_old;
    char *relative_new;
    int32 len_old;
    int32 len_new;
} ThreadData;

static gboolean update_ui_handler(void * user_data);
static void refresh_ui_list_locked(enum RefreshType, char *path_to_focus);
static void free_task_list(TaskList *tasks);
static void save_config(void);
static void free_message(void *data);
static void protect_interface_from_user(bool state);

static char *row_path_get(CecupRow *row) {
    if (row->src_path) {
        return row->src_path;
    } else if (row->dst_path) {
        return row->dst_path;
    } else {
        error2("Error: src_path and dst_path are NULL.\n");
        exit(EXIT_FAILURE);
    }
}

static void ipc_send_log_internal(char *file, int line,
                                  enum DataType type, char *format, ...);

#define IPC_SEND_LOG(...)        \
    ipc_send_log_internal(__FILE__, __LINE__, DATA_TYPE_LOG, __VA_ARGS__)
#define IPC_SEND_LOG_ERROR(...)  \
    ipc_send_log_internal(__FILE__, __LINE__, DATA_TYPE_LOG_ERROR, __VA_ARGS__)
#define IPC_SEND_LOG_CMD(...)    \
    ipc_send_log_internal(__FILE__, __LINE__, DATA_TYPE_LOG_CMD, __VA_ARGS__)

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

/* for ignored files on the source, rsync --verbose --verbose outputs:
 * [sender] hiding file <filename> because of pattern <pattern>
 */

#define RSYNC_IGNORE_PRE_DIR  "[sender] hiding directory "
#define RSYNC_IGNORE_PRE_FILE "[sender] hiding file "
#define RSYNC_SHOW_PRE_FILE   "[sender] showing file "
#define RSYNC_SHOW_PRE_DIR    "[sender] showing directory "
#define RSYNC_IGNORE_INTER    " because of pattern "
#define RSYNC_WILDCARD        "*"
#define RSYNC_INCLUDE_DIRS    "*/"

#define RSYNC_HARDLINK_NOTATION " => "
#define RSYNC_SYMLINK_NOTATION " -> "
#define BATCH_SIZE 256

static struct {
    char *problem;
    char *rename;
} replacements[] = {
    {"\n",                    "_newline_in_filename_"                    },
    /* {"\\",                    "_backslash_in_filename_"                }, */
    /* {"\"",                    "_double_quote_in_filename_"             }, */
    /* {"\'",                    "_single_quote_in_filename_"             }, */
    /* {"<",                     "_less_than_in_filename_"                }, */
    /* {">",                     "_greater_than_in_filename_"             }, */
    /* {":",                     "_colon_in_filename_"                    }, */
    /* {"|",                     "_pipe_in_filename_"                     }, */
    /* {"?",                     "_question_mark_in_filename_"            }, */
    {RSYNC_WILDCARD,          "_asterisk_in_filename_"                   },
    {RSYNC_HARDLINK_NOTATION, "_rsync_hardlink_notation_in_filename_"     },
    {RSYNC_SYMLINK_NOTATION,  "_rsync_symlink_notation_in_filename_"      },
    {RSYNC_IGNORE_PRE_FILE,   "rsync_ignore_prelude_file_in_filename"     },
    {RSYNC_IGNORE_PRE_DIR,    "rsync_ignore_prelude_dir_in_filename"      },
    {RSYNC_IGNORE_INTER,      "rsync_ignore_interlude_in_filename"        },
    {RSYNC_SHOW_PRE_FILE,     "rsync_show_prelude_file_in_filename"       },
    {RSYNC_SHOW_PRE_DIR,      "rsync_show_prelude_dir_in_filename"        },
};

typedef struct CecupMenuItem {
    char *label;
    uint32 keyval;
    GdkModifierType mask;
    void (*callback)(GtkWidget *, void *);
    char *variant;
} CecupMenuItem;

#endif /* CECUP_H */
