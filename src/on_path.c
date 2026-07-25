// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(ON_PATH_C)
#define ON_PATH_C

#include "gtk_include.h"

#include "util.c"
#include "update.c"
#include "cecup.h"
#include "work_rsync.c"

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_on_path 1
#elif !defined(TESTING_on_path)
#define TESTING_on_path 0
#endif
#if !defined(TESTING)
#define TESTING 0
#endif

typedef struct SelectionData {
    GtkEditable *editable;
    int32 start_pos;
    int32 end_pos;
} SelectionData;

static gboolean
on_path_selection_idle(void *data) {
    SelectionData *selection_data = data;

    gtk_editable_select_region(selection_data->editable,
                               selection_data->start_pos, selection_data->end_pos);

    free2(selection_data, sizeof(*selection_data));
    return G_SOURCE_REMOVE;
}

static void
on_path_editing_started(GtkEditable *editable, void *data) {
    GtkWidget *tree = data;
    int32 side;
    int32 row_id;
    void *row_id_ptr;
    char *relative;

    if (!GTK_IS_EDITABLE(editable) || (tree == NULL)) {
        return;
    }

    side = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(tree), "side"));

    if ((row_id_ptr = g_object_get_data(G_OBJECT(editable), "cecup-row-id")) == NULL) {
        return;
    }

    row_id = GPOINTER_TO_INT(row_id_ptr) - 1;
    relative = item_path_side(row_id, side);

    if (relative) {
        char *name;
        char *last_dot;
        int32 name_len;
        int32 start_pos;
        int32 end_pos;
        int32 path_len;

        path_len = item_path_len_side(row_id, side);
        end_pos = path_len;

        name = basename2(relative, &path_len, &name_len);
        last_dot = memrchr64(name, '.', name_len);

        start_pos = path_len - name_len;

        ASSERT_MORE(path_len, 0);

        if (last_dot) {
            if (last_dot != name) {
                end_pos = (int32)(last_dot - relative);
            }
        } else if (relative[path_len - 1] == '/') {
            end_pos = path_len - 1;
        }

        if (end_pos > start_pos) {
            SelectionData *selection_data = malloc2(SIZEOF(*selection_data));
            memset64(selection_data, 0, SIZEOF(*selection_data));

            selection_data->editable = editable;
            selection_data->start_pos = start_pos;
            selection_data->end_pos = end_pos;

            g_idle_add(on_path_selection_idle, selection_data);
        }
    }

    return;
}

static void
on_path_edited(GtkEditable *editable, void *data) {
    GtkWidget *tree = data;
    int32 row_id;
    void *row_id_ptr;
    int8 side;
    char *base_path;
    int32 base_path_len;
    char old_full[MAX_PATH_LENGTH];
    char *relative_old;
    int32 new_length;
    char *new_text;
    MessageBatch *batch = NULL;
    char new_full[MAX_PATH_LENGTH];
    char relative_new[MAX_PATH_LENGTH];
    int32 old_length;
    int32 new_full_length;

    side = (int8)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(tree), "side"));

    if ((row_id_ptr = g_object_get_data(G_OBJECT(editable), "cecup-row-id")) == NULL) {
        return;
    }

    row_id = GPOINTER_TO_INT(row_id_ptr) - 1;
    new_text = (char *)gtk_editable_get_text(editable);
    relative_old = item_path_side(row_id, side);

    if (side == L) {
        base_path = cecup.src_base;
        base_path_len = cecup.src_base_len;
    } else {
        base_path = cecup.dst_base;
        base_path_len = cecup.dst_base_len;
    }

    if (relative_old == NULL || (strcmp(relative_old, new_text) == 0)) {
        return;
    }

    new_length = strlen32(new_text);
    if (new_length >= (MAX_PATH_LENGTH / 2)) {
        LOG_ERROR("Error renaming: new path is too long.\n");
        return;
    }
    if (new_length <= 0) {
        LOG_ERROR("Error renaming: new length is zero.\n");
        return;
    }

    SNPRINTF(old_full, "%s/%s", base_path, relative_old);

    old_length = strlen32(relative_old);
    memcpy64(relative_new, new_text, new_length + 1);
    normalize(relative_new, &new_length);

    if (BEGINS_WITH(relative_new, new_length, "/")) {
        LOG_ERROR(_("Invalid rename: %s starts with a slash.\n"), relative_new);
        return;
    }
    if (BEGINS_WITH(relative_new, new_length, "..")) {
        LOG_ERROR(_("Invalid rename: %s starts with ..\n"), relative_new);
        return;
    }

    new_full_length = SNPRINTF(new_full, "%s/%s", base_path, relative_new);
    normalize(new_full, &new_full_length);

    if (renameat2(AT_FDCWD, old_full, AT_FDCWD, new_full, RENAME_NOREPLACE) < 0) {
        LOG_ERROR(_("Error renaming %s to %s: %s\n"), old_full, new_full, strerror(errno));
        return;
    }

    LOG(_("Renamed: %s -> %s\n"), relative_old, relative_new);

    if ((relative_old[old_length - 1] == '/') && (relative_new[new_length - 1] != '/')) {
        relative_new[new_length] = '/';
        relative_new[new_length + 1] = '\0';
        new_length += 1;
    }

    if (relative_new[new_length - 1] == '/') {
        char *paths[] = {new_full, NULL};
        FTS *fts_handle;
        FTSENT *entry;

        if ((fts_handle = fts_open(paths, FTS_PHYSICAL | FTS_NOCHDIR, NULL)) == NULL) {
            error("Error in fts_open(%s): %s.\n", paths[0], strerror(errno));
            aux_invalidate_preview();
            work_batch_flush(&batch);
            return;
        }

        errno = 0;
        while ((entry = fts_read(fts_handle))) {
            char *child_rel_new;
            char child_rel_old[MAX_PATH_LENGTH];
            int32 child_rel_new_len;
            int32 child_rel_old_len;
            int32 suffix_len;
            bool is_dir;

            /* Skip the root directory itself as it's already pushed */
            if (entry->fts_level == 0) {
                continue;
            }
            switch (entry->fts_info) {
            case FTS_D:
                    continue;
            case FTS_ERR:
            case FTS_NS:
                error("FTS error on %s: %s.\n", entry->fts_path, strerror(entry->fts_errno));
                continue;
            default:
                break;
            }
            is_dir = entry->fts_info == FTS_DP;

            child_rel_new = entry->fts_path + base_path_len;
            child_rel_new_len = (int32)entry->fts_pathlen - base_path_len;
            if (child_rel_new[0] == '/') {
                child_rel_new += 1;
                child_rel_new_len -= 1;
            }

            /* Construct old relative path by swapping prefixes */
            suffix_len = child_rel_new_len - new_length;
            child_rel_old_len = old_length + suffix_len;
            memcpy64(child_rel_old, relative_old, old_length);
            memcpy64(child_rel_old + old_length, child_rel_new + new_length, suffix_len + 1);

            normalize(child_rel_old, &child_rel_old_len);

            if (is_dir && (child_rel_old[child_rel_old_len - 1] != '/')) {
                child_rel_old_len += 1;
                child_rel_old[child_rel_old_len - 1] = '/';
                child_rel_old[child_rel_old_len] = '\0';
            }

            work_batch_push_rename(&batch, MSG_BATCH_ROW_RENAME, side,
                                   child_rel_old, child_rel_old_len,
                                   child_rel_new, child_rel_new_len);
            errno = 0;
        }
        if (errno) {
            LOG_ERROR(_("Error in fts_read(%s): %s.\n"), new_full, strerror(errno));
        }
        if (fts_close(fts_handle) < 0) {
            LOG_ERROR(_("Error in fts_close(%s): %s.\n"), new_full, strerror(errno));
        }
    }

    work_batch_push_rename(&batch, MSG_BATCH_ROW_RENAME, side,
                           relative_old, old_length, relative_new, new_length);

    aux_invalidate_preview();
    work_batch_flush(&batch);
    return;
}

static void
on_path_editing_notify(GObject *object, GParamSpec *pspec, void *data) {
    gboolean is_editing;

    (void)pspec;
    is_editing = gtk_editable_label_get_editing(GTK_EDITABLE_LABEL(object));

    if (is_editing) {
        on_path_editing_started(GTK_EDITABLE(g_object_ref(object)), data);
    } else {
        on_path_edited(GTK_EDITABLE(g_object_ref(object)), data);
    }

    return;
}

#if (0 == TESTING_on_path) && TESTING
static inline void
on_path_functions_sink(void) {
    (void)on_path_functions_sink;
    (void)on_path_editing_notify;
    return;
}
#endif

#if TESTING_on_path
#define CBASE_IMPLEMENT
#include "cbase.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "work.c"

int
main(void) {
    GtkWidget *tree;
    GtkWidget *label;
    GParamSpec *pspec;
    GtkWidget *box_parent;
    GtkWidget *paned;
    char *src_dir = "/tmp/cecup_test_src";
    char *file_rel = "test_file.txt";
    char src_file_full[MAX_PATH_LENGTH];
    int32 n = 1;

    if (!gtk_init_check()) {
        exit(EXIT_SUCCESS);
    }

    // 1. Setup Global State and Filesystem
    memset64(&cecup, 0, SIZEOF(cecup));
    cecup.src_base = xmemdup(src_dir, strlen32(src_dir) + 1);
    cecup.src_base_len = strlen32(src_dir);

    mkdir(src_dir, 0755);
    SNPRINTF(src_file_full, "%s/%s", src_dir, file_rel);
    close(open(src_file_full, O_CREAT | O_RDWR, 0644));

    cecup.rows_len = n;
    cecup.rows[L] = malloc2(n * SIZEOF(int32));
    cecup.rows[L][0] = 0;

    {
        Traversal *t = &cecup.traversal[L];
        t->nfiles = n;
        t->paths = malloc2(n * SIZEOF(char *));
        t->paths_lens = malloc2(n * SIZEOF(int16));
        t->paths[0] = xmemdup(file_rel, strlen32(file_rel) + 1);
        t->paths_lens[0] = (int16)strlen32(file_rel);

        t->stats = malloc2(n * SIZEOF(struct stat));
        t->patterns = malloc2(n * SIZEOF(char *));
        t->symlink_targets = malloc2(n * SIZEOF(char *));
        memset64(t->stats, 0, n * SIZEOF(struct stat));
        memset64(t->patterns, 0, n * SIZEOF(char *));
        memset64(t->symlink_targets, 0, n * SIZEOF(char *));
        t->map = hash_create_fs_map(INITIAL_CAPACITY, "t->map");
    }

    // 2. Setup Widgets
    cecup.stop_button = gtk_button_new();
    g_object_ref_sink(cecup.stop_button);
    cecup.sync_button = gtk_button_new();
    g_object_ref_sink(cecup.sync_button);

    box_parent = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    g_object_ref_sink(box_parent);
    paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_append(GTK_BOX(box_parent), paned);
    cecup.tree[L] = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_paned_set_start_child(GTK_PANED(paned), cecup.tree[L]);

    label = gtk_editable_label_new(file_rel);
    g_object_ref_sink(label);

    tree = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    g_object_ref_sink(tree);
    g_object_set_data(G_OBJECT(tree), "side", GINT_TO_POINTER(L));
    g_object_set_data(G_OBJECT(label), "cecup-row-id", GINT_TO_POINTER(1));

    // 3. Test on_path_editing_notify (routing)
    pspec = g_param_spec_boolean("editing", "editing", "editing", FALSE, G_PARAM_READWRITE);

    // Simulate start editing
    gtk_editable_label_start_editing(GTK_EDITABLE_LABEL(label));
    on_path_editing_notify(G_OBJECT(label), pspec, tree);

    // Simulate end editing (without changing text yet)
    gtk_editable_label_stop_editing(GTK_EDITABLE_LABEL(label), FALSE);
    on_path_editing_notify(G_OBJECT(label), pspec, tree);

    // 4. Test on_path_edited (The Rename syscall)
    {
        char *new_name = "renamed_file.txt";
        char new_file_full[MAX_PATH_LENGTH];

        gtk_editable_set_text(GTK_EDITABLE(label), new_name);

        cecup.preview_dirty = false;
        on_path_edited(GTK_EDITABLE(label), tree);

        SNPRINTF(new_file_full, "%s/%s", src_dir, new_name);
        ASSERT(access(new_file_full, F_OK) == 0);
        ASSERT(access(src_file_full, F_OK) == -1);
        ASSERT(cecup.preview_dirty == true);
    }

    // 5. Cleanup
    {
        Traversal *t = &cecup.traversal[L];
        free2(t->paths[0], strlen32(t->paths[0]) + 1);
        free2(t->paths, n * SIZEOF(char *));
        free2(t->paths_lens, n * SIZEOF(int16));
        free2(t->stats, n * SIZEOF(struct stat));
        free2(t->patterns, n * SIZEOF(char *));
        free2(t->symlink_targets, n * SIZEOF(char *));
        hash_destroy_fs_map(t->map);
        free2(cecup.rows[L], n * SIZEOF(int32));
        free2(cecup.src_base, cecup.src_base_len + 1);
    }

    g_object_unref(label);
    g_object_unref(tree);
    g_object_unref(cecup.stop_button);
    g_object_unref(cecup.sync_button);
    g_object_unref(box_parent);
    g_param_spec_unref(pspec);

    unlink("/tmp/cecup_test_src/renamed_file.txt");
    rmdir(src_dir);

    exit(EXIT_SUCCESS);
}

#endif /* TESTING_on_path */

#endif /* ON_PATH_C */
