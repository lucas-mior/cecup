#if !defined(CECUP_H)
#define CECUP_H

#include <gtk/gtk.h>
#include "generic.c"

#define MAX_FILE_LENGTH 4096

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
    char *src_action;
    char *dst_action;
    char *src_path;
    char *dst_path;
    char *size_text;
    int64 size_raw;
    char *src_color;
    char *dst_color;
    char *reason;
} CecupRow;

typedef struct CecupState {
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

    GPtrArray *rows;
    int32 sort_col;
    GtkSortType sort_order;
    uint32 refresh_id;
} CecupState;

typedef struct ThreadData {
    CecupState *widgets;
    char src_path[MAX_FILE_LENGTH];
    char dst_path[MAX_FILE_LENGTH];
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
    CecupState *widgets;
    char *message;
    char *action;
    char *filepath;
    char *reason;
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
static void refresh_ui_list(CecupState *w);
static gboolean refresh_ui_timeout_callback(gpointer data);

#endif /* CECUP_H */
