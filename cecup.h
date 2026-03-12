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

#define SIDE_LEFT 0
#define SIDE_RIGHT 1

#define MAX_PATH_LENGTH 4096
#define ALIGN16(n) (((n) + 15) & ~15)

enum CecupAction {
    UI_ACTION_NEW,
    UI_ACTION_UPDATE,
    UI_ACTION_HARDLINK,
    UI_ACTION_SYMLINK,
    UI_ACTION_EQUAL,
    UI_ACTION_DELETED,
    UI_ACTION_DELETE,
    UI_ACTION_IGNORE,
    NUM_UI_ACTIONS
};

enum CecupReason {
    UI_REASON_NEW,
    UI_REASON_UPDATE,
    UI_REASON_HARDLINK,
    UI_REASON_SYMLINK,
    UI_REASON_EQUAL,
    UI_REASON_IGNORED,
    UI_REASON_MISSING,
    NUM_UI_REASONS
};

static char *action_emojis[] = {
    [UI_ACTION_NEW]      = EMOJI_NEW,
    [UI_ACTION_HARDLINK] = EMOJI_LINK,
    [UI_ACTION_SYMLINK]  = EMOJI_SYMLINK,
    [UI_ACTION_UPDATE]   = EMOJI_UPDATE,
    [UI_ACTION_EQUAL]    = EMOJI_EQUAL,
    [UI_ACTION_DELETED]  = EMOJI_DELETE,
    [UI_ACTION_DELETE]   = EMOJI_DELETE,
    [UI_ACTION_IGNORE]   = EMOJI_IGNORE
};

static char *src_action_strings[] = {
    [UI_ACTION_NEW]      = N_("Copy to backup"),
    [UI_ACTION_HARDLINK] = N_("Create hardlink in backup"),
    [UI_ACTION_SYMLINK]  = N_("Create symlink in backup"),
    [UI_ACTION_UPDATE]   = N_("Update file in backup"),
    [UI_ACTION_EQUAL]    = N_("Already identical"),
    [UI_ACTION_DELETED]  = N_("Not found in original"),
    [UI_ACTION_DELETE]   = "",
    [UI_ACTION_IGNORE]   = N_("Skip")
};

static char *dst_action_strings[] = {
    [UI_ACTION_NEW]      = N_("Copy from original"),
    [UI_ACTION_HARDLINK] = N_("Create hardlink according to original"),
    [UI_ACTION_SYMLINK]  = N_("Create symlink according to original"),
    [UI_ACTION_UPDATE]   = N_("Update from original"),
    [UI_ACTION_EQUAL]    = N_("Already identical"),
    [UI_ACTION_DELETED]  = "",
    [UI_ACTION_DELETE]   = N_("Remove from backup"),
    [UI_ACTION_IGNORE]   = N_("Skip")
};

static char *reason_strings[] = {
    [UI_REASON_EQUAL]    = N_("Files have the same size and modification time"),
    [UI_REASON_IGNORED]  = N_("Matches an ignore rule"),
    [UI_REASON_MISSING]  = N_("File does not exist in the original folder"),
    [UI_REASON_NEW]      = N_("New file found in the original folder"),
    [UI_REASON_HARDLINK] = N_("Hardlinked file in the original folder"),
    [UI_REASON_SYMLINK]  = N_("Symlink in the original folder"),
    [UI_REASON_UPDATE]   = N_("The original file is newer"),
};

static char *colors[] = {
    [UI_ACTION_NEW]      = "#D4EDDA",
    [UI_ACTION_UPDATE]   = "#CCE5FF",
    [UI_ACTION_HARDLINK] = "#E2D1F9",
    [UI_ACTION_SYMLINK]  = "#FFD1F9",
    [UI_ACTION_EQUAL]    = "#F0F0F0",
    [UI_ACTION_IGNORE]   = "#FFF3CD",
    [UI_ACTION_DELETE]   = "#F8D7DA",
    [UI_ACTION_DELETED]  = "#F8D7DA",
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

    char *src_path;
    char *dst_path;
    char *link_target;
    char *ignore_pattern;

    int32 src_path_len;
    int32 dst_path_len;
    int32 link_target_len;
    int32 ignore_pattern_len;

    char size_text[16];
    char mtime_text[32];
    int64 size_raw;
    int64 mtime_raw;

    char *src_color;
    char *dst_color;

    bool selected;
} CecupRow;

enum DataType {
    DATA_TYPE_LOG,
    DATA_TYPE_LOG_ERROR,
    DATA_TYPE_LOG_CMD,
    DATA_TYPE_TREE_ROW,
    DATA_TYPE_REMOVE_TREE_ROW,
    DATA_TYPE_ENABLE_BUTTONS,
    DATA_TYPE_CLEAR_TREES,
    DATA_TYPE_PROGRESS_RSYNC,
    DATA_TYPE_PROGRESS_PREVIEW,
    DATA_TYPE_REGENERATE_PREVIEW
};

typedef struct Message {
    enum DataType type;
    enum CecupAction action;
    enum CecupReason reason;

    char *message;
    char *filepath;
    char *link_target;
    char *ignore_pattern;

    int32 message_len;
    int32 filepath_len;
    int32 link_target_len;
    int32 ignore_pattern_len;

    int64 size;
    int64 mtime;

    int32 side;
    double fraction;
} Message;

typedef struct TaskList {
    int32 count;
    Message *items[];
} TaskList;

static struct {
    GtkWidget *gtk_window;

    GtkWidget *src_entry;
    GtkWidget *dst_entry;
    ulong src_entry_id;
    ulong dst_entry_id;
    char *src_base;
    char *dst_base;
    int64 src_base_len;
    int64 dst_base_len;

    GtkWidget *diff_entry;
    GtkWidget *term_entry;

    GtkWidget *preview_button;
    GtkWidget *stop_button;
    GtkWidget *sync_button;
    volatile bool cancel_sync;

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
    GtkWidget *l_tree;
    GtkWidget *r_tree;
    GtkWidget *stats_label;

    GtkWidget *progress_rsync;
    GtkWidget *progress_preview;

    CecupRow **rows;
    int32 rows_len;
    int32 rows_capacity;

    CecupRow **rows_visible;
    int32 rows_visible_len;

    enum CecupColumn sort_col;
    GtkSortType sort_order;
    uint32 refresh_id;

    Arena *row_arena;
    Arena *ui_arena;
    GMutex row_arena_mutex;
    GMutex ui_arena_mutex;
} cecup;

typedef struct ThreadData {
    bool is_preview;
    bool check_different_fs;
    bool delete_excluded;
    bool delete_after;
} ThreadData;

enum PathType {
    PATH_RELATIVE,
    PATH_ABSOLUTE,
};

static gboolean update_ui_handler(void * user_data);
static void on_preview_clicked(GtkWidget *b, void * data);
static void refresh_ui_list(void);
static gboolean refresh_ui_timeout_callback(void * data);
static TaskList *get_target_tasks(int32 side,
                                   char *clicked_path,
                                   enum CecupAction clicked_action);
static void free_update_data(Message *message);
static void free_task_list(TaskList *tasks);
static void save_config(void);

enum RsyncCharAction {
    RSYNC_CHAR_SEND = '<',
    RSYNC_CHAR_RECEIVE = '>',
    RSYNC_CHAR_CHANGE = 'c',
    RSYNC_CHAR_HARDLINK = 'h',
    RSYNC_CHAR_NO_UPDATE = '.',
    RSYNC_CHAR_MESSAGE = '*',
};

enum RsyncCharType {
    RSYNC_CHAR_FILE = 'f',
    RSYNC_CHAR_DIR = 'd',
    RSYNC_CHAR_SYMLINK = 'L',
    RSYNC_CHAR_DEVICE = 'D',
    RSYNC_CHAR_SPECIAL = 'S',
};

enum RsyncCharAttribute {
    RSYNC_CHAR_CHECKSUM = 'c',
    RSYNC_CHAR_SIZE = 's',
    RSYNC_CHAR_TIME = 't',
    RSYNC_CHAR_PERM = 'p',
    RSYNC_CHAR_OWNER = 'o',
    RSYNC_CHAR_GROUP = 'g',
    RSYNC_CHAR_ACL = 'a',
    RSYNC_CHAR_XATTR = 'x',
    RSYNC_CHAR_NO_ATTR_CHANGE = '.',
    RSYNC_CHAR_ALL_SPACE_MEANS_ALL_UNCHANGED = ' ',
};

#define RSYNC_ITEMIZE_PLACEHOLDERS "YXcstpoguax"
#define RSYNC_INDEX_ACTION 0
#define RSYNC_INDEX_FILE_TYPE 1

#define RSYNC_MESSAGE_DELETING "*deleting"

/* for ignored files on the source, rsync --verbose --verbose outputs:
 * [sender] hiding file <filename> because of pattern <pattern>
 */
#define RSYNC_IGNORE_PRE "[sender] hiding file "
#define RSYNC_IGNORE_INTER " because of pattern "

#define RSYNC_HARDLINK_NOTATION " => "
#define RSYNC_SYMLINK_NOTATION " -> "
#define BATCH_SIZE 256

// clang-format off
static struct {
    char *problem;
    char *rename;
} replacements[] = {
    {"\\",                    "_backslash_in_filename_"                   },
    {"\n",                    "_newline_in_filename_"                     },
    {"\"",                    "_double_quote_in_filename_"                },
    {"\'",                    "_single_quote_in_filename_"                },
    {"<",                     "_less_than_in_filename_"                   },
    {">",                     "_greater_than_in_filename_"                },
    {":",                     "_colon_in_filename_"                       },
    {"|",                     "_pipe_in_filename_"                        },
    {"?",                     "_question_mark_in_filename_"               },
    {"*",                     "_asterisk_in_filename_"                    },
    {RSYNC_HARDLINK_NOTATION, "_rsync_hardlink_notation_in_filename_"     },
    {RSYNC_SYMLINK_NOTATION,  "_rsync_symlink_notation_in_filename_"      },
    {RSYNC_IGNORE_PRE,        "rsync_ignore_message_prelude_in_filename"  },
    {RSYNC_IGNORE_INTER,      "rsync_ignore_message_interlude_in_filename"},
};
// clang-format on

#endif /* CECUP_H */
