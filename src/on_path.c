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

#if !defined (ON_PATH_C)
#define ON_PATH_C

#include <gtk/gtk.h>

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

    free(selection_data, sizeof(*selection_data));
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
            SelectionData *selection_data = xmalloc(SIZEOF(*selection_data));
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

    SNPRINTF(old_full, "%s/%s", base_path, relative_old);

    if (new_length > 0) {
        char new_full[MAX_PATH_LENGTH];
        char relative_new[MAX_PATH_LENGTH];
        int32 old_length;
        int32 new_full_length;

        old_length = strlen32(relative_old);
        memcpy64(relative_new, new_text, new_length + 1);
        normalize(relative_new, &new_length);

        if (BEGINS_WITH(relative_new, "/")) {
            LOG_ERROR(_("Invalid rename: %s starts with a slash.\n"), relative_new);
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

        work_batch_push_rename(&batch, MSG_ROW_RENAME, side,
                               relative_old, old_length, relative_new, new_length);

        if (relative_new[new_length - 1] == '/') {
            char *paths[2];
            FTS *fts_handle;
            FTSENT *entry;

            paths[0] = new_full;
            paths[1] = NULL;
            fts_handle = fts_open(paths, FTS_PHYSICAL | FTS_NOCHDIR, NULL);

            if (fts_handle != NULL) {
                while ((entry = fts_read(fts_handle))) {
                    char *child_rel_new;
                    char child_rel_old[MAX_PATH_LENGTH];
                    int32 child_rel_new_len;
                    int32 child_rel_old_len;
                    int32 suffix_len;

                    /* Skip the root directory itself as it's already pushed */
                    if (entry->fts_level == 0) {
                        continue;
                    }

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

                    work_batch_push_rename(&batch, MSG_ROW_RENAME, side,
                                           child_rel_old, child_rel_old_len,
                                           child_rel_new, child_rel_new_len);
                }
                fts_close(fts_handle);
            }
        }

        aux_invalidate_preview();
        work_batch_flush(&batch);
    }

    return;
}

static void
on_path_editing_notify(GObject *object, GParamSpec *pspec, void *data) {
    gboolean is_editing;

    (void)pspec;
    is_editing = gtk_editable_label_get_editing(GTK_EDITABLE_LABEL(object));

    if (is_editing) {
        // TODO: Memory Leak. Calling `g_object_ref(object)` increments the reference count, but
        // `on_path_editing_started` does not drop the reference. This leads to leaking the object
        // every time editing begins.
        on_path_editing_started(GTK_EDITABLE(g_object_ref(object)), data);
    } else {
        // TODO: Memory Leak. Similar to the branch above, `on_path_edited` does not unref the
        // object. This leads to leaking the object every time editing finishes.
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

#include "work.c"
#include "assert.c"

int main(void) {
    GtkWidget *entry;
    SelectionData *data;
    int start_pos;
    int end_pos;
    char filename[64] = "dir/file.txt";
    int32 filename_len = strlen32(filename);

    if (!gtk_init_check()) {
        exit(EXIT_SUCCESS);
    }

    entry = gtk_entry_new();
    g_object_ref_sink(entry);
    gtk_editable_set_text(GTK_EDITABLE(entry), filename);

    data = xmalloc(SIZEOF(*data));
    memset64(data, 0, SIZEOF(*data));
    data->editable = GTK_EDITABLE(entry);
    data->start_pos = 4;
    data->end_pos = 8;

    on_path_selection_idle(data);

    {
        int32 base_len;
        char *base = basename2(filename, &filename_len, &base_len);
        int64 expected_start = base - filename;
        int64 expected_end = (char *)memchr(filename, '.', filename_len) - filename;

        ASSERT(gtk_editable_get_selection_bounds(GTK_EDITABLE(entry), &start_pos, &end_pos));
        ASSERT_EQUAL((int32)start_pos, expected_start);
        ASSERT_EQUAL((int32)end_pos, expected_end);
    }

    g_object_unref(entry);

    exit(EXIT_SUCCESS);
}
#endif

#endif /* ON_PATH_C */
