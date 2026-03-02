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

#define MAX_PATH_LENGTH 4096
#define ALIGN16(n) (((n) + 15) & ~15)

enum CecupAction {
    UI_ACTION_NONE = 0,
    UI_ACTION_NEW,
    UI_ACTION_HARDLINK,
    UI_ACTION_SYMLINK,
    UI_ACTION_UPDATE,
    UI_ACTION_EQUAL,
    UI_ACTION_DELETED,
    UI_ACTION_DELETE,
    UI_ACTION_IGNORE,
    NUM_UI_ACTIONS
};

enum CecupReason {
    UI_REASON_NONE = 0,
    UI_REASON_NEW,
    UI_REASON_HARDLINK,
    UI_REASON_SYMLINK,
    UI_REASON_UPDATE,
    UI_REASON_EQUAL,
    UI_REASON_IGNORED,
    UI_REASON_MISSING,
    NUM_UI_REASONS
};

static char *action_emojis[] = {
    [UI_ACTION_NONE]     = "",
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
    [UI_ACTION_NONE]     = "",
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
    [UI_ACTION_NONE]     = "",
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
    [UI_REASON_NONE]     = "",
    [UI_REASON_EQUAL]    = N_("Files have the same size and modification time"),
    [UI_REASON_IGNORED]  = N_("Matches an ignore rule"),
    [UI_REASON_MISSING]  = N_("File does not exist in the original folder"),
    [UI_REASON_NEW]      = N_("New file found in the original folder"),
    [UI_REASON_HARDLINK] = N_("Hardlinked file in the original folder"),
    [UI_REASON_SYMLINK]  = N_("Symlink in the original folder"),
    [UI_REASON_UPDATE]   = N_("The original file is newer"),
};

enum CecupColumn : int {
    COL_SELECTED = 0,
    COL_SRC_ACTION,
    COL_DST_ACTION,
    COL_SRC_PATH,
    COL_DST_PATH,
    COL_SIZE_TEXT,
    COL_SIZE_RAW,
    COL_MTIME_TEXT,
    COL_MTIME_RAW,
    COL_SRC_COLOR,
    COL_DST_COLOR,
    COL_REASON,
    COL_ROW_PTR,
    NUM_COLS
};

typedef struct CecupRow {
    int32 selected;
    enum CecupAction src_action;
    enum CecupAction dst_action;
    char *src_path;
    int64 src_path_len;
    char *dst_path;
    int64 dst_path_len;
    char *link_target;
    int64 link_target_len;
    char size_text[16];
    int64 size_raw;
    char mtime_text[32];
    int64 mtime_raw;
    char *src_color;
    char *dst_color;
    enum CecupReason reason;
} CecupRow;

static struct {
    GtkWidget *gtk_window;
    GtkWidget *src_entry;
    GtkWidget *dst_entry;

    GtkWidget *diff_entry;
    GtkWidget *term_entry;

    GtkWidget *preview_button;
    GtkWidget *stop_button;
    GtkWidget *sync_button;

    GtkWidget *ignore_button;
    GtkWidget *fix_button;

    GtkWidget *check_fs;
    GtkWidget *check_equal;
    GtkWidget *delete_excluded;
    GtkWidget *delete_after;
    
    GtkWidget *filter_new;
    GtkWidget *filter_hard;
    GtkWidget *filter_update;
    GtkWidget *filter_equal;
    GtkWidget *filter_delete;
    GtkWidget *filter_ignore;

    GtkListStore *store;
    GtkTextBuffer *log_buffer;
    char ignore_path[MAX_PATH_LENGTH];
    char config_path[MAX_PATH_LENGTH];
    volatile bool cancel_sync;
    GtkWidget *l_tree;
    GtkWidget *r_tree;
    GtkWidget *stats_label;

    GtkWidget *progress_rsync;
    GtkWidget *progress_equal;
    GtkWidget *progress_preview;

    CecupRow **rows;
    int32 rows_count;
    int32 rows_capacity;

    CecupRow **visible_rows;
    int32 visible_count;

    enum CecupColumn sort_col;
    GtkSortType sort_order;
    uint32 refresh_id;

    Arena *row_arena;
    GMutex row_arena_mutex;
    Arena *ui_arena;
    GMutex ui_arena_mutex;
} cecup;

typedef struct ThreadData {
    char src_path[MAX_PATH_LENGTH];
    char dst_path[MAX_PATH_LENGTH];
    bool is_preview;
    bool check_different_fs;
    bool delete_excluded;
    bool delete_after;
    bool scan_equal;
} ThreadData;

enum DataType {
    DATA_TYPE_LOG,
    DATA_TYPE_TREE_ROW,
    DATA_TYPE_TREE_ROW_BATCH,
    DATA_TYPE_REMOVE_TREE_ROW,
    DATA_TYPE_ENABLE_BUTTONS,
    DATA_TYPE_CLEAR_TREES,
    DATA_TYPE_PROGRESS_RSYNC,
    DATA_TYPE_PROGRESS_EQUAL,
    DATA_TYPE_PROGRESS_PREVIEW
};

typedef struct UIUpdateData {
    struct UIUpdateData *batch;
    int32 batch_count;
    enum DataType type;
    enum CecupAction action;
    enum CecupReason reason;

    char *message;
    int64 message_len;

    char *filepath;
    int64 filepath_length;
    char *link_target;
    int64 link_target_len;
    char *src_base;
    int64 src_base_len;
    char *dst_base;
    int64 dst_base_len;
    int64 size;
    int64 mtime;

    char *diff_tool;
    int64 diff_tool_len;
    char *term_cmd;
    int64 term_cmd_len;

    int32 side;
    double fraction;
} UIUpdateData;

static gboolean update_ui_handler(void * user_data);
static void on_preview_clicked(GtkWidget *b, void * data);
static void refresh_ui_list(void);
static gboolean refresh_ui_timeout_callback(void * data);

#endif /* CECUP_H */
