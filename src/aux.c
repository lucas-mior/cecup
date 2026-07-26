// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(AUX_C)
#define AUX_C

#include "gtk_include.h"

#include "cecup.h"
#include "item.c"
#include "tasks.c"

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_aux 1
#elif !defined(TESTING_aux)
#define TESTING_aux 0
#endif
#if !defined(TESTING)
#define TESTING 0
#endif

static bool
aux_is_root(char *path) {
    ASSERT(path != NULL);

    if (path[0] == '.') {
        if (path[1] == '\0') {
            return true;
        }
        if ((path[1] == '/') && (path[2] == '\0')) {
            return true;
        }
    }
    return false;
}

static void
stop_working(bool state) {
    xpthread_mutex_lock(&cecup.stop_lock);
    cecup.stop_working = state;
    xpthread_mutex_unlock(&cecup.stop_lock);
    return;
}

static void
on_banner_response(GtkInfoBar *info_bar, int32 response_id, void *data) {
    (void)response_id;
    (void)data;
    gtk_widget_set_visible(GTK_WIDGET(info_bar), FALSE);
    return;
}

static void
aux_invalidate_preview(void) {
    static GtkWidget *warning_banner = NULL;

    cecup.preview_dirty = true;
    if (!gtk_widget_get_sensitive(cecup.stop_button)) {
        gtk_widget_set_sensitive(cecup.sync_button, FALSE);
        gtk_widget_set_tooltip_text(cecup.sync_button, _("Click Analysis first"));
    }

    if (warning_banner == NULL) {
        GtkWidget *label = gtk_label_new(_("Alterations were made. File list may not be entirely consistent"));
        GtkWidget *parent = gtk_widget_get_parent(cecup.tree[L]);

        warning_banner = gtk_info_bar_new();
        gtk_info_bar_set_message_type(GTK_INFO_BAR(warning_banner), GTK_MESSAGE_WARNING);
        gtk_info_bar_add_child(GTK_INFO_BAR(warning_banner), label);
        gtk_info_bar_set_show_close_button(GTK_INFO_BAR(warning_banner), TRUE);
        g_signal_connect(warning_banner, "response", G_CALLBACK(on_banner_response), NULL);

        while (parent != NULL) {
            if (GTK_IS_PANED(parent)) {
                GtkWidget *paned_parent = gtk_widget_get_parent(parent);
                if (paned_parent != NULL && GTK_IS_BOX(paned_parent)) {
                    gtk_box_append(GTK_BOX(paned_parent), warning_banner);
                    break;
                }
            }
            parent = gtk_widget_get_parent(parent);
        }
    } else {
        gtk_widget_set_visible(warning_banner, TRUE);
    }

    return;
}

static void
aux_protect_interface_from_user(bool state) {
    // this function is called everytime a thread is spawn.
    // question: is this enough to avoid race conditions?

    gtk_widget_set_sensitive(cecup.preview_button, !state);
    gtk_widget_set_sensitive(cecup.ignore_button, !state);
    gtk_widget_set_sensitive(cecup.dir_entry[L], !state);
    gtk_widget_set_sensitive(cecup.dir_entry[R], !state);
    gtk_widget_set_sensitive(cecup.invert_button, !state);

    // stop is the only button that remains clickable
    gtk_widget_set_sensitive(cecup.stop_button, state);

    gtk_widget_set_sensitive(cecup.delete_after_button, !state);
    gtk_widget_set_sensitive(cecup.delete_ignored_button, !state);
    gtk_widget_set_sensitive(cecup.check_fs_button, !state);
    gtk_widget_set_sensitive(cecup.diff_entry, !state);
    gtk_widget_set_sensitive(cecup.term_entry, !state);
    gtk_widget_set_sensitive(cecup.browse_button[L], !state);
    gtk_widget_set_sensitive(cecup.browse_button[R], !state);

    gtk_widget_set_sensitive(cecup.filter_update, !state);
    gtk_widget_set_sensitive(cecup.filter_new,    !state);
    gtk_widget_set_sensitive(cecup.filter_delete, !state);
    gtk_widget_set_sensitive(cecup.filter_link,   !state);
    gtk_widget_set_sensitive(cecup.filter_ignore, !state);

    if (state) {
        gtk_widget_set_sensitive(cecup.sync_button, FALSE);
    } else if (cecup.preview_dirty) {
        gtk_widget_set_sensitive(cecup.sync_button, FALSE);
        gtk_widget_set_tooltip_text(cecup.sync_button, _("Click Analysis first"));
    } else {
        gtk_widget_set_sensitive(cecup.sync_button, TRUE);
        gtk_widget_set_tooltip_text(cecup.sync_button, _("Start copying and updating all files"));
    }
    stop_working(false);
    return;
}

static void
cecup_reset_dir(int32 side) {
    g_signal_handler_block(cecup.dir_entry[side], cecup.entry_id[side]);
    gtk_editable_set_text(GTK_EDITABLE(cecup.dir_entry[side]), "./");
    g_signal_handler_unblock(cecup.dir_entry[side], cecup.entry_id[side]);
    aux_invalidate_preview();
    return;
}

static void
cecup_set_dirs(char *new_src, int32 new_src_len, char *new_dst, int32 new_dst_len) {
    if (new_src) {
        if (cecup.src_base) {
            free2(cecup.src_base, cecup.src_base_len + 1);
        }
        cecup.src_base = malloc2(new_src_len + 1);
        memcpy64(cecup.src_base, new_src, new_src_len + 1);
        cecup.src_base_len = new_src_len;
    }

    if (new_dst) {
        if (cecup.dst_base) {
            free2(cecup.dst_base, cecup.dst_base_len + 1);
        }
        cecup.dst_base = malloc2(new_dst_len + 1);
        memcpy64(cecup.dst_base, new_dst, new_dst_len + 1);
        cecup.dst_base_len = new_dst_len;
    }

    g_signal_handler_block(cecup.dir_entry[L], cecup.entry_id[L]);
    g_signal_handler_block(cecup.dir_entry[R], cecup.entry_id[R]);

    if (cecup.src_base) {
        gtk_editable_set_text(GTK_EDITABLE(cecup.dir_entry[L]), cecup.src_base);
    }
    if (cecup.dst_base) {
        gtk_editable_set_text(GTK_EDITABLE(cecup.dir_entry[R]), cecup.dst_base);
    }

    g_signal_handler_unblock(cecup.dir_entry[L], cecup.entry_id[L]);
    g_signal_handler_unblock(cecup.dir_entry[R], cecup.entry_id[R]);

    return;
}

static bool
cecup_get_dirs(void) {
    char full_src[PATH_MAX];
    char full_dst[PATH_MAX];
    int32 full_src_len;
    int32 full_dst_len;
    char *tmp_src;
    char *tmp_dst;

    tmp_src = (char *)gtk_editable_get_text(GTK_EDITABLE(cecup.dir_entry[L]));
    tmp_dst = (char *)gtk_editable_get_text(GTK_EDITABLE(cecup.dir_entry[R]));

    save_config();

    if (strlen32(tmp_src) <= 0) {
        LOG_ERROR(_("Error: Invalid source directory.\n"));
        cecup_reset_dir(L);
        return false;
    }
    if (strlen32(tmp_dst) <= 0) {
        LOG_ERROR(_("Error: Invalid destination directory.\n"));
        cecup_reset_dir(R);
        return false;
    }

    if (realpath(tmp_src, full_src) == NULL) {
        LOG_ERROR(_("Error getting full path of %s: %s.\n"), tmp_src, strerror(errno));
        cecup_reset_dir(L);
        return false;
    }
    if (realpath(tmp_dst, full_dst) == NULL) {
        LOG_ERROR(_("Error getting full path of %s: %s.\n"), tmp_dst, strerror(errno));
        cecup_reset_dir(R);
        return false;
    }

    if (strequal(full_src, full_dst)) {
        LOG_ERROR(_("Error: source and backup are the same directory\n"));
        cecup_reset_dir(R);
        return false;
    }

    full_src_len = strlen32(full_src);
    full_dst_len = strlen32(full_dst);

    normalize(full_src, &full_src_len);
    normalize(full_dst, &full_dst_len);

    ASSERT_LESS(full_dst_len, PATH_MAX - 2);
    ASSERT_LESS(full_src_len, PATH_MAX - 2);

    if (full_src[full_src_len - 1] != '/') {
        full_src_len += 1;
        full_src[full_src_len - 1] = '/';
        full_src[full_src_len] = '\0';
    }
    if (full_dst[full_dst_len - 1] != '/') {
        full_dst_len += 1;
        full_dst[full_dst_len - 1] = '/';
        full_dst[full_dst_len] = '\0';
    }

    if (STREQUAL(full_src, full_src_len, "/") || STREQUAL(full_dst, full_dst_len, "/")) {
        LOG_ERROR(_("Error: directory can not be the root dir.\n"));
        return false;
    }

    if ((full_src_len > full_dst_len)
        && !memcmp64(full_src, full_dst, full_dst_len)) {
        LOG_ERROR(_("Error: source directory is contained in the "
                    "destination directory\n"));
        cecup_reset_dir(L);
        return false;
    }
    if ((full_dst_len > full_src_len)
        && !memcmp64(full_dst, full_src, full_src_len)) {
        LOG_ERROR(_("Error: destination directory is contained in the "
                    "source directory\n"));
        cecup_reset_dir(R);
        return false;
    }

    if (cecup.src_base && cecup.dst_base) {
        if (strequal(cecup.src_base, full_src) && strequal(cecup.dst_base, full_dst)) {
            cecup_set_dirs(NULL, 0, NULL, 0);
            return true;
        }
    }

    cecup_set_dirs(full_src, full_src_len, full_dst, full_dst_len);

    aux_invalidate_preview();

    return true;
}
static void
config_bool_set(GKeyFile *key, char *section, char *name, GtkWidget *button) {
    gboolean state;

    state = false;
    if (GTK_IS_CHECK_BUTTON(button)) {
        state = gtk_check_button_get_active(GTK_CHECK_BUTTON(button));
    } else if (GTK_IS_TOGGLE_BUTTON(button)) {
        state = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button));
    }

    g_key_file_set_boolean(key, section, name, state);
    return;
}

static void
save_config(void) {
    GKeyFile *key;
    char *out;
    gsize len;

    key = g_key_file_new();

    g_key_file_set_string(key, "Paths", "src",
                          gtk_editable_get_text(GTK_EDITABLE(cecup.dir_entry[L])));
    g_key_file_set_string(key, "Paths", "dst",
                          gtk_editable_get_text(GTK_EDITABLE(cecup.dir_entry[R])));
    g_key_file_set_string(key, "Tools", "diff",
                          gtk_editable_get_text(GTK_EDITABLE(cecup.diff_entry)));
    g_key_file_set_string(key, "Tools", "term",
                          gtk_editable_get_text(GTK_EDITABLE(cecup.term_entry)));

    config_bool_set(key, "Filters", "new",    cecup.filter_new);
    config_bool_set(key, "Filters", "hard",   cecup.filter_link);
    config_bool_set(key, "Filters", "update", cecup.filter_update);
    config_bool_set(key, "Filters", "equal",  cecup.filter_equal);
    config_bool_set(key, "Filters", "delete", cecup.filter_delete);
    config_bool_set(key, "Filters", "ignore", cecup.filter_ignore);

    config_bool_set(key, "Options", "check_fs",       cecup.check_fs_button);
    config_bool_set(key, "Options", "delete_after",   cecup.delete_after_button);
    config_bool_set(key, "Options", "delete_ignored", cecup.delete_ignored_button);

    out = g_key_file_to_data(key, &len, NULL);
    write_entire_file(cecup.config_path, out, (gssize)len);

    g_free(out);
    g_key_file_free(key);
    return;
}

static void
log_internal(char *file, int line, char *func, enum MsgType type, char *format, ...) {
    Message *message;
    char buffer[MAX_PATH_LENGTH*2];
    int32 n;
    int32 m;
    va_list va_args;
    char fileline[64];

    va_start(va_args, format);
    n = vsnprintf(buffer, SIZEOF(buffer), format, va_args);

    if ((n < 0) || (n >= SIZEOF(buffer))) {
        error_impl(file, line, func,
                   "Error in vsnprintf(\"%s\") (n = %d)\n", format, n);
        fatal(EXIT_FAILURE);
    }
    va_end(va_args);

    message = malloc2(SIZEOF(*message));
    memset64(message, 0, SIZEOF(*message));

    if (RELEASING) {
        m = SNPRINTF(fileline, "%s", "");
    } else {
        m = SNPRINTF(fileline, "%s:%d:%s ", file, line, func);
    }

    message->text_len = n + m;
    message->text = malloc2(n + m + 1);

    memcpy64(message->text, fileline, m);
    memcpy64(message->text + m, buffer, n + 1);

    message->type = type;
    g_idle_add(update_ui_handler, message);
    return;
}

static void
free_message(void *data) {
    Message *message;

    if ((message = data)) {
        free2(message->text, message->text_len + 1);
        if (message->dst_path != message->src_path) {
            // in this case,
            // dst_path might be NULL or a different allocation than src_path
            free2(message->src_path, message->src_path_len + 1);
            free2(message->dst_path, message->dst_path_len + 1);
        } else {
            // in this case,
            // either both are NULL (which free() simply ignores)
            // or only src_path is allocated.
            // dst_path might be the same pointer to the src_path allocation, or be NULL.
            free2(message->src_path, message->src_path_len + 1);
        }

        free2(message, SIZEOF(*message));
    }
    return;
}

static void
check_consistent_traversal_rows(Traversal *traversal, int32 *rows,
                                char *which_traversal, char *which_rows) {
    for (int32 idx = 0; idx < traversal->nfiles; idx += 1) {
        int32 idx_lookup;
        int32 row_id = traversal->row_ids[idx];
        char *path = traversal->paths[idx];
        int32 path_len = (int32)traversal->paths_lens[idx];
        bool lookup = hash_lookup_fs_map(traversal->map, path, path_len, &idx_lookup);

        if (row_id != -1) {
            if (row_id >= cecup.rows_len) {
                error("Consistency error: %s.row_ids[%d] points to invalid row %d.\n",
                      which_traversal, idx, row_id);
                fatal(EXIT_FAILURE);
            }
            if (rows[row_id] != idx) {
                error("Consistency error:"
                      " %s.row_ids["RED("%d")"] -> row_id="YELLOW("%d")","
                      " but %s["YELLOW("%d")"] -> idx="RED("%d")".\n",
                      which_traversal, idx, row_id,
                      which_rows, row_id, rows[row_id]);
                fatal(EXIT_FAILURE);
            }

            if (!lookup) {
                error("Consistency error:"
                      " %s index %d (path %s) is mapped to row but missing in hash map.\n",
                      which_traversal, idx, path);
                fatal(EXIT_FAILURE);
            } else if (idx_lookup != idx) {
                error("Consistency error:"
                      " %s index %d (path %s) is mapped to row but mismatched in hash map.\n",
                      which_traversal, idx, path);
                fatal(EXIT_FAILURE);
            }
        } else {
            if (lookup) {
                if (idx_lookup == idx) {
                    error("Consistency error:"
                          " %s index %d (path %s) has no row but exists in hash.\n",
                          which_traversal, idx, path);
                    fatal(EXIT_FAILURE);
                }
            }
        }
    }

    for (uint32 bucket_idx = 0; bucket_idx < traversal->map->capacity; bucket_idx += 1) {
        Bucket_fs_map *bucket;
        int32 v;

        bucket = &traversal->map->array[bucket_idx];
        if ((int64)bucket->key > 0) {
            v = bucket->value;
            if (v < 0 || v >= traversal->nfiles) {
                error("Consistency error: %s hash map contains invalid index %d.\n",
                      which_traversal, v);
                fatal(EXIT_FAILURE);
            }
            if (traversal->row_ids[v] == -1) {
                error("Consistency error: %s hash map contains index %d with no row_id.\n",
                      which_traversal, v);
                fatal(EXIT_FAILURE);
            }
        }
    }

    return;
}

#define CHECK_CONSISTENT_TRAVERSAL_ROWS(TRAVERSAl, ROWS) \
    check_consistent_traversal_rows(TRAVERSAl, ROWS, #TRAVERSAl, #ROWS)

static void
check_consistent_state(void) {
    xpthread_mutex_lock(&cecup.arena_mutex);

    error("Checking consistent state...\n");

    for (int32 row_id = 0; row_id < cecup.rows_len; row_id += 1) {
        int32 src_idx = cecup.rows[L][row_id];
        int32 dst_idx = cecup.rows[R][row_id];

        if ((src_idx == -1) && (dst_idx == -1)) {
            error("Consistency error: Row %d has no index for either side.\n", row_id);
            fatal(EXIT_FAILURE);
        }

        if (src_idx >= 0) {
            if (src_idx >= cecup.traversal[L].nfiles) {
                error("Consistency error: Row %d points to invalid src_idx %d.\n", row_id, src_idx);
                fatal(EXIT_FAILURE);
            }
            if (cecup.traversal[L].row_ids[src_idx] != row_id) {
                error("Consistency error:"
                      " rows_src["RED("%d")"] -> src_idx="GREEN("%d")","
                      " but src.row_ids["GREEN("%d")"] -> row_id="RED("%d")".\n",
                      row_id, src_idx,
                      src_idx, cecup.traversal[L].row_ids[src_idx]);
                fatal(EXIT_FAILURE);
            }
        }

        if (dst_idx >= 0) {
            if (dst_idx >= cecup.traversal[R].nfiles) {
                error("Consistency error: Row %d points to invalid dst_idx %d.\n", row_id, dst_idx);
                fatal(EXIT_FAILURE);
            }
            if (cecup.traversal[R].row_ids[dst_idx] != row_id) {
                error("Consistency error:"
                      " rows_dst["RED("%d")"] -> dst_idx="GREEN("%d")","
                      " but dst.row_ids["GREEN("%d")"] -> row_id="RED("%d")".\n",
                      row_id, dst_idx,
                      dst_idx, cecup.traversal[R].row_ids[dst_idx]);
                fatal(EXIT_FAILURE);
            }
        }
    }

    CHECK_CONSISTENT_TRAVERSAL_ROWS(&cecup.traversal[L], cecup.rows[L]);
    CHECK_CONSISTENT_TRAVERSAL_ROWS(&cecup.traversal[R], cecup.rows[R]);

    xpthread_mutex_unlock(&cecup.arena_mutex);

    error("State is consistent...\n");
    return;
}

#if (0 == TESTING_aux) && TESTING
static inline void
aux_functions_sink(void) {
    (void)cecup_get_dirs;
    (void)get_target_tasks;
    (void)task_list_free;
    (void)free_message;
    (void)aux_protect_interface_from_user;
    (void)aux_invalidate_preview;
    return;
}
#endif

#if TESTING_aux
#define CBASE_IMPLEMENT
#include "cbase.h"

#include "update.c"
#include "work.c"
#include "on.c"
#include "assert.c"

int main(void) {
    Message *msg;
    bool ok;
    GtkWidget *box;
    GtkWidget *paned;
    GtkWidget *info;

    pthread_mutex_init(&cecup.stop_lock, NULL);
    pthread_mutex_init(&cecup.arena_mutex, NULL);

    /* Test aux_is_root */
    ASSERT(aux_is_root("."));
    ASSERT(aux_is_root("./"));
    ASSERT(!aux_is_root(".."));
    ASSERT(!aux_is_root("path/to/something"));

    /* Test free_message - Separate paths */
    msg = malloc2(SIZEOF(*msg));
    memset64(msg, 0, SIZEOF(*msg));
    msg->text = malloc2(10);
    msg->text_len = 9;
    msg->src_path = malloc2(10);
    msg->src_path_len = 9;
    msg->dst_path = malloc2(10);
    msg->dst_path_len = 9;
    free_message(msg);

    /* Test free_message - Shared paths */
    msg = malloc2(SIZEOF(*msg));
    memset64(msg, 0, SIZEOF(*msg));
    msg->text = malloc2(5);
    msg->text_len = 4;
    msg->src_path = malloc2(5);
    msg->src_path_len = 4;
    msg->dst_path = msg->src_path;
    free_message(msg);

    /* Test free_message - NULL dst_path */
    msg = malloc2(SIZEOF(*msg));
    memset64(msg, 0, SIZEOF(*msg));
    msg->text = malloc2(5);
    msg->text_len = 4;
    msg->src_path = malloc2(5);
    msg->src_path_len = 4;
    msg->dst_path = NULL;
    free_message(msg);

    stop_working(true);
    ASSERT(cecup.stop_working == true);
    stop_working(false);
    ASSERT(cecup.stop_working == false);

    if (!gtk_init_check()) {
        exit(EXIT_FAILURE);
    }

    cecup.sync_button = gtk_button_new();
    cecup.stop_button = gtk_button_new();
    cecup.preview_button = gtk_button_new();
    cecup.ignore_button = gtk_button_new();
    cecup.invert_button = gtk_button_new();
    cecup.delete_after_button = gtk_check_button_new();
    cecup.delete_ignored_button = gtk_check_button_new();
    cecup.check_fs_button = gtk_check_button_new();
    cecup.diff_entry = gtk_entry_new();
    cecup.term_entry = gtk_entry_new();
    cecup.browse_button[L] = gtk_button_new();
    cecup.browse_button[R] = gtk_button_new();
    cecup.dir_entry[L] = gtk_entry_new();
    cecup.dir_entry[R] = gtk_entry_new();

    cecup.entry_id[L] = g_signal_connect(cecup.dir_entry[L], "changed", G_CALLBACK(gtk_widget_show), NULL);
    cecup.entry_id[R] = g_signal_connect(cecup.dir_entry[R], "changed", G_CALLBACK(gtk_widget_show), NULL);

    cecup.filter_new = gtk_check_button_new();
    cecup.filter_link = gtk_check_button_new();
    cecup.filter_update = gtk_check_button_new();
    cecup.filter_equal = gtk_check_button_new();
    cecup.filter_delete = gtk_check_button_new();
    cecup.filter_ignore = gtk_check_button_new();

    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    cecup.tree[L] = gtk_tree_view_new();
    gtk_paned_set_start_child(GTK_PANED(paned), cecup.tree[L]);
    gtk_box_append(GTK_BOX(box), paned);

    /* Test aux_protect_interface_from_user */
    aux_protect_interface_from_user(true);
    ASSERT(gtk_widget_get_sensitive(cecup.stop_button) == TRUE);
    ASSERT(gtk_widget_get_sensitive(cecup.sync_button) == FALSE);

    aux_protect_interface_from_user(false);
    ASSERT(gtk_widget_get_sensitive(cecup.stop_button) == FALSE);

    /* Test cecup_get_dirs, cecup_reset_dir, save_config, config_bool_set */
    SNPRINTF(cecup.config_path, "%s", "/tmp/cecup_test_config.ini");

    gtk_editable_set_text(GTK_EDITABLE(cecup.dir_entry[L]), "/tmp");
    gtk_editable_set_text(GTK_EDITABLE(cecup.dir_entry[R]), "/var");
    ok = cecup_get_dirs();
    ASSERT(ok);

    gtk_editable_set_text(GTK_EDITABLE(cecup.dir_entry[L]), "/tmp");
    gtk_editable_set_text(GTK_EDITABLE(cecup.dir_entry[R]), "/tmp");
    ok = cecup_get_dirs();
    ASSERT(!ok); /* Same directory failure */

    mkdir("/tmp/cecup_aux_parent", 0700);
    mkdir("/tmp/cecup_aux_parent/child", 0700);
    mkdir("/tmp/cecup_aux_parent2", 0700);

    gtk_editable_set_text(GTK_EDITABLE(cecup.dir_entry[L]),
                          "/tmp/cecup_aux_parent/child");
    gtk_editable_set_text(GTK_EDITABLE(cecup.dir_entry[R]),
                          "/tmp/cecup_aux_parent");
    ok = cecup_get_dirs();
    ASSERT(!ok); /* Source inside destination failure */

    gtk_editable_set_text(GTK_EDITABLE(cecup.dir_entry[L]),
                          "/tmp/cecup_aux_parent");
    gtk_editable_set_text(GTK_EDITABLE(cecup.dir_entry[R]),
                          "/tmp/cecup_aux_parent/child");
    ok = cecup_get_dirs();
    ASSERT(!ok); /* Destination inside source failure */

    gtk_editable_set_text(GTK_EDITABLE(cecup.dir_entry[L]),
                          "/tmp/cecup_aux_parent");
    gtk_editable_set_text(GTK_EDITABLE(cecup.dir_entry[R]),
                          "/tmp/cecup_aux_parent2");
    ok = cecup_get_dirs();
    ASSERT(ok); /* Shared string prefix but not contained */

    rmdir("/tmp/cecup_aux_parent/child");
    rmdir("/tmp/cecup_aux_parent");
    rmdir("/tmp/cecup_aux_parent2");
    remove("/tmp/cecup_test_config.ini");

    /* Test on_banner_response */
    info = gtk_info_bar_new();
    gtk_widget_set_visible(info, TRUE);
    on_banner_response(GTK_INFO_BAR(info), 0, NULL);
    ASSERT(gtk_widget_get_visible(info) == FALSE);

    /* Test check_consistent_state & check_consistent_traversal_rows */
    cecup.rows_len = 1;
    cecup.rows_capacity = 10;
    cecup.rows[L] = malloc2(10 * SIZEOF(int32));
    cecup.rows[R] = malloc2(10 * SIZEOF(int32));
    cecup.rows[L][0] = 0;
    cecup.rows[R][0] = 0;

    cecup.traversal[L].nfiles = 1;
    cecup.traversal[L].row_ids = malloc2(10 * SIZEOF(int32));
    cecup.traversal[L].row_ids[0] = 0;
    cecup.traversal[L].paths = malloc2(10 * SIZEOF(char*));
    cecup.traversal[L].paths[0] = "test1";
    cecup.traversal[L].paths_lens = malloc2(10 * SIZEOF(int16));
    cecup.traversal[L].paths_lens[0] = 5;
    cecup.traversal[L].map = hash_create_fs_map(16, "traversal[L].map");
    hash_insert_fs_map(cecup.traversal[L].map, "test1", 5, 0);

    cecup.traversal[R].nfiles = 1;
    cecup.traversal[R].row_ids = malloc2(10 * SIZEOF(int32));
    cecup.traversal[R].row_ids[0] = 0;
    cecup.traversal[R].paths = malloc2(10 * SIZEOF(char*));
    cecup.traversal[R].paths[0] = "test2";
    cecup.traversal[R].paths_lens = malloc2(10 * SIZEOF(int16));
    cecup.traversal[R].paths_lens[0] = 5;
    cecup.traversal[R].map = hash_create_fs_map(16, "traversal[R].map");
    hash_insert_fs_map(cecup.traversal[R].map, "test2", 5, 0);

    check_consistent_state(); /* Would fatal() if failed */

    log_internal(__FILE__, __LINE__, (char *)__func__, 0, "Test log msg %d", 42);
    g_main_context_iteration(NULL, FALSE);

    /* Cleanup testing memory to prevent leak sanitizers triggering */
    free2(cecup.rows[L], 10 * SIZEOF(int32));
    free2(cecup.rows[R], 10 * SIZEOF(int32));
    free2(cecup.traversal[L].row_ids, 10 * SIZEOF(int32));
    free2(cecup.traversal[L].paths, 10 * SIZEOF(char*));
    free2(cecup.traversal[L].paths_lens, 10 * SIZEOF(int16));
    free2(cecup.traversal[R].row_ids, 10 * SIZEOF(int32));
    free2(cecup.traversal[R].paths, 10 * SIZEOF(char*));
    free2(cecup.traversal[R].paths_lens, 10 * SIZEOF(int16));

    hash_destroy_fs_map(cecup.traversal[L].map);
    hash_destroy_fs_map(cecup.traversal[R].map);

    exit(EXIT_SUCCESS);
}
#endif

#endif /* AUX_C */
