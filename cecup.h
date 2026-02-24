#if !defined(CECUP_H)
#define CECUP_H

#include <gtk/gtk.h>
#include "generic.c"

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

typedef struct AppWidgets {
    GtkWidget *gtk_window;
    GtkWidget *src_entry;
    GtkWidget *dst_entry;
    GtkWidget *diff_entry;
    GtkWidget *term_entry;
    GtkWidget *preview_button;
    GtkWidget *sync_button;
    GtkWidget *exclude_button;
    GtkWidget *check_fs_toggle;
    GtkListStore *store;
    GtkTextBuffer *log_buffer;
    char *exclude_path;
    char *config_path;
} AppWidgets;

typedef struct ThreadData {
    AppWidgets *widgets;
    char src_path[1024];
    char dst_path[1024];
    int32 is_preview;
    int32 check_different_fs;
} ThreadData;

enum DataType {
    DATA_TYPE_LOG,
    DATA_TYPE_TREE_ROW,
    DATA_TYPE_REMOVE_TREE_ROW,
    DATA_TYPE_ENABLE_BUTTONS,
    DATA_TYPE_CLEAR_TREES,
};

typedef struct UIUpdateData {
    AppWidgets *widgets;
    char *message;
    char *action;
    char *filepath;
    char *reason;
    int64 size;
    int32 side;
    enum DataType type;
} UIUpdateData;

static gboolean update_ui_handler(gpointer user_data);

#endif /* CECUP_H */
