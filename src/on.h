#if !defined (ON_H)
#define ON_H

#include "cecup.h"

static CecupMenuItem tree_menu_items[] = {
{N_("📄 Open File"),          0,          0,                                 on_menu_open_item, "file",      N_("open this file using default program")},
{N_("📂 Open Folder"),        0,          0,                                 on_menu_open_item, "folder",    N_("open the folder of this file using default program")},
{N_("📍 Copy Full Path"),     GDK_KEY_c,  GDK_CONTROL_MASK,                  on_menu_copy_path, "absolute",  N_("copy complete path of this file")},
{N_("📋 Copy Relative Path"), GDK_KEY_c,  GDK_CONTROL_MASK | GDK_SHIFT_MASK, on_menu_copy_path, "relative",  N_("copy path of this file (relative to original/backup path)")},
{N_("⏯️ Apply"),              0,          0,                                 on_menu_apply,     NULL,        N_("Apply (onlyt selected or file under cursor)")},
{N_("🔍 Diff"),               0,          0,                                 on_menu_diff,      NULL,        N_("Compare original and backup files using external diff tool")},
{N_("✏️ Rename"),              GDK_KEY_F2, 0,                                 on_menu_rename,    NULL,        N_("Rename file under cursor")},
{N_("🗑️ Delete"),             0,          0,                                 on_menu_delete,    NULL,        N_("Delete this file")},
{N_("💤 Ignore..."),          0,          0,                                 NULL,              NULL,        N_("Add ignore pattern for this file")},
};

#endif /* ON_H */
