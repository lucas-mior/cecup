// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#define CBASE_IMPLEMENT
#include "cbase.h"

#include "gtk_include.h"
#include "cecup.h"

#include "on.c"
#include "columns.c"

#include ".welcome.h"

#define SPACING_BOX 5
#define PADDING_BUTTON 5
#define PADDING_FILTER_BUTTON 2
#define PADDING_LABEL 0
#define PADDING_ZERO 0
#define FILL_FALSE false
#define EXPAND_FALSE false
#define FILL_TRUE true
#define EXPAND_TRUE true

#if DEBUGGING

#pragma clang diagnostic ignored "-Wvariadic-macro-arguments-omitted"

#define NEW_WITH_NAME(VARIABLE, FUNCTION, ...) do {      \
    VARIABLE = FUNCTION(__VA_ARGS__);                    \
    gtk_widget_set_name(VARIABLE, #VARIABLE);            \
} while (0)

#else

#define NEW_WITH_NAME(VARIABLE, FUNCTION, ...) do {      \
    VARIABLE = FUNCTION(__VA_ARGS__);                    \
} while (0)

#endif

static bool is_first_run = false;

static void
free_text_info(void *data) {
    free2(data, SIZEOF(TextInfo));
    return;
}

static bool
main_key_file_error_is_missing(GError *key_error) {
    return g_error_matches(key_error, G_KEY_FILE_ERROR,
                           G_KEY_FILE_ERROR_KEY_NOT_FOUND)
           || g_error_matches(key_error, G_KEY_FILE_ERROR,
                              G_KEY_FILE_ERROR_GROUP_NOT_FOUND);
}

static char *
main_key_file_get_string(GKeyFile *key, char *group, char *name) {
    GError *key_error = NULL;
    char *value;

    value = g_key_file_get_string(key, group, name, &key_error);
    if (key_error) {
        if (!main_key_file_error_is_missing(key_error)) {
            error("Error reading configuration key [%s] %s: %s.\n",
                  group, name, key_error->message);
        }
        g_clear_error(&key_error);
    }
    return value;
}

static bool
main_key_file_get_boolean(
    GKeyFile *key,
    char *group,
    char *name,
    bool *value
) {
    GError *key_error = NULL;
    gboolean result;

    result = g_key_file_get_boolean(key, group, name, &key_error);
    if (key_error) {
        if (!main_key_file_error_is_missing(key_error)) {
            error("Error reading configuration key [%s] %s: %s.\n",
                  group, name, key_error->message);
        }
        g_clear_error(&key_error);
        return false;
    }

    *value = result;
    return true;
}

#if !defined(CECUP_SYSTEM_CONFIG_DIR)
#define CECUP_SYSTEM_CONFIG_DIR ""
#endif

#if !defined(LOCALEDIR)
#define LOCALEDIR ""
#endif

static bool
main_dir_exists(char *path) {
    if (path == NULL) {
        return false;
    }
    if (path[0] == '\0') {
        return false;
    }
    return g_file_test(path, G_FILE_TEST_IS_DIR);
}

static void
main_store_path(char *destination, char *path, char *description) {
    int32 path_len;

    path_len = strlen32(path);
    if (path_len >= MAX_PATH_LENGTH) {
        error("Error: %s path is too long: %s.\n", description, path);
        fatal(EXIT_FAILURE);
    }

    memcpy64(destination, path, path_len + 1);
    return;
}

static char *
main_strdup_existing_dir(char *path) {
    if (main_dir_exists(path)) {
        return g_strdup(path);
    }
    return NULL;
}

static char *
main_default_config_dir(void) {
    char *env_dir;
    char *candidate;
    char **system_dirs;

    env_dir = getenv("CECUP_DEFAULT_CONFIG_DIR");
    candidate = main_strdup_existing_dir(env_dir);
    if (candidate != NULL) {
        return candidate;
    }

    candidate = main_strdup_existing_dir(CECUP_SYSTEM_CONFIG_DIR);
    if (candidate != NULL) {
        return candidate;
    }

    system_dirs = (char **)g_get_system_config_dirs();
    for (int32 i = 0; system_dirs[i] != NULL; i += 1) {
        candidate = g_build_filename(system_dirs[i], "cecup", NULL);
        if (main_dir_exists(candidate)) {
            return candidate;
        }
        g_free(candidate);
    }

    candidate = main_strdup_existing_dir("etc");
    if (candidate != NULL) {
        return candidate;
    }

    return NULL;
}

static void
main_copy_default_config_file(
    char *config_base,
    char *default_config_dir,
    char *filename
) {
    GError *file_error = NULL;
    char *src_path;
    char *dst_path;
    char *contents;
    gsize contents_len;

    dst_path = g_build_filename(config_base, filename, NULL);
    if (g_file_test(dst_path, G_FILE_TEST_EXISTS)) {
        g_free(dst_path);
        return;
    }
    if (default_config_dir == NULL) {
        g_free(dst_path);
        return;
    }

    src_path = g_build_filename(default_config_dir, filename, NULL);
    contents = NULL;
    contents_len = 0;

    if (!g_file_get_contents(src_path, &contents, &contents_len, &file_error)) {
        if ((file_error != NULL)
            && !g_error_matches(file_error, G_FILE_ERROR,
                                G_FILE_ERROR_NOENT)) {
            error("Error reading default configuration file %s: %s.\n",
                  src_path, file_error->message);
        }
        g_clear_error(&file_error);
        g_free(src_path);
        g_free(dst_path);
        return;
    }

    if (!g_file_set_contents(dst_path, contents, (gssize)contents_len,
                             &file_error)) {
        error("Error writing user configuration file %s: %s.\n",
              dst_path, file_error->message);
        g_clear_error(&file_error);
    }

    g_free(contents);
    g_free(src_path);
    g_free(dst_path);
    return;
}

static void
main_seed_config_dir(char *config_base) {
    char *default_config_dir;

    default_config_dir = main_default_config_dir();
    if (default_config_dir == NULL) {
        error("Could not find default configuration directory."
              " Starting with built-in defaults.\n");
    }

    main_copy_default_config_file(config_base, default_config_dir,
                                  "ignore.conf");
    main_copy_default_config_file(config_base, default_config_dir,
                                  "cecup.conf");

    g_free(default_config_dir);
    return;
}

static void
main_setup_config_paths(void) {
    char *config_base;
    char *ignore_path;
    char *config_path;
    bool config_dir_missing;

    config_base = g_build_filename(g_get_user_config_dir(), "cecup", NULL);
    config_dir_missing = !g_file_test(config_base, G_FILE_TEST_IS_DIR);

    if (g_mkdir_with_parents(config_base, 0755) < 0) {
        error("Error creating configuration directory %s: %s.\n",
              config_base, strerror(errno));
        g_free(config_base);
        fatal(EXIT_FAILURE);
    }

    if (config_dir_missing) {
        is_first_run = true;
    }
    main_seed_config_dir(config_base);

    ignore_path = g_build_filename(config_base, "ignore.conf", NULL);
    config_path = g_build_filename(config_base, "cecup.conf", NULL);

    main_store_path(cecup.ignore_path, ignore_path, "ignore configuration");
    main_store_path(cecup.config_path, config_path, "configuration");

    g_free(ignore_path);
    g_free(config_path);
    g_free(config_base);
    return;
}

static void
main_setup_locale(void) {
    char *locale_env;
    char *locale_dir;

    if (setlocale(LC_ALL, "") == NULL) {
        error("Error setting locale.\n");
    }

    locale_dir = NULL;
    locale_env = getenv("CECUP_LOCALEDIR");
    if (main_dir_exists(locale_env)) {
        locale_dir = locale_env;
    } else if (main_dir_exists("./po")) {
        locale_dir = "./po";
    } else if (main_dir_exists(LOCALEDIR)) {
        locale_dir = LOCALEDIR;
    }

    if (locale_dir != NULL) {
        bindtextdomain(QUOTE(GETTEXT_PACKAGE), locale_dir);
    } else {
        error("Could not find locale directory. Using untranslated strings.\n");
    }

    bind_textdomain_codeset(QUOTE(GETTEXT_PACKAGE), "UTF-8");
    textdomain(QUOTE(GETTEXT_PACKAGE));
    return;
}

static void
main_setup_tree_columns(GtkWidget *tree) {
    GActionMap *action_map = G_ACTION_MAP(cecup.application);
    int8 side = (int8)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(tree), "side"));

    if (g_action_map_lookup_action(action_map, "tree_dispatch") == NULL) {
        GSimpleAction *dispatch = g_simple_action_new("tree_dispatch", G_VARIANT_TYPE_INT32);
        GSimpleAction *ignore = g_simple_action_new("ignore", G_VARIANT_TYPE_STRING);

        g_action_map_add_action(action_map, G_ACTION(dispatch));
        g_action_map_add_action(action_map, G_ACTION(ignore));

        g_signal_connect(dispatch, "activate", G_CALLBACK(on_menu_dispatch),      NULL);
        g_signal_connect(ignore,   "activate", G_CALLBACK(on_menu_ignore_action), NULL);

    }

    {
        GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
        GtkColumnViewColumn *column = gtk_column_view_column_new(NULL, factory);

        g_signal_connect(factory, "setup", G_CALLBACK(column_setup_checkbox), GINT_TO_POINTER(side));
        g_signal_connect(factory, "bind", G_CALLBACK(column_bind_checkbox), GINT_TO_POINTER(side));
        gtk_column_view_append_column(GTK_COLUMN_VIEW(tree), column);
        g_object_unref(column);
    }

    {
        GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
        GtkColumnViewColumn *column = gtk_column_view_column_new(_("Task"), factory);
        GtkSorter *sorter = GTK_SORTER(gtk_string_sorter_new(NULL));

        g_signal_connect(factory, "setup", G_CALLBACK(column_setup_action), NULL);
        g_signal_connect(factory, "bind", G_CALLBACK(column_bind_action), GINT_TO_POINTER(side));

        gtk_column_view_column_set_resizable(column, TRUE);
        gtk_column_view_column_set_sorter(column, sorter);

        if (side == L) {
            g_object_set_data(G_OBJECT(column), "col_id", GINT_TO_POINTER(COL_SRC_ACTION));
        } else {
            g_object_set_data(G_OBJECT(column), "col_id", GINT_TO_POINTER(COL_DST_ACTION));
        }
        gtk_column_view_append_column(GTK_COLUMN_VIEW(tree), column);
        g_object_unref(column);
    }

    {
        GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
        GtkSorter *sorter = GTK_SORTER(gtk_string_sorter_new(NULL));
        GtkColumnViewColumn *column = gtk_column_view_column_new(_("Name"), factory);

        g_signal_connect(factory, "setup", G_CALLBACK(column_setup_path), tree);
        g_signal_connect(factory, "bind", G_CALLBACK(column_bind_path), tree);

        gtk_column_view_column_set_resizable(column, TRUE);
        gtk_column_view_column_set_fixed_width(column, 500);
        gtk_column_view_column_set_sorter(column, sorter);

        if (side == L) {
            g_object_set_data(G_OBJECT(column), "col_id", GINT_TO_POINTER(COL_SRC_PATH));
        } else {
            g_object_set_data(G_OBJECT(column), "col_id", GINT_TO_POINTER(COL_DST_PATH));
        }
        gtk_column_view_append_column(GTK_COLUMN_VIEW(tree), column);
        g_object_unref(column);
    }

    {
        GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
        GtkSorter *sorter = GTK_SORTER(gtk_string_sorter_new(NULL));
        TextInfo *text_info = malloc2(SIZEOF(*text_info));
        GtkColumnViewColumn *column = gtk_column_view_column_new(_("Size"), factory);

        text_info->side = side;
        text_info->type = COLUMN_SIZE;
        g_object_set_data_full(G_OBJECT(factory), "text_info", text_info, free_text_info);

        g_signal_connect(factory, "setup", G_CALLBACK(column_text_setup), NULL);
        g_signal_connect(factory, "bind", G_CALLBACK(column_text_bind), text_info);

        gtk_column_view_column_set_resizable(column, TRUE);
        gtk_column_view_column_set_sorter(column, sorter);

        if (side == L) {
            g_object_set_data(G_OBJECT(column), "col_id", GINT_TO_POINTER(COL_SRC_SIZE));
        } else {
            g_object_set_data(G_OBJECT(column), "col_id", GINT_TO_POINTER(COL_DST_SIZE));
        }
        gtk_column_view_append_column(GTK_COLUMN_VIEW(tree), column);
        g_object_unref(column);
    }

    {
        GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
        GtkSorter *sorter = GTK_SORTER(gtk_string_sorter_new(NULL));
        TextInfo *text_info = malloc2(SIZEOF(*text_info));
        GtkColumnViewColumn *column = gtk_column_view_column_new(_("Modification Time"), factory);

        text_info->side = side;
        text_info->type = COLUMN_MTIME;
        g_object_set_data_full(G_OBJECT(factory), "text_info", text_info, free_text_info);

        g_signal_connect(factory, "setup", G_CALLBACK(column_text_setup), NULL);
        g_signal_connect(factory, "bind", G_CALLBACK(column_text_bind), text_info);

        gtk_column_view_column_set_expand(column, TRUE);
        gtk_column_view_column_set_resizable(column, TRUE);
        gtk_column_view_column_set_sorter(column, sorter);

        if (side == L) {
            g_object_set_data(G_OBJECT(column), "col_id", GINT_TO_POINTER(COL_SRC_MTIME));
        } else {
            g_object_set_data(G_OBJECT(column), "col_id", GINT_TO_POINTER(COL_DST_MTIME));
        }
        gtk_column_view_append_column(GTK_COLUMN_VIEW(tree), column);
        g_object_unref(column);
    }

    gtk_widget_set_has_tooltip(tree, TRUE);
    gtk_widget_set_focusable(tree, TRUE);

    g_signal_connect(tree, "query-tooltip", G_CALLBACK(on_tree_tooltip), NULL);

    {
        GtkSorter *sorter = gtk_column_view_get_sorter(GTK_COLUMN_VIEW(tree));
        g_signal_connect(sorter, "changed", G_CALLBACK(on_sort_changed), tree);
    }

    {
        GtkGesture *click = gtk_gesture_click_new();

        gtk_widget_add_controller(tree, GTK_EVENT_CONTROLLER(click));
        gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(click), GTK_PHASE_CAPTURE);
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 0);
        g_signal_connect(click, "pressed", G_CALLBACK(on_tree_button_press), NULL);
    }

    {
        GtkEventController *key = gtk_event_controller_key_new();

        gtk_widget_add_controller(tree, GTK_EVENT_CONTROLLER(key));
        gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(key), GTK_PHASE_BUBBLE);
        g_signal_connect(key, "key-pressed", G_CALLBACK(on_tree_key_press), NULL);
    }

    return;
}

static void
main_application_run(GtkApplication *application, gpointer user_data) {
    GtkWidget *main_vbox;
    GtkWidget *header_vbox;
    GtkWidget *button_hbox;
    GtkWidget *filter_hbox;
    GtkWidget *search_hbox;
    GtkWidget *options_hbox;
    GtkWidget *reset_button;
    GtkWidget *v_paned;
    GtkWidget *top_vbox;
    GtkWidget *footer_vbox;
    GtkWidget *paned_trees;

    GtkWidget *vbox[2];
    GtkWidget *entry_hbox[2];
    GtkWidget *scroll[2];
    GtkWidget *progress_vbox;
    GtkWidget *paths_hbox;

    char src_path_buffer[MAX_PATH_LENGTH];
    char dst_path_buffer[MAX_PATH_LENGTH];

    (void)user_data;

    {
        static char *base_css[] = {
            "columnview row { min-height: 0px; }",
            "columnview cell { padding: 0px; }",
            "paned > separator { min-width: 10px; min-height: 10px; }",
            "scrollbar.vertical slider { min-width: 12px; }",
            "scrollbar.horizontal slider { min-height: 8px; }",
            "progressbar text { font-size: 11pt; font-weight: bold; }"
        };
        GtkCssProvider *css_provider;
        char css_buffer[BUFSIZ];
        int32 offset;

        css_provider = gtk_css_provider_new();
        offset = 0;

        for (int32 i = 0; i < LENGTH(base_css); i += 1) {
            int32 n;
            n = snprintf2(css_buffer + offset, SIZEOF(css_buffer) - offset,
                          "%s\n", base_css[i]);
            offset += n;
        }

        for (int32 i = 0; i < LENGTH(colors); i += 1) {
            int32 m;

            if (colors[i] == NULL) {
                continue;
            }

            m = snprintf2(css_buffer + offset, SIZEOF(css_buffer) - offset,
                          "row:not(:selected)"
                          " .cell-color-%d { background-color: %s; }\n",
                          i, colors[i]);
            offset += m;
        }

        gtk_css_provider_load_from_string(css_provider, css_buffer);
        gtk_style_context_add_provider_for_display(gdk_display_get_default(),
                                                   GTK_STYLE_PROVIDER(css_provider),
                                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(css_provider);
    }

    NEW_WITH_NAME(cecup.gtk_window, gtk_application_window_new, application);
    gtk_window_set_title(GTK_WINDOW(cecup.gtk_window), "cecup");
    gtk_window_set_default_size(GTK_WINDOW(cecup.gtk_window), 1100, 800);

    g_signal_connect(cecup.gtk_window, "destroy", G_CALLBACK(on_window_destroy), NULL);

    NEW_WITH_NAME(main_vbox, gtk_box_new, GTK_ORIENTATION_VERTICAL, SPACING_BOX);
    gtk_window_set_child(GTK_WINDOW(cecup.gtk_window), main_vbox);

    NEW_WITH_NAME(header_vbox, gtk_box_new, GTK_ORIENTATION_VERTICAL, SPACING_BOX);
    gtk_widget_set_margin_start(header_vbox, SPACING_BOX);
    gtk_widget_set_margin_end(header_vbox, SPACING_BOX);
    gtk_widget_set_margin_top(header_vbox, SPACING_BOX);
    gtk_widget_set_margin_bottom(header_vbox, SPACING_BOX);
    gtk_box_append(GTK_BOX(main_vbox), header_vbox);

    NEW_WITH_NAME(options_hbox, gtk_box_new, GTK_ORIENTATION_HORIZONTAL, SPACING_BOX);
    gtk_widget_set_hexpand(options_hbox, TRUE);

    NEW_WITH_NAME(cecup.ignore_button, gtk_button_new);
    NEW_WITH_NAME(cecup.check_fs_button, gtk_check_button_new);
    NEW_WITH_NAME(cecup.delete_ignored_button, gtk_check_button_new);
    NEW_WITH_NAME(cecup.delete_after_button, gtk_check_button_new);

    gtk_button_set_label(GTK_BUTTON(cecup.ignore_button),  _("Edit Ignore Rules"));
    gtk_check_button_set_label(GTK_CHECK_BUTTON(cecup.check_fs_button),
                               _("Protect same-drive sync"));
    gtk_check_button_set_label(GTK_CHECK_BUTTON(cecup.delete_ignored_button),
                               _("Remove ignored items"));
    gtk_check_button_set_label(GTK_CHECK_BUTTON(cecup.delete_after_button),
                               _("Sync 100%"));

    gtk_widget_set_tooltip_text(cecup.ignore_button,
                                _("Edit the list of filename patterns to ignore"));
    gtk_widget_set_tooltip_text(cecup.check_fs_button,
                                _("Prevent copying if original and backup are on the same disk"));
    gtk_widget_set_tooltip_text(cecup.delete_ignored_button,
                                _("Remove files from backup if they were added to the ignore list"));
    gtk_widget_set_tooltip_text(cecup.delete_after_button,
                                _("Delete files in backup that do not exist in the original"));

    NEW_WITH_NAME(cecup.diff_entry, gtk_entry_new);
    NEW_WITH_NAME(cecup.term_entry, gtk_entry_new);

    gtk_widget_set_tooltip_text(cecup.diff_entry,
                                _("Executable used for comparing files"));
    gtk_widget_set_tooltip_text(cecup.term_entry,
                                _("Terminal emulator used to launch the diff tool"));

    NEW_WITH_NAME(reset_button, gtk_button_new);
    gtk_button_set_label(GTK_BUTTON(reset_button), _("Defaults"));
    gtk_widget_set_tooltip_text(reset_button, _("Restore original settings"));

    gtk_box_append(GTK_BOX(options_hbox), cecup.ignore_button);
    gtk_widget_set_margin_start(cecup.ignore_button, PADDING_BUTTON);
    gtk_widget_set_margin_end(cecup.ignore_button, PADDING_BUTTON);

    gtk_box_append(GTK_BOX(options_hbox), cecup.check_fs_button);
    gtk_widget_set_margin_start(cecup.check_fs_button, PADDING_BUTTON);
    gtk_widget_set_margin_end(cecup.check_fs_button, PADDING_BUTTON);

    gtk_box_append(GTK_BOX(options_hbox), cecup.delete_ignored_button);
    gtk_widget_set_margin_start(cecup.delete_ignored_button, PADDING_BUTTON);
    gtk_widget_set_margin_end(cecup.delete_ignored_button, PADDING_BUTTON);

    gtk_box_append(GTK_BOX(options_hbox), cecup.delete_after_button);
    gtk_widget_set_margin_start(cecup.delete_after_button, PADDING_BUTTON);
    gtk_widget_set_margin_end(cecup.delete_after_button, PADDING_BUTTON);

    gtk_box_append(GTK_BOX(options_hbox), gtk_label_new(_("Diff Tool:")));
    gtk_box_append(GTK_BOX(options_hbox), cecup.diff_entry);

    gtk_editable_set_text(GTK_EDITABLE(cecup.diff_entry), "diff");
    gtk_box_append(GTK_BOX(options_hbox), gtk_label_new(_("Terminal:")));
    gtk_box_append(GTK_BOX(options_hbox), cecup.term_entry);
    gtk_editable_set_text(GTK_EDITABLE(cecup.term_entry), "xterm");

    gtk_box_append(GTK_BOX(options_hbox), reset_button);
    gtk_box_append(GTK_BOX(header_vbox), options_hbox);

    NEW_WITH_NAME(progress_vbox, gtk_box_new, GTK_ORIENTATION_VERTICAL, 2);
    NEW_WITH_NAME(cecup.progress_bar, gtk_progress_bar_new);

    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(cecup.progress_bar), TRUE);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(cecup.progress_bar), _("Analyzing changes"));

    gtk_box_append(GTK_BOX(progress_vbox), cecup.progress_bar);
    gtk_box_append(GTK_BOX(header_vbox), progress_vbox);
    gtk_widget_set_margin_bottom(progress_vbox, 5);

    NEW_WITH_NAME(search_hbox, gtk_box_new, GTK_ORIENTATION_HORIZONTAL, SPACING_BOX);
    NEW_WITH_NAME(cecup.search_entry, gtk_entry_new);

    gtk_widget_set_margin_start(search_hbox, 10);
    gtk_widget_set_margin_end(search_hbox, 10);
    gtk_entry_set_placeholder_text(GTK_ENTRY(cecup.search_entry), _("Search files..."));
    gtk_entry_set_icon_from_icon_name(GTK_ENTRY(cecup.search_entry),
                                      GTK_ENTRY_ICON_PRIMARY, "system-search-symbolic");
    {
        GtkWidget *search_label;

        search_label = gtk_label_new(_("🔍"));
        gtk_box_append(GTK_BOX(search_hbox), search_label);
        gtk_widget_set_margin_start(search_label, 5);
        gtk_widget_set_margin_end(search_label, 5);
    }
    gtk_widget_set_hexpand(cecup.search_entry, TRUE);
    gtk_box_append(GTK_BOX(search_hbox), cecup.search_entry);

    NEW_WITH_NAME(cecup.stats_label, gtk_label_new, "");

    gtk_label_set_label(GTK_LABEL(cecup.stats_label), _("✅ Everything ready"));
    gtk_box_append(GTK_BOX(search_hbox), cecup.stats_label);
    gtk_widget_set_margin_start(cecup.stats_label, 10);
    gtk_widget_set_margin_end(cecup.stats_label, 10);

    NEW_WITH_NAME(cecup.select_visible_button, gtk_button_new);
    NEW_WITH_NAME(cecup.unselect_button, gtk_button_new);

    gtk_button_set_label(GTK_BUTTON(cecup.select_visible_button), _("Select all visible"));
    gtk_button_set_label(GTK_BUTTON(cecup.unselect_button),       _("Unselect all"));

    gtk_widget_set_tooltip_text(cecup.select_visible_button,
                                _("Select all files which currently are on the list below"));
    gtk_widget_set_tooltip_text(cecup.unselect_button,
                                _("Unselect all files which are currently selected"));

    gtk_box_append(GTK_BOX(search_hbox), cecup.select_visible_button);
    gtk_box_append(GTK_BOX(search_hbox), cecup.unselect_button);

    gtk_box_append(GTK_BOX(main_vbox), search_hbox);

    NEW_WITH_NAME(filter_hbox, gtk_box_new, GTK_ORIENTATION_HORIZONTAL, SPACING_BOX);
    gtk_widget_set_halign(filter_hbox, GTK_ALIGN_CENTER);

    NEW_WITH_NAME(cecup.filter_new, gtk_toggle_button_new);
    NEW_WITH_NAME(cecup.filter_link, gtk_toggle_button_new);
    NEW_WITH_NAME(cecup.filter_update, gtk_toggle_button_new);
    NEW_WITH_NAME(cecup.filter_equal, gtk_toggle_button_new);
    NEW_WITH_NAME(cecup.filter_delete, gtk_toggle_button_new);
    NEW_WITH_NAME(cecup.filter_ignore, gtk_toggle_button_new);

    gtk_button_set_label(GTK_BUTTON(cecup.filter_new),    EMOJI_NEW);
    gtk_button_set_label(GTK_BUTTON(cecup.filter_link),   EMOJI_LINK);
    gtk_button_set_label(GTK_BUTTON(cecup.filter_update), EMOJI_UPDATE);
    gtk_button_set_label(GTK_BUTTON(cecup.filter_equal),  EMOJI_EQUAL);
    gtk_button_set_label(GTK_BUTTON(cecup.filter_delete), EMOJI_DELETE);
    gtk_button_set_label(GTK_BUTTON(cecup.filter_ignore), EMOJI_IGNORE);

    gtk_widget_set_tooltip_text(cecup.filter_new,    _("Show new files"));
    gtk_widget_set_tooltip_text(cecup.filter_link,   _("Show links"));
    gtk_widget_set_tooltip_text(cecup.filter_update, _("Show updates"));
    gtk_widget_set_tooltip_text(cecup.filter_equal,  _("Show equals"));
    gtk_widget_set_tooltip_text(cecup.filter_delete, _("Show files to be deleted"));
    gtk_widget_set_tooltip_text(cecup.filter_ignore, _("Show ignored files"));

    gtk_box_append(GTK_BOX(filter_hbox), cecup.filter_new);
    gtk_widget_set_margin_start(cecup.filter_new, PADDING_FILTER_BUTTON);
    gtk_widget_set_margin_end(cecup.filter_new, PADDING_FILTER_BUTTON);

    gtk_box_append(GTK_BOX(filter_hbox), cecup.filter_link);
    gtk_widget_set_margin_start(cecup.filter_link, PADDING_FILTER_BUTTON);
    gtk_widget_set_margin_end(cecup.filter_link, PADDING_FILTER_BUTTON);

    gtk_box_append(GTK_BOX(filter_hbox), cecup.filter_update);
    gtk_widget_set_margin_start(cecup.filter_update, PADDING_FILTER_BUTTON);
    gtk_widget_set_margin_end(cecup.filter_update, PADDING_FILTER_BUTTON);

    gtk_box_append(GTK_BOX(filter_hbox), cecup.filter_equal);
    gtk_widget_set_margin_start(cecup.filter_equal, PADDING_FILTER_BUTTON);
    gtk_widget_set_margin_end(cecup.filter_equal, PADDING_FILTER_BUTTON);

    gtk_box_append(GTK_BOX(filter_hbox), cecup.filter_delete);
    gtk_widget_set_margin_start(cecup.filter_delete, PADDING_FILTER_BUTTON);
    gtk_widget_set_margin_end(cecup.filter_delete, PADDING_FILTER_BUTTON);

    gtk_box_append(GTK_BOX(filter_hbox), cecup.filter_ignore);
    gtk_widget_set_margin_start(cecup.filter_ignore, PADDING_FILTER_BUTTON);
    gtk_widget_set_margin_end(cecup.filter_ignore, PADDING_FILTER_BUTTON);

    gtk_box_append(GTK_BOX(main_vbox), filter_hbox);

    NEW_WITH_NAME(v_paned, gtk_paned_new, GTK_ORIENTATION_VERTICAL);
    gtk_widget_set_vexpand(v_paned, TRUE);
    gtk_box_append(GTK_BOX(main_vbox), v_paned);

    NEW_WITH_NAME(top_vbox, gtk_box_new, GTK_ORIENTATION_VERTICAL, 0);
    gtk_paned_set_start_child(GTK_PANED(v_paned), top_vbox);

    NEW_WITH_NAME(paned_trees, gtk_paned_new, GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_vexpand(paned_trees, TRUE);
    gtk_box_append(GTK_BOX(top_vbox), paned_trees);

    NEW_WITH_NAME(vbox[L], gtk_box_new, GTK_ORIENTATION_VERTICAL, 5);
    NEW_WITH_NAME(scroll[L], gtk_scrolled_window_new);
    gtk_scrolled_window_set_overlay_scrolling(GTK_SCROLLED_WINDOW(scroll[L]), FALSE);

    {
        GtkSelectionModel *selection_model;
        GListModel *list_model = g_object_ref(cecup.store);
        selection_model = GTK_SELECTION_MODEL(gtk_single_selection_new(list_model));
        NEW_WITH_NAME(cecup.tree[L], gtk_column_view_new, selection_model);
    }

    g_object_set_data(G_OBJECT(cecup.tree[L]), "side", GINT_TO_POINTER(L));
    main_setup_tree_columns(cecup.tree[L]);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll[L]), cecup.tree[L]);
    gtk_box_append(GTK_BOX(vbox[L]), scroll[L]);
    gtk_widget_set_vexpand(scroll[L], TRUE);
    gtk_paned_set_start_child(GTK_PANED(paned_trees), vbox[L]);

    NEW_WITH_NAME(vbox[R], gtk_box_new, GTK_ORIENTATION_VERTICAL, 5);
    NEW_WITH_NAME(scroll[R], gtk_scrolled_window_new);
    gtk_scrolled_window_set_overlay_scrolling(GTK_SCROLLED_WINDOW(scroll[R]), FALSE);

    {
        GtkSelectionModel *selection_model;
        GListModel *list_model = g_object_ref(cecup.store);
        selection_model = GTK_SELECTION_MODEL(gtk_single_selection_new(list_model));
        NEW_WITH_NAME(cecup.tree[R], gtk_column_view_new, selection_model);
    }

    g_object_set_data(G_OBJECT(cecup.tree[R]), "side", GINT_TO_POINTER(R));
    main_setup_tree_columns(cecup.tree[R]);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll[R]), cecup.tree[R]);
    gtk_box_append(GTK_BOX(vbox[R]), scroll[R]);
    gtk_widget_set_vexpand(scroll[R], TRUE);
    gtk_paned_set_end_child(GTK_PANED(paned_trees), vbox[R]);

    {
        char cwd[MAX_PATH_LENGTH];

        if (getcwd(cwd, sizeof(cwd)) == NULL) {
            error("Error getting current working directory: %s.\n", strerror(errno));
            fatal(EXIT_FAILURE);
        }

        if (strlen32(cwd) > (SIZEOF(src_path_buffer) / 2)) {
            error("Error: current working directory path is too long.\n");
            fatal(EXIT_FAILURE);
        }

        SNPRINTF(src_path_buffer, "%s/a/", cwd);
        SNPRINTF(dst_path_buffer, "%s/b/", cwd);
    }

    NEW_WITH_NAME(paths_hbox, gtk_box_new, GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_start(paths_hbox, 10);
    gtk_widget_set_margin_end(paths_hbox, 10);
    gtk_widget_set_margin_top(paths_hbox, 10);
    gtk_widget_set_margin_bottom(paths_hbox, 10);

    NEW_WITH_NAME(entry_hbox[L], gtk_box_new, GTK_ORIENTATION_HORIZONTAL, 5);
    NEW_WITH_NAME(cecup.dir_entry[L], gtk_entry_new);
    NEW_WITH_NAME(cecup.browse_button[L], gtk_button_new);

    gtk_editable_set_text(GTK_EDITABLE(cecup.dir_entry[L]), src_path_buffer);

    gtk_button_set_label(GTK_BUTTON(cecup.browse_button[L]), _("Select Folder"));
    gtk_widget_set_tooltip_text(cecup.dir_entry[L], _("Folder containing original files"));
    gtk_widget_set_tooltip_text(cecup.browse_button[L], _("Open browser to select"
                                                          " the folder containing original files"));

    gtk_widget_set_hexpand(cecup.dir_entry[L], TRUE);
    gtk_box_append(GTK_BOX(entry_hbox[L]), cecup.dir_entry[L]);
    gtk_box_append(GTK_BOX(entry_hbox[L]), cecup.browse_button[L]);
    gtk_widget_set_hexpand(entry_hbox[L], TRUE);
    gtk_box_append(GTK_BOX(paths_hbox), entry_hbox[L]);

    NEW_WITH_NAME(cecup.invert_button, gtk_button_new);
    gtk_button_set_label(GTK_BUTTON(cecup.invert_button), "<--->");
    gtk_widget_set_tooltip_text(cecup.invert_button, _("Invert Original and Backup"));
    gtk_box_append(GTK_BOX(paths_hbox), cecup.invert_button);

    NEW_WITH_NAME(entry_hbox[R], gtk_box_new, GTK_ORIENTATION_HORIZONTAL, 5);
    NEW_WITH_NAME(cecup.dir_entry[R], gtk_entry_new);
    NEW_WITH_NAME(cecup.browse_button[R], gtk_button_new);

    gtk_editable_set_text(GTK_EDITABLE(cecup.dir_entry[R]), dst_path_buffer);

    gtk_button_set_label(GTK_BUTTON(cecup.browse_button[R]), _("Select Folder"));
    gtk_widget_set_tooltip_text(cecup.dir_entry[R], _("Folder where the backup will be stored"));
    gtk_widget_set_tooltip_text(cecup.browse_button[R], _("Open browser to select"
                                                          " the folder where the backup will be stored"));

    gtk_widget_set_hexpand(cecup.dir_entry[R], TRUE);
    gtk_box_append(GTK_BOX(entry_hbox[R]), cecup.dir_entry[R]);
    gtk_box_append(GTK_BOX(entry_hbox[R]), cecup.browse_button[R]);
    gtk_widget_set_hexpand(entry_hbox[R], TRUE);
    gtk_box_append(GTK_BOX(paths_hbox), entry_hbox[R]);

    gtk_box_append(GTK_BOX(top_vbox), paths_hbox);

    NEW_WITH_NAME(button_hbox, gtk_box_new, GTK_ORIENTATION_HORIZONTAL, SPACING_BOX);
    gtk_widget_set_halign(button_hbox, GTK_ALIGN_CENTER);

    NEW_WITH_NAME(cecup.preview_button, gtk_button_new);
    NEW_WITH_NAME(cecup.stop_button, gtk_button_new);
    NEW_WITH_NAME(cecup.sync_button, gtk_button_new);

    gtk_button_set_label(GTK_BUTTON(cecup.preview_button), _("🔎 Analyze"));
    gtk_button_set_label(GTK_BUTTON(cecup.stop_button),    _("⏹️ Stop"));
    gtk_button_set_label(GTK_BUTTON(cecup.sync_button),    _("⏩ Apply Changes"));

    gtk_widget_set_tooltip_text(cecup.preview_button,
                                _("Check which files need to be copied or updated"));
    gtk_widget_set_tooltip_text(cecup.stop_button,
                                _("Cancel the current task"));
    gtk_widget_set_tooltip_text(cecup.sync_button,
                                _("Start copying and updating all files"));

    cecup.preview_dirty = true;
    gtk_widget_set_sensitive(cecup.stop_button, FALSE);

    gtk_box_append(GTK_BOX(button_hbox), cecup.preview_button);
    gtk_widget_set_margin_start(cecup.preview_button, PADDING_BUTTON);
    gtk_widget_set_margin_end(cecup.preview_button, PADDING_BUTTON);

    gtk_box_append(GTK_BOX(button_hbox), cecup.stop_button);
    gtk_widget_set_margin_start(cecup.stop_button, PADDING_BUTTON);
    gtk_widget_set_margin_end(cecup.stop_button, PADDING_BUTTON);

    gtk_box_append(GTK_BOX(button_hbox), cecup.sync_button);
    gtk_widget_set_margin_start(cecup.sync_button, PADDING_BUTTON);
    gtk_widget_set_margin_end(cecup.sync_button, PADDING_BUTTON);

    gtk_box_append(GTK_BOX(top_vbox), button_hbox);

    NEW_WITH_NAME(footer_vbox, gtk_box_new, GTK_ORIENTATION_VERTICAL, SPACING_BOX);
    gtk_widget_set_margin_top(footer_vbox, SPACING_BOX);
    gtk_widget_set_margin_bottom(footer_vbox, SPACING_BOX);
    gtk_box_append(GTK_BOX(top_vbox), footer_vbox);

    {
        GtkWidget *log_scroll;

        NEW_WITH_NAME(log_scroll, gtk_scrolled_window_new);

        gtk_scrolled_window_set_overlay_scrolling(GTK_SCROLLED_WINDOW(log_scroll), FALSE);
        gtk_widget_set_size_request(log_scroll, -1, 100);

        NEW_WITH_NAME(cecup.log_view, gtk_text_view_new);
        gtk_text_view_set_editable(GTK_TEXT_VIEW(cecup.log_view), FALSE);
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(cecup.log_view), GTK_WRAP_WORD_CHAR);
        cecup.log_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(cecup.log_view));
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(log_scroll), cecup.log_view);

        gtk_paned_set_end_child(GTK_PANED(v_paned), log_scroll);
    }

    {
        GtkAdjustment *l_adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scroll[L]));
        GtkAdjustment *r_adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scroll[R]));

        g_signal_connect(l_adj, "value-changed", G_CALLBACK(on_scroll_sync), r_adj);
        g_signal_connect(r_adj, "value-changed", G_CALLBACK(on_scroll_sync), l_adj);
    }

    {
        GtkGesture *log_gesture = gtk_gesture_click_new();

        gtk_widget_add_controller(cecup.log_view, GTK_EVENT_CONTROLLER(log_gesture));
        gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(log_gesture),
                                                   GTK_PHASE_CAPTURE);
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(log_gesture), 0);
        g_signal_connect(log_gesture, "pressed", G_CALLBACK(on_log_button_press), NULL);
    }

    {
        GActionMap *action_map = G_ACTION_MAP(cecup.application);
        GSimpleAction *action_copy_all = g_simple_action_new("copy_all", NULL);
        GSimpleAction *action_copy_line = g_simple_action_new("copy_line", G_VARIANT_TYPE_INT32);

        g_action_map_add_action(action_map, G_ACTION(action_copy_all));
        g_action_map_add_action(action_map, G_ACTION(action_copy_line));

        g_signal_connect(action_copy_all, "activate", G_CALLBACK(on_log_copy), "all");
        g_signal_connect(action_copy_line, "activate", G_CALLBACK(on_log_copy), "line");

    }

    do {
        GError *key_error = NULL;
        GKeyFile *key;
        char *value;
        bool boolean_value;

        key = g_key_file_new();
        if (!g_key_file_load_from_file(key, cecup.config_path,
                                       G_KEY_FILE_NONE, &key_error)) {
            if (!g_error_matches(key_error, G_FILE_ERROR,
                                 G_FILE_ERROR_NOENT)) {
                error("Error loading configuration file %s: %s.\n",
                      cecup.config_path, key_error->message);
            }
            g_clear_error(&key_error);
            g_key_file_free(key);
            break;
        }

        value = main_key_file_get_string(key, "Paths", "src");
        if (value) {
            gtk_editable_set_text(GTK_EDITABLE(cecup.dir_entry[L]), value);
            g_free(value);
        }

        value = main_key_file_get_string(key, "Paths", "dst");
        if (value) {
            gtk_editable_set_text(GTK_EDITABLE(cecup.dir_entry[R]), value);
            g_free(value);
        }

        value = main_key_file_get_string(key, "Tools", "diff");
        if (value) {
            gtk_editable_set_text(GTK_EDITABLE(cecup.diff_entry), value);
            g_free(value);
        }

        value = main_key_file_get_string(key, "Tools", "term");
        if (value) {
            gtk_editable_set_text(GTK_EDITABLE(cecup.term_entry), value);
            g_free(value);
        }

        if (main_key_file_get_boolean(key, "Filters", "new",
                                      &boolean_value)) {
            gtk_toggle_button_set_active(
                GTK_TOGGLE_BUTTON(cecup.filter_new), boolean_value);
        }
        if (main_key_file_get_boolean(key, "Filters", "hard",
                                      &boolean_value)) {
            gtk_toggle_button_set_active(
                GTK_TOGGLE_BUTTON(cecup.filter_link), boolean_value);
        }
        if (main_key_file_get_boolean(key, "Filters", "update",
                                      &boolean_value)) {
            gtk_toggle_button_set_active(
                GTK_TOGGLE_BUTTON(cecup.filter_update), boolean_value);
        }
        if (main_key_file_get_boolean(key, "Filters", "equal",
                                      &boolean_value)) {
            gtk_toggle_button_set_active(
                GTK_TOGGLE_BUTTON(cecup.filter_equal), boolean_value);
        }
        if (main_key_file_get_boolean(key, "Filters", "delete",
                                      &boolean_value)) {
            gtk_toggle_button_set_active(
                GTK_TOGGLE_BUTTON(cecup.filter_delete), boolean_value);
        }
        if (main_key_file_get_boolean(key, "Filters", "ignore",
                                      &boolean_value)) {
            gtk_toggle_button_set_active(
                GTK_TOGGLE_BUTTON(cecup.filter_ignore), boolean_value);
        }
        if (main_key_file_get_boolean(key, "Options", "check_fs",
                                      &boolean_value)) {
            gtk_check_button_set_active(
                GTK_CHECK_BUTTON(cecup.check_fs_button), boolean_value);
        }
        if (main_key_file_get_boolean(key, "Options", "delete_ignored",
                                      &boolean_value)) {
            cecup.delete_ignored = boolean_value;
            gtk_check_button_set_active(
                GTK_CHECK_BUTTON(cecup.delete_ignored_button), boolean_value);
        }
        if (main_key_file_get_boolean(key, "Options", "delete_after",
                                      &boolean_value)) {
            cecup.delete_after = boolean_value;
            gtk_check_button_set_active(
                GTK_CHECK_BUTTON(cecup.delete_after_button), boolean_value);
        }

        g_key_file_free(key);
    } while (0);

    cecup.entry_id[L] = g_signal_connect(cecup.dir_entry[L], "activate",
                                         G_CALLBACK(on_config_changed), NULL);
    cecup.entry_id[R] = g_signal_connect(cecup.dir_entry[R], "activate",
                                         G_CALLBACK(on_config_changed), NULL);

    g_signal_connect(cecup.browse_button[L], "clicked", G_CALLBACK(on_browse_src),      NULL);
    g_signal_connect(cecup.browse_button[R], "clicked", G_CALLBACK(on_browse_dst),      NULL);
    g_signal_connect(cecup.invert_button,    "clicked", G_CALLBACK(on_invert_clicked),  NULL);
    g_signal_connect(cecup.preview_button,   "clicked", G_CALLBACK(on_preview_clicked), NULL);
    g_signal_connect(cecup.stop_button,      "clicked", G_CALLBACK(on_stop_clicked),    NULL);
    g_signal_connect(cecup.sync_button,      "clicked", G_CALLBACK(on_sync_clicked),    NULL);
    g_signal_connect(cecup.ignore_button,    "clicked", G_CALLBACK(on_ignore_clicked),  NULL);
    g_signal_connect(cecup.unselect_button,  "clicked", G_CALLBACK(on_unselect_all_clicked), NULL);
    g_signal_connect(cecup.select_visible_button, "clicked", G_CALLBACK(on_select_all_visible_clicked), NULL);
    g_signal_connect(reset_button,           "clicked", G_CALLBACK(on_reset_clicked),   NULL);

    g_signal_connect(cecup.search_entry, "changed", G_CALLBACK(on_search_changed), NULL);

    g_signal_connect(cecup.filter_new,    "toggled", G_CALLBACK(on_filter_toggled), NULL);
    g_signal_connect(cecup.filter_link,   "toggled", G_CALLBACK(on_filter_toggled), NULL);
    g_signal_connect(cecup.filter_update, "toggled", G_CALLBACK(on_filter_toggled), NULL);
    g_signal_connect(cecup.filter_equal,  "toggled", G_CALLBACK(on_filter_toggled), NULL);
    g_signal_connect(cecup.filter_delete, "toggled", G_CALLBACK(on_filter_toggled), NULL);
    g_signal_connect(cecup.filter_ignore, "toggled", G_CALLBACK(on_filter_toggled), NULL);

    g_signal_connect(cecup.diff_entry, "changed", G_CALLBACK(on_config_changed), NULL);
    g_signal_connect(cecup.term_entry, "changed", G_CALLBACK(on_config_changed), NULL);

    g_signal_connect(cecup.check_fs_button, "toggled", G_CALLBACK(on_preview_setting_toggled), NULL);
    g_signal_connect(cecup.delete_ignored_button, "toggled", G_CALLBACK(on_delete_ignored_toggled), NULL);
    g_signal_connect(cecup.delete_after_button, "toggled", G_CALLBACK(on_delete_after_toggled), NULL);

    gtk_window_present(GTK_WINDOW(cecup.gtk_window));

    if (is_first_run || DEBUGGING) {
        GtkWidget *dialog = gtk_dialog_new_with_buttons(_("Welcome to cecup"), GTK_WINDOW(cecup.gtk_window),
                                                        GTK_DIALOG_MODAL,
                                                        _("_OK"), GTK_RESPONSE_OK,
                                                        NULL);
        GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
        GtkWidget *scroll_welcome = gtk_scrolled_window_new();
        GtkWidget *label = gtk_label_new(NULL);

        gtk_window_set_default_size(GTK_WINDOW(dialog), 640, 480);

        gtk_widget_set_vexpand(scroll_welcome, TRUE);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll_welcome), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

        gtk_label_set_markup(GTK_LABEL(label), _(cecup_welcome_text));
        gtk_label_set_wrap(GTK_LABEL(label), TRUE);
        gtk_label_set_xalign(GTK_LABEL(label), 0.0);
        gtk_label_set_yalign(GTK_LABEL(label), 0.0);
        gtk_widget_set_margin_start(label, 15);
        gtk_widget_set_margin_end(label, 15);
        gtk_widget_set_margin_top(label, 15);
        gtk_widget_set_margin_bottom(label, 15);

        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll_welcome), label);
        gtk_box_append(GTK_BOX(content_area), scroll_welcome);

        g_signal_connect(dialog, "response", G_CALLBACK(gtk_window_destroy), NULL);
        gtk_widget_show(dialog);
    }

    return;
}

int32
main(int32 argc, char **argv) {
    int32 status;
    Arena *arena;

    program = argv[0];
    timezone_init();

    ASSERT_EQUAL(!L, R);
    ASSERT_EQUAL(!R, L);

    disable_dbus_warning();

    main_setup_locale();

    xpthread_mutex_init(&cecup.arena_mutex, NULL);
    arena = arena_create(SIZEMB(64), "cecup.arena");
    ASSERT(arena);
    cecup.arena = arena;

    cecup.rows_len = 0;
    cecup.rows_capacity = INITIAL_CAPACITY;

    cecup.rows[L] = malloc2(cecup.rows_capacity*SIZEOF(*(cecup.rows[L])));
    cecup.rows[R] = malloc2(cecup.rows_capacity*SIZEOF(*(cecup.rows[R])));
    cecup.rows_visible = malloc2(cecup.rows_capacity*SIZEOF(*(cecup.rows_visible)));
    cecup.rows_selected = malloc2(cecup.rows_capacity*SIZEOF(*(cecup.rows_selected)));

    cecup.actions_set = hash_create_actions_set(INITIAL_CAPACITY, "cecup.actions_set");

    cecup.ntransfers = 0;
    cecup.transfers_capacity = INITIAL_CAPACITY;
    cecup.transfers = malloc2(cecup.transfers_capacity*SIZEOF(*(cecup.transfers)));
    cecup.transfers_lens = malloc2(cecup.transfers_capacity*SIZEOF(*(cecup.transfers_lens)));

    cecup.ndeletions = 0;
    cecup.deletions_capacity = INITIAL_CAPACITY;
    cecup.deletions = malloc2(cecup.deletions_capacity*SIZEOF(*(cecup.deletions)));
    cecup.deletions_lens = malloc2(cecup.deletions_capacity*SIZEOF(*(cecup.deletions_lens)));

    traversal_allocate(&cecup.traversal[L], L);
    traversal_allocate(&cecup.traversal[R], R);

    cecup.ignore_patterns = NULL;
    cecup.ignore_capacity = 0;
    cecup.ignore_count = 0;

    cecup.sort_col = COL_SRC_PATH;
    cecup.sort_order = GTK_SORT_ASCENDING;

    main_setup_config_paths();

    cecup.store = G_LIST_MODEL(cecup_list_model_new());
    stop_working(false);

    cecup.application = gtk_application_new("com.cecup.app", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(cecup.application, "activate", G_CALLBACK(main_application_run), NULL);
    status = g_application_run(G_APPLICATION(cecup.application), argc, argv);

    g_object_unref(cecup.application);
    g_object_unref(cecup.store);

    free2(cecup.rows[L], cecup.rows_capacity*SIZEOF(*(cecup.rows[L])));
    free2(cecup.rows[R], cecup.rows_capacity*SIZEOF(*(cecup.rows[R])));
    free2(cecup.rows_visible, cecup.rows_capacity*SIZEOF(*(cecup.rows_visible)));
    free2(cecup.rows_selected, cecup.rows_capacity*SIZEOF(uint8));

    free2(cecup.transfers, cecup.transfers_capacity*SIZEOF(*(cecup.transfers)));
    free2(cecup.transfers_lens, cecup.transfers_capacity*SIZEOF(*(cecup.transfers_lens)));
    hash_destroy_actions_set(cecup.actions_set);

    free2(cecup.deletions, cecup.deletions_capacity*SIZEOF(*(cecup.deletions)));
    free2(cecup.deletions_lens, cecup.deletions_capacity*SIZEOF(*(cecup.deletions_lens)));

    free2(cecup.base[L], cecup.base_len[L] + 1);
    free2(cecup.base[R], cecup.base_len[R] + 1);

    arena_destroy(arena);

    traversal_free(&cecup.traversal[L]);
    traversal_free(&cecup.traversal[R]);

    xpthread_mutex_destroy(&cecup.arena_mutex);

    exit(status);
}
