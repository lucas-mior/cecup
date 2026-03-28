#if !defined(ON_H)
#define ON_H

#if !defined(__INCLUDE_LEVEL__) || (__INCLUDE_LEVEL__ >= 1)

#include <gtk/gtk.h>
#include "cecup.h"

static void on_menu_open_item(GtkWidget *m, void *data);
static void on_menu_copy_path(GtkWidget *m, void *data);
static void on_menu_apply(GtkWidget *m, void *data);
static void on_menu_diff(GtkWidget *m, void *data);
static void on_menu_rename(GtkWidget *m, void *data);
static void on_menu_delete(GtkWidget *m, void *data);

static void on_popover_closed(GtkWidget *popover, void *data);

static CecupMenuItem tree_menu_items[] = {
{N_("📄 Open File"),          0,          0,                                 on_menu_open_item, "file"},
{N_("📂 Open Folder"),        0,          0,                                 on_menu_open_item, "folder"},
{N_("📍 Copy Full Path"),     GDK_KEY_c,  GDK_CONTROL_MASK,                  on_menu_copy_path, "absolute"},
{N_("📋 Copy Relative Path"), GDK_KEY_c,  GDK_CONTROL_MASK | GDK_SHIFT_MASK, on_menu_copy_path, "relative"},
{N_("⏯️ Apply"),              0,          0,                                 on_menu_apply,     NULL},
{N_("🔍 Diff"),               0,          0,                                 on_menu_diff,      NULL},
{N_("✏️ Rename"),              GDK_KEY_F2, 0,                                 on_menu_rename,    NULL},
{N_("🗑️ Delete"),             0,          0,                                 on_menu_delete,    NULL},
{N_("💤 Ignore..."),          0,          0,                                 NULL,              NULL},
};

#endif /* !defined(__INCLUDE_LEVEL__) || (__INCLUDE_LEVEL__ >= 1) */

#endif /* ON_H */
