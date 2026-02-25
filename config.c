#if !defined(CONFIG_C)
#define CONFIG_C

#include "cecup.h"

static void
read_config(void) {
    GKeyFile *key;
    char *val;

    key = g_key_file_new();
    if (g_key_file_load_from_file(key, cecup_state.config_path, G_KEY_FILE_NONE,
                                  NULL)) {
        if ((val = g_key_file_get_string(key, "Paths", "src", NULL)) != NULL) {
            gtk_entry_set_text(GTK_ENTRY(cecup_state.src_entry), val);
            g_free(val);
        }
        if ((val = g_key_file_get_string(key, "Paths", "dst", NULL)) != NULL) {
            gtk_entry_set_text(GTK_ENTRY(cecup_state.dst_entry), val);
            g_free(val);
        }
        if ((val = g_key_file_get_string(key, "Tools", "diff", NULL)) != NULL) {
            gtk_entry_set_text(GTK_ENTRY(cecup_state.diff_entry), val);
            g_free(val);
        }
        if ((val = g_key_file_get_string(key, "Tools", "term", NULL)) != NULL) {
            gtk_entry_set_text(GTK_ENTRY(cecup_state.term_entry), val);
            g_free(val);
        }

        if (g_key_file_has_key(key, "Filters", "new", NULL)) {
            gtk_toggle_button_set_active(
                GTK_TOGGLE_BUTTON(cecup_state.filter_new),
                g_key_file_get_boolean(key, "Filters", "new", NULL));
        }
        if (g_key_file_has_key(key, "Filters", "hard", NULL)) {
            gtk_toggle_button_set_active(
                GTK_TOGGLE_BUTTON(cecup_state.filter_hard),
                g_key_file_get_boolean(key, "Filters", "hard", NULL));
        }
        if (g_key_file_has_key(key, "Filters", "update", NULL)) {
            gtk_toggle_button_set_active(
                GTK_TOGGLE_BUTTON(cecup_state.filter_update),
                g_key_file_get_boolean(key, "Filters", "update", NULL));
        }
        if (g_key_file_has_key(key, "Filters", "equal", NULL)) {
            gtk_toggle_button_set_active(
                GTK_TOGGLE_BUTTON(cecup_state.filter_equal),
                g_key_file_get_boolean(key, "Filters", "equal", NULL));
        }
        if (g_key_file_has_key(key, "Filters", "delete", NULL)) {
            gtk_toggle_button_set_active(
                GTK_TOGGLE_BUTTON(cecup_state.filter_delete),
                g_key_file_get_boolean(key, "Filters", "delete", NULL));
        }
        if (g_key_file_has_key(key, "Filters", "ignore", NULL)) {
            gtk_toggle_button_set_active(
                GTK_TOGGLE_BUTTON(cecup_state.filter_ignore),
                g_key_file_get_boolean(key, "Filters", "ignore", NULL));
        }
        if (g_key_file_has_key(key, "Options", "check_fs", NULL)) {
            gtk_toggle_button_set_active(
                GTK_TOGGLE_BUTTON(cecup_state.check_fs_toggle),
                g_key_file_get_boolean(key, "Options", "check_fs", NULL));
        }
    }
    g_key_file_free(key);
}

static void
save_config(void) {
    GKeyFile *key;
    char *out;
    gsize len;

    key = g_key_file_new();
    g_key_file_set_string(key, "Paths", "src",
                          gtk_entry_get_text(GTK_ENTRY(cecup_state.src_entry)));
    g_key_file_set_string(key, "Paths", "dst",
                          gtk_entry_get_text(GTK_ENTRY(cecup_state.dst_entry)));
    g_key_file_set_string(
        key, "Tools", "diff",
        gtk_entry_get_text(GTK_ENTRY(cecup_state.diff_entry)));
    g_key_file_set_string(
        key, "Tools", "term",
        gtk_entry_get_text(GTK_ENTRY(cecup_state.term_entry)));
    g_key_file_set_boolean(key, "Filters", "new",
                           gtk_toggle_button_get_active(
                               GTK_TOGGLE_BUTTON(cecup_state.filter_new)));
    g_key_file_set_boolean(key, "Filters", "hard",
                           gtk_toggle_button_get_active(
                               GTK_TOGGLE_BUTTON(cecup_state.filter_hard)));
    g_key_file_set_boolean(key, "Filters", "update",
                           gtk_toggle_button_get_active(
                               GTK_TOGGLE_BUTTON(cecup_state.filter_update)));
    g_key_file_set_boolean(key, "Filters", "equal",
                           gtk_toggle_button_get_active(
                               GTK_TOGGLE_BUTTON(cecup_state.filter_equal)));
    g_key_file_set_boolean(key, "Filters", "delete",
                           gtk_toggle_button_get_active(
                               GTK_TOGGLE_BUTTON(cecup_state.filter_delete)));
    g_key_file_set_boolean(key, "Filters", "ignore",
                           gtk_toggle_button_get_active(
                               GTK_TOGGLE_BUTTON(cecup_state.filter_ignore)));
    g_key_file_set_boolean(key, "Options", "check_fs",
                           gtk_toggle_button_get_active(
                               GTK_TOGGLE_BUTTON(cecup_state.check_fs_toggle)));

    out = g_key_file_to_data(key, &len, NULL);
    g_file_set_contents(cecup_state.config_path, out, (gssize)len, NULL);

    g_free(out);
    g_key_file_free(key);
    return;
}

#endif /* CONFIG_C */
