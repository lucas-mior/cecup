#if !defined(CECUP_H)
#define CECUP_H

#include <gtk/gtk.h>
#include "generic.c"

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
    UI_REASON_EXCLUDED,
    UI_REASON_MISSING,
    NUM_UI_REASONS
};

static const char *action_strings[] = {
    [UI_ACTION_NONE]     = "",
    [UI_ACTION_NEW]      = "New",
    [UI_ACTION_HARDLINK] = "Hardlink",
    [UI_ACTION_UPDATE]   = "Update",
    [UI_ACTION_EQUAL]    = "Equal",
    [UI_ACTION_DELETED]  = "Deleted",
    [UI_ACTION_DELETE]   = "Delete",
    [UI_ACTION_IGNORE]   = "Ignore"
};

static const char *reason_strings[] = {
    [UI_REASON_NONE]      = "",
    [UI_REASON_EQUAL]     = "Files have the same name, size and modification time",
    [UI_REASON_EXCLUDED]  = "Matched exclusion pattern",
    [UI_REASON_MISSING]   = "Missing in source directory",
    [UI_REASON_NEW]       = "New file in source directory",
    [UI_REASON_HARDLINK]  = "Hardlink in source directory",
    [UI_REASON_UPDATE]    = "Updated in source directory"
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
    char *size_text;
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
    GtkWidget *exclude_button;
    GtkWidget *check_fs_toggle;
    
    GtkWidget *filter_new;
    GtkWidget *filter_hard;
    GtkWidget *filter_update;
    GtkWidget *filter_equal;
    GtkWidget *filter_delete;
    GtkWidget *filter_ignore;

    GtkListStore *store;
    GtkTextBuffer *log_buffer;
    char *exclude_path;
    char *config_path;
    volatile int32 cancel_sync;
    GtkWidget *l_tree;
    GtkWidget *r_tree;

    CecupRow **rows;
    int32 rows_count;
    int32 rows_capacity;

    CecupRow **visible_rows;
    int32 visible_count;

    int32 sort_col;
    GtkSortType sort_order;
    uint32 refresh_id;
} cecup_state;

typedef struct ThreadData {
    char src_path[MAX_PATH_LENGTH];
    char dst_path[MAX_PATH_LENGTH];
    bool is_preview;
    bool check_different_fs;
    bool show_equal;
} ThreadData;

enum DataType {
    DATA_TYPE_LOG,
    DATA_TYPE_TREE_ROW,
    DATA_TYPE_REMOVE_TREE_ROW,
    DATA_TYPE_ENABLE_BUTTONS,
    DATA_TYPE_CLEAR_TREES,
};

typedef struct UIUpdateData {
    char *message;
    enum CecupAction action;
    char *filepath;
    enum CecupReason reason;
    char *src_base;
    char *dst_base;
    char *diff_tool;
    char *term_cmd;
    int64 size;
    int32 side;
    enum DataType type;
} UIUpdateData;

static gboolean update_ui_handler(gpointer user_data);
static void on_preview_clicked(GtkWidget *b, gpointer data);
static void refresh_ui_list(void);
static gboolean refresh_ui_timeout_callback(gpointer data);

#endif /* CECUP_H */
