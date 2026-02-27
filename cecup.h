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

#define EMOJI_NEW "➕"
#define EMOJI_LINK "🔗"
#define EMOJI_UPDATE "➡️"
#define EMOJI_EQUAL "🟰"
#define EMOJI_DELETE "❌"
#define EMOJI_IGNORE "💤"

#define MAX_PATH_LENGTH 4096

enum CecupAction {
    UI_ACTION_NONE = 0,
    UI_ACTION_NEW,
    UI_ACTION_HARDLINK,
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
    [UI_ACTION_UPDATE]   = EMOJI_UPDATE,
    [UI_ACTION_EQUAL]    = EMOJI_EQUAL,
    [UI_ACTION_DELETED]  = EMOJI_DELETE,
    [UI_ACTION_DELETE]   = EMOJI_DELETE,
    [UI_ACTION_IGNORE]   = EMOJI_IGNORE
};

static char *src_action_strings[] = {
    [UI_ACTION_NONE]     = "",
    [UI_ACTION_NEW]      = "Copy to destination",
    [UI_ACTION_HARDLINK] = "Hardlink to destination",
    [UI_ACTION_UPDATE]   = "Update destination",
    [UI_ACTION_EQUAL]    = "Equal",
    [UI_ACTION_DELETED]  = "Missing in source",
    [UI_ACTION_DELETE]   = "",
    [UI_ACTION_IGNORE]   = "Ignore"
};

static char *dst_action_strings[] = {
    [UI_ACTION_NONE]     = "",
    [UI_ACTION_NEW]      = "Copy from source",
    [UI_ACTION_HARDLINK] = "Hardlink from source",
    [UI_ACTION_UPDATE]   = "Update from source",
    [UI_ACTION_EQUAL]    = "Equal",
    [UI_ACTION_DELETED]  = "",
    [UI_ACTION_DELETE]   = "Delete from destination",
    [UI_ACTION_IGNORE]   = "Ignore"
};

static char *reason_strings[] = {
    [UI_REASON_NONE]     = "",
    [UI_REASON_EQUAL]    = "Files have the same size and modification time",
    [UI_REASON_IGNORED]  = "Matched ignore pattern",
    [UI_REASON_MISSING]  = "Missing in source directory",
    [UI_REASON_NEW]      = "New file in source directory",
    [UI_REASON_HARDLINK] = "Hardlink in source directory",
    [UI_REASON_UPDATE]   = "Modification time is newer in the source directory",
};

enum {
    COL_SELECTED = 0,
    COL_SRC_ACTION,
    COL_DST_ACTION,
    COL_SRC_PATH,
    COL_DST_PATH,
    COL_SIZE_TEXT,
    COL_SIZE_RAW,
    COL_SRC_COLOR,
    COL_DST_COLOR,
    COL_REASON,
    NUM_COLS
};

typedef struct CecupRow {
    int32 selected;
    enum CecupAction src_action;
    enum CecupAction dst_action;
    char *src_path;
    char *dst_path;
    char *link_target;
    char size_text[32];
    int64 size_raw;
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
    GtkWidget *sync_button;
    GtkWidget *stop_button;
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
    volatile int32 cancel_sync;
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

    int32 sort_col;
    GtkSortType sort_order;
    uint32 refresh_id;

    Arena *row_arena;
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
    DATA_TYPE_REMOVE_TREE_ROW,
    DATA_TYPE_ENABLE_BUTTONS,
    DATA_TYPE_CLEAR_TREES,
    DATA_TYPE_PROGRESS_RSYNC,
    DATA_TYPE_PROGRESS_EQUAL,
    DATA_TYPE_PROGRESS_PREVIEW
};

typedef struct UIUpdateData {
    char *message;
    enum CecupAction action;
    char *filepath;
    char *link_target;
    enum CecupReason reason;
    char *src_base;
    char *dst_base;
    char *diff_tool;
    char *term_cmd;
    int64 size;
    int32 side;
    double fraction;
    enum DataType type;
} UIUpdateData;

static gboolean update_ui_handler(gpointer user_data);
static void on_preview_clicked(GtkWidget *b, gpointer data);
static void refresh_ui_list(void);
static gboolean refresh_ui_timeout_callback(gpointer data);

#endif /* CECUP_H */
