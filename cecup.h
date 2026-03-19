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

#define MAX_PATH_LENGTH 4096
#define MAX_NAME_LENGTH 256

enum CecupAction {
    ACTION_NEW,
    ACTION_UPDATE,
    ACTION_HARDLINK,
    ACTION_SYMLINK,
    ACTION_EQUAL,
    ACTION_DELETED,
    ACTION_DELETE,
    ACTION_IGNORE,
};

enum CecupReason {
    REASON_NEW,
    REASON_UPDATE,
    REASON_HARDLINK,
    REASON_SYMLINK,
    REASON_EQUAL,
    REASON_IGNORED,
    REASON_MISSING,
};

enum IgnoreType {
    IGNORE_TYPE_EXT,
    IGNORE_TYPE_DIR,
    IGNORE_TYPE_NAME,
    IGNORE_TYPE_NAME_ANY_DIR,
};

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
    [REASON_EQUAL]    = N_("Files have the same size and modification time"),
    [REASON_IGNORED]  = N_("Matches an ignore pattern"),
    [REASON_MISSING]  = N_("File does not exist in the original folder"),
    [REASON_NEW]      = N_("New file found in the original folder"),
    [REASON_HARDLINK] = N_("Hardlinked file in the original folder"),
    [REASON_SYMLINK]  = N_("Symlink in the original folder"),
    [REASON_UPDATE]   = N_("The original file is newer"),
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
    DATA_TYPE_REGENERATE_PREVIEW
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
} Message;

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
    GtkWidget *fix_button;

    GtkWidget *check_fs;
    GtkWidget *delete_excluded;
    GtkWidget *delete_after;
    
    GtkWidget *filter_new;
    GtkWidget *filter_hard;
    GtkWidget *filter_update;
    GtkWidget *filter_equal;
    GtkWidget *filter_delete;
    GtkWidget *filter_ignore;

    GtkListStore *store;
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
    GCond ui_ready_cond;
    bool ui_waiting;
    bool changed_dirs;

    pid_t child_pid;
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
static void on_preview_clicked(GtkWidget *b, void *data);

static void on_menu_open_item(GtkWidget *m, void *data);
static void on_menu_copy_path(GtkWidget *m, void *data);
static void on_menu_apply(GtkWidget *m, void *data);
static void on_menu_diff(GtkWidget *m, void *data);
static void on_menu_rename(GtkWidget *m, void *data);
static void on_menu_delete(GtkWidget *m, void *data);
static void on_menu_ignore(GtkWidget *m, void *data);

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

#define RSYNC_MESSAGE_DELETING "*deleting "

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
    {RSYNC_WILDCARD,          "_asterisk_in_filename_"                    },
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

static CecupMenuItem tree_menu_items[] = {
{N_("📄 Open File"),          0,          0,                                 on_menu_open_item, "file"},
{N_("📂 Open Folder"),        0,          0,                                 on_menu_open_item, "folder"},
{N_("📍 Copy Full Path"),     GDK_KEY_c,  GDK_CONTROL_MASK,                  on_menu_copy_path, "absolute"},
{N_("📋 Copy Relative Path"), GDK_KEY_c,  GDK_CONTROL_MASK | GDK_SHIFT_MASK, on_menu_copy_path, "relative"},
{N_("⏯️ Apply"),              0,          0,                                 on_menu_apply,     NULL},
{N_("🔍 Diff"),               0,          0,                                 on_menu_diff,      NULL},
{N_("✏️ Rename"),              GDK_KEY_F2, 0,                                 on_menu_rename,    NULL},
{N_("🗑️ Delete"),             0,          0,                                 on_menu_delete,    NULL},
{N_("💤 Ignore..."),          0,          0,                                 NULL,              NULL},
};

#endif /* CECUP_H */
