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

#if !defined (ON_H)
#define ON_H

#include "cecup.h"

static void on_popover_closed(GtkWidget *popover, void *data);
static void execute_menu_item_from_key_press(GtkWidget *tree, CecupMenuItem *menu_item);

static void on_menu_open_item(GtkWidget *m, void *data);
static void on_menu_copy_path(GtkWidget *m, void *data);
static void on_menu_apply(GtkWidget *m, void *data);
static void on_menu_diff(GtkWidget *m, void *data);
static void on_menu_rename(GtkWidget *m, void *data);
static void on_menu_delete(GtkWidget *m, void *data);

static CecupMenuItem tree_menu_items[] = {
{N_("📄 Open File"),          0,          0,                                 on_menu_open_item, "file",      N_("open this file using default program")},
{N_("📂 Open Folder"),        0,          0,                                 on_menu_open_item, "folder",    N_("open the folder of this file using default program")},
{N_("📍 Copy Full Path"),     GDK_KEY_c,  GDK_CONTROL_MASK,                  on_menu_copy_path, "absolute",  N_("copy complete path of this file")},
{N_("📋 Copy Relative Path"), GDK_KEY_c,  GDK_CONTROL_MASK | GDK_SHIFT_MASK, on_menu_copy_path, "relative",  N_("copy path of this file (relative to original/backup folder path)")},
{N_("⏯️ Apply"),              0,          0,                                 on_menu_apply,     NULL,        N_("Apply (only selected or file under cursor)")},
{N_("🔍 Diff"),               0,          0,                                 on_menu_diff,      NULL,        N_("Compare original and backup files using external diff tool")},
{N_("✏️ Rename"),              GDK_KEY_F2, 0,                                 on_menu_rename,    NULL,        N_("Rename file under cursor")},
{N_("🗑️ Delete"),             0,          0,                                 on_menu_delete,    NULL,        N_("Delete this file")},
{N_("💤 Ignore..."),          0,          0,                                 NULL,              NULL,        N_("Add ignore pattern for this file")},
};

static char *
get_variant(GtkWidget *widget, const char *function) {
    char *variant;

    if ((variant = g_object_get_data(G_OBJECT(widget), "variant")) == NULL) {
        error("Error in %s: \"variant\" not passed to widget.\n", function);
        fatal(EXIT_FAILURE);
    }

    return variant;
}

#endif /* ON_H */
