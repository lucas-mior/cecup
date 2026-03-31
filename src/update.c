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

#if !defined(UPDATE_C)
#define UPDATE_C

#include <gtk/gtk.h>
#include <glib/gmain.h>
#include <stdlib.h>

#include "util.c"
#include "i18n.h"

#include "cecup.h"
#include "aux.c"
#include "list_model.c"
#include "ignore_patterns.c"
#include "item.c"

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_update 1
#elif !defined(TESTING_update)
#define TESTING_update 0
#endif

#define UI_INTERVAL_MS 100

static void update_list_from_rows(void);
static void update_stats_text(int32 count_selected, int64 total_size_bytes);

static bool
update_row_remove(Message *message) {
    char *path_removed = message->src_path;
    int32 path_removed_len = message->path_len;
    int32 side = message->side;
    bool changed = false;
    Traversal *traversal;

    if (side == L) {
        traversal = &cecup.traversal_src;
    } else {
        traversal = &cecup.traversal_dst;
    }

    if (path_removed == NULL || path_removed_len == 0) {
        return false;
    }

    if (path_removed[path_removed_len - 1] != '/') {
        int32 *idx_ptr;
        int32 row_id;
        int32 *side_ptr;

        if ((idx_ptr = hash_lookup_fs_map(traversal->map,
                                          path_removed, path_removed_len)) == NULL) {
            return false;
        }

        if ((row_id = traversal->row_ids[*idx_ptr]) < 0) {
            return false;
        }

        changed = true;
        if (side == L) {
            side_ptr = &cecup.rows_src[row_id];
        } else {
            side_ptr = &cecup.rows_dst[row_id];
        }

        if (traversal->stats[*idx_ptr].st_nlink > 1) {
            char inode[32];
            int32 inode_len;
            int32 *first_idx_ptr;

            inode_len = ITOA(inode, (long)traversal->stats[*idx_ptr].st_ino);
            if ((first_idx_ptr = hash_lookup_inode_map(traversal->inode_map, inode, inode_len))) {
                traversal->nlinks[*first_idx_ptr] -= 1;
                if (traversal->nlinks[*first_idx_ptr] <= 0) {
                    hash_remove_inode_map(traversal->inode_map, inode, inode_len);
                }
            }
        }

        hash_remove_fs_map(traversal->map,
                           traversal->paths[*idx_ptr], traversal->paths_lens[*idx_ptr]);
        memset64(&traversal->stats[*idx_ptr], 0, SIZEOF(struct stat));
        traversal->row_ids[*idx_ptr] = -1;
        *side_ptr = -1;

        if ((cecup.rows_src[row_id] == -1) && (cecup.rows_dst[row_id] == -1)) {
            for (int32 j = row_id; j < (cecup.rows_len - 1); j += 1) {
                int32 idx_src;
                int32 idx_dst;

                cecup.rows_src[j] = cecup.rows_src[j + 1];
                cecup.rows_dst[j] = cecup.rows_dst[j + 1];
                cecup.rows_selected[j] = cecup.rows_selected[j + 1];

                idx_src = cecup.rows_src[j];
                idx_dst = cecup.rows_dst[j];

                if (idx_src >= 0) {
                    cecup.traversal_src.row_ids[idx_src] = j;
                }
                if (idx_dst >= 0) {
                    cecup.traversal_dst.row_ids[idx_dst] = j;
                }
            }
            cecup.rows_len -= 1;
        }
    } else {
        for (int32 i = 0; i < cecup.rows_len;) {
            int32 row_id = i;
            char *path = item_path_get(row_id);
            int32 *idx_ptr;

            if (!BEGINS_WITH(path, path_removed, path_removed_len)) {
                i += 1;
                continue;
            }

            changed = true;
            if (side == L) {
                idx_ptr = &cecup.rows_src[row_id];
            } else {
                idx_ptr = &cecup.rows_dst[row_id];
            }

            if (*idx_ptr >= 0) {
                int32 idx = *idx_ptr;

                if (traversal->stats[idx].st_nlink > 1) {
                    char inode[32];
                    int32 inode_len;
                    int32 *first_idx_ptr;

                    inode_len = ITOA(inode, (long)traversal->stats[idx].st_ino);
                    if ((first_idx_ptr = hash_lookup_inode_map(traversal->inode_map,
                                                               inode, inode_len))) {
                        traversal->nlinks[*first_idx_ptr] -= 1;
                        if (traversal->nlinks[*first_idx_ptr] <= 0) {
                            hash_remove_inode_map(traversal->inode_map, inode, inode_len);
                        }
                    }
                }

                hash_remove_fs_map(traversal->map,
                                   traversal->paths[idx], traversal->paths_lens[idx]);
                memset64(&traversal->stats[idx], 0, SIZEOF(struct stat));
                traversal->row_ids[idx] = -1;
                *idx_ptr = -1;
            }

            if ((cecup.rows_src[row_id] == -1)
                && (cecup.rows_dst[row_id] == -1)) {
                for (int32 j = i; j < (cecup.rows_len - 1); j += 1) {
                    int32 idx_src;
                    int32 idx_dst;

                    cecup.rows_src[j] = cecup.rows_src[j + 1];
                    cecup.rows_dst[j] = cecup.rows_dst[j + 1];
                    cecup.rows_selected[j] = cecup.rows_selected[j + 1];

                    idx_src = cecup.rows_src[j];
                    idx_dst = cecup.rows_dst[j];

                    if (idx_src >= 0) {
                        cecup.traversal_src.row_ids[idx_src] = j;
                    }
                    if (idx_dst >= 0) {
                        cecup.traversal_dst.row_ids[idx_dst] = j;
                    }
                }
                cecup.rows_len -= 1;
            } else {
                i += 1;
            }
        }
    }

    traversal_patch_nlinks(traversal);

    if (DEBUGGING) {
        check_consistent_state();
    }

    if (changed) {
        invalidate_preview();
    }

    return changed;
}

static bool
update_row_transfer(Message *message) {
    Traversal *traversal_src = &cecup.traversal_src;
    Traversal *traversal_dst = &cecup.traversal_dst;

    char *path_transfered = message->src_path;
    int32 path_transfered_len = message->path_len;
    int32 *idx_ptr;
    int32 idx_src;
    int32 row_id;

    if (path_transfered == NULL || path_transfered_len == 0) {
        return false;
    }

    if ((idx_ptr
         = hash_lookup_fs_map(traversal_src->map,
                              path_transfered, path_transfered_len)) == NULL) {
        LOG_ERROR("Warning: transfered file does not exist in source.\n");
        if (DEBUGGING) {
            fatal(EXIT_FAILURE);
        }
        return false;
    }

    idx_src = *idx_ptr;
    row_id = traversal_src->row_ids[idx_src];

    if (row_id < 0) {
        return false;
    }

    if (cecup.rows_dst[row_id] < 0) {
        int32 *lookup;

        if ((lookup = hash_lookup_fs_map(traversal_dst->map,
                                         path_transfered, path_transfered_len))) {
            cecup.rows_dst[row_id] = *lookup;
        } else {
            cecup.rows_dst[row_id] = traversal_push(traversal_dst, &traversal_src->stats[idx_src],
                                                    traversal_src->paths[idx_src],
                                                    traversal_src->paths_lens[idx_src],
                                                    traversal_src->link_targets[idx_src],
                                                    traversal_src->link_targets_lens[idx_src],
                                                    traversal_src->patterns[idx_src],
                                                    traversal_src->patterns_lens[idx_src]);

            if (traversal_src->stats[idx_src].st_nlink > 1) {
                char inode[32];
                int32 inode_len;
                int32 *first_idx_ptr;

                inode_len = ITOA(inode, (long)traversal_src->stats[idx_src].st_ino);
                if ((first_idx_ptr = hash_lookup_inode_map(traversal_dst->inode_map,
                                                           inode, inode_len))) {
                    traversal_dst->nlinks[*first_idx_ptr] += 1;
                } else {
                    hash_insert_inode_map(traversal_dst->inode_map,
                                          inode, inode_len, cecup.rows_dst[row_id]);
                    traversal_dst->nlinks[cecup.rows_dst[row_id]] = 1;
                }
            } else {
                traversal_dst->nlinks[cecup.rows_dst[row_id]] = 1;
            }
        }
        traversal_dst->row_ids[cecup.rows_dst[row_id]] = row_id;
    } else {
        memcpy64(&traversal_dst->stats[cecup.rows_dst[row_id]],
                 &traversal_src->stats[idx_src], SIZEOF(struct stat));
    }

    traversal_patch_nlinks(traversal_src);
    traversal_patch_nlinks(traversal_dst);

    if (DEBUGGING) {
        check_consistent_state();
    }

    invalidate_preview();
    return true;
}

static bool
update_row_rename(Message *message) {
    Traversal *traversal;
    Traversal *other_traversal;
    char *old_path = message->old_path;
    char *new_path = message->new_path;
    int32 old_path_len = message->old_path_len;
    int32 new_path_len = message->new_path_len;
    int32 side = message->side;
    bool is_dir = (old_path[old_path_len - 1] == '/');
    bool changed = false;

    if (side == L) {
        traversal = &cecup.traversal_src;
        other_traversal = &cecup.traversal_dst;
    } else {
        traversal = &cecup.traversal_dst;
        other_traversal = &cecup.traversal_src;
    }

    for (int32 i = 0; i < cecup.rows_len;) {
        int32 row_id = i;
        char *path_old = item_path_side(row_id, side);
        char *path_new;
        int32 idx;
        int32 other_idx;
        int32 n_idx;
        int32 sub_len;
        int32 suffix_len;
        int32 merge_row_id;
        IgnorePattern *p_match;
        bool is_match = false;

        if (path_old) {
            if (is_dir) {
                if (BEGINS_WITH(path_old, old_path, old_path_len)) {
                    is_match = true;
                }
            } else if (strcmp(path_old, old_path) == 0) {
                is_match = true;
            }
        }

        if (!is_match) {
            i += 1;
            continue;
        }

        changed = true;
        sub_len = item_path_len_side(row_id, side);
        suffix_len = sub_len - old_path_len;
        path_new = xarena_push(traversal->arena, new_path_len + suffix_len + 1);
        memcpy64(path_new, new_path, new_path_len);
        memcpy64(path_new + new_path_len, path_old + old_path_len, suffix_len + 1);

        if (side == L) {
            idx = cecup.rows_src[row_id];
            other_idx = cecup.rows_dst[row_id];
        } else {
            idx = cecup.rows_dst[row_id];
            other_idx = cecup.rows_src[row_id];
        }

        hash_remove_fs_map(traversal->map, traversal->paths[idx], traversal->paths_lens[idx]);
        traversal->row_ids[idx] = -1;

        p_match = ignore_patterns_match(path_new, new_path_len + suffix_len,
                                        S_ISDIR(traversal->stats[idx].st_mode),
                                        cecup.ignore_patterns, cecup.ignore_count);

        {
            char *p_match_str;
            int32 p_match_len;

            if (p_match) {
                p_match_str = p_match->str;
                p_match_len = p_match->len;
            } else {
                p_match_str = NULL;
                p_match_len = 0;
            }

            n_idx = traversal_push(traversal, &traversal->stats[idx],
                                   path_new, new_path_len + suffix_len,
                                   traversal->link_targets[idx],
                                   traversal->link_targets_lens[idx],
                                   p_match_str, p_match_len);
        }

        merge_row_id = -1;
        {
            int32 *m_idx_ptr;
            if ((m_idx_ptr = hash_lookup_fs_map(other_traversal->map,
                                                path_new, new_path_len + suffix_len))) {
                merge_row_id = other_traversal->row_ids[*m_idx_ptr];
            }
        }

        if (merge_row_id >= 0) {
            if (side == L) {
                cecup.rows_src[merge_row_id] = n_idx;
            } else {
                cecup.rows_dst[merge_row_id] = n_idx;
            }
            traversal->row_ids[n_idx] = merge_row_id;

            if (other_idx >= 0) {
                if (side == L) {
                    cecup.rows_src[row_id] = -1;
                } else {
                    cecup.rows_dst[row_id] = -1;
                }
                i += 1;
            } else {
                for (int32 j = i; j < (cecup.rows_len - 1); j += 1) {
                    int32 idx_src;
                    int32 idx_dst;

                    cecup.rows_src[j] = cecup.rows_src[j + 1];
                    cecup.rows_dst[j] = cecup.rows_dst[j + 1];
                    cecup.rows_selected[j] = cecup.rows_selected[j + 1];

                    idx_src = cecup.rows_src[j];
                    idx_dst = cecup.rows_dst[j];

                    if (idx_src >= 0) {
                        cecup.traversal_src.row_ids[idx_src] = j;
                    }
                    if (idx_dst >= 0) {
                        cecup.traversal_dst.row_ids[idx_dst] = j;
                    }
                }
                cecup.rows_len -= 1;
            }
        } else {
            if (other_idx >= 0) {
                if (side == L) {
                    cecup.rows_src[row_id] = n_idx;
                    cecup.rows_dst[row_id] = -1;
                    cecup.traversal_src.row_ids[n_idx] = row_id;
                    item_add(-1, other_idx);
                } else {
                    cecup.rows_dst[row_id] = n_idx;
                    cecup.rows_src[row_id] = -1;
                    cecup.traversal_dst.row_ids[n_idx] = row_id;
                    item_add(other_idx, -1);
                }
            } else {
                if (side == L) {
                    cecup.rows_src[row_id] = n_idx;
                } else {
                    cecup.rows_dst[row_id] = n_idx;
                }
                traversal->row_ids[n_idx] = row_id;
            }
            i += 1;
        }

        if (!is_dir) {
            break;
        }
    }

    traversal_patch_nlinks(traversal);
    traversal_patch_nlinks(other_traversal);

    if (DEBUGGING) {
        check_consistent_state();
    }

    if (changed) {
        invalidate_preview();
    }

    return changed;
}

static bool
update_row_ignore(Message *message) {
    (void)message;

    invalidate_preview();
    ignore_patterns_load();

    for (int32 row_id = 0; row_id < cecup.rows_len; row_id += 1) {
        char *path = item_path_get(row_id);
        int32 path_len = item_path_len_get(row_id);
        bool is_dir = false;
        IgnorePattern *match;

        if (path_len > 0) {
            if (path[path_len - 1] == '/') {
                is_dir = true;
            }
        }

        match = ignore_patterns_match(path, path_len, is_dir,
                                      cecup.ignore_patterns, cecup.ignore_count);

        if (cecup.rows_src[row_id] >= 0) {
            int32 idx_src = cecup.rows_src[row_id];
            bool was_ignored = (cecup.traversal_src.patterns[idx_src] != NULL);
            bool is_ignored = match;

            if (was_ignored != is_ignored) {
                if (cecup.traversal_src.stats[idx_src].st_nlink > 1) {
                    char inode[32];
                    int32 inode_len;
                    int32 *first_idx_ptr;

                    inode_len = ITOA(inode, (long)cecup.traversal_src.stats[idx_src].st_ino);
                    if ((first_idx_ptr = hash_lookup_inode_map(cecup.traversal_src.inode_map,
                                                               inode, inode_len))) {
                        if (is_ignored) {
                            cecup.traversal_src.nlinks[*first_idx_ptr] -= 1;
                        } else {
                            cecup.traversal_src.nlinks[*first_idx_ptr] += 1;
                        }
                    }
                }
            }

            if (match) {
                cecup.traversal_src.patterns[idx_src] = match->str;
                cecup.traversal_src.patterns_lens[idx_src] = (int16)match->len;
            } else {
                cecup.traversal_src.patterns[idx_src] = NULL;
                cecup.traversal_src.patterns_lens[idx_src] = 0;
            }
        }

        if (cecup.rows_dst[row_id] >= 0) {
            int32 idx_dst = cecup.rows_dst[row_id];
            bool was_ignored = cecup.traversal_dst.patterns[idx_dst];
            bool is_ignored = match;

            if (was_ignored != is_ignored) {
                if (cecup.traversal_dst.stats[idx_dst].st_nlink > 1) {
                    char inode[32];
                    int32 inode_len;
                    int32 *first_idx_ptr;

                    inode_len = ITOA(inode, (long)cecup.traversal_dst.stats[idx_dst].st_ino);
                    if ((first_idx_ptr = hash_lookup_inode_map(cecup.traversal_dst.inode_map,
                                                               inode, inode_len))) {
                        if (is_ignored) {
                            cecup.traversal_dst.nlinks[*first_idx_ptr] -= 1;
                        } else {
                            cecup.traversal_dst.nlinks[*first_idx_ptr] += 1;
                        }
                    }
                }
            }

            if (match) {
                cecup.traversal_dst.patterns[idx_dst] = match->str;
                cecup.traversal_dst.patterns_lens[idx_dst] = (int16)match->len;
            } else {
                cecup.traversal_dst.patterns[idx_dst] = NULL;
                cecup.traversal_dst.patterns_lens[idx_dst] = 0;
            }
        }
    }

    traversal_patch_nlinks(&cecup.traversal_src);
    traversal_patch_nlinks(&cecup.traversal_dst);

    if (DEBUGGING) {
        check_consistent_state();
    }

    return true;
}

static void
update_list_from_rows(void) {
    int32 count_new = 0;
    int32 count_hard = 0;
    int32 count_update = 0;
    int32 count_equal = 0;
    int32 count_delete = 0;
    int32 count_ignore = 0;
    int32 count_selected = 0;
    int32 current_store_count = 0;

    int64 total_size_bytes = 0;
    char button_label[64];

    struct timespec t0;
    struct timespec t1;

    bool show_new = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_new));
    bool show_link = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_hard));
    bool show_update = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_update));
    bool show_equal = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_equal));
    bool show_delete = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_delete));
    bool show_ignore = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cecup.filter_ignore));

    clock_gettime(CLOCK_MONOTONIC_RAW, &t0);
    current_store_count = (int32)g_list_model_get_n_items(cecup.store);

    cecup.rows_visible_len = 0;
    for (int32 i = 0; i < cecup.rows_len; i += 1) {
        int32 row_id;
        enum Action src_act;
        enum Action dst_act;
        enum Reason reason;
        int64 sz;
        bool is_visible = false;

        row_id = i;
        if (cecup.rows_selected[row_id]) {
            count_selected += 1;
        }

        item_get_actions_reasons(row_id, &src_act, &dst_act, &reason);

        sz = item_size_side(row_id, L);
        if (sz < 0) {
            sz = 0;
        }

        switch (src_act) {
        case ACTION_NEW:
            count_new += 1;
            total_size_bytes += sz;
            is_visible = show_new;
            break;
        case ACTION_HARDLINK:
        case ACTION_SYMLINK:
            count_hard += 1;
            total_size_bytes += sz;
            is_visible = show_link;
            break;
        case ACTION_UPDATE:
            count_update += 1;
            total_size_bytes += sz;
            is_visible = show_update;
            break;
        case ACTION_EQUAL:
            count_equal += 1;
            is_visible = show_equal;
            break;
        case ACTION_DELETED:
        case ACTION_DELETE:
            count_delete += 1;
            is_visible = show_delete;
            break;
        case ACTION_IGNORE:
            if (dst_act == ACTION_DELETE) {
                count_delete += 1;
                is_visible = show_delete;
            } else {
                count_ignore += 1;
                is_visible = show_ignore;
            }
            break;
        case ACTION_LAST:
        default:
            break;
        }

        if (!is_visible) {
            continue;
        }

        if (cecup.search_query_len > 0) {
            char *path = item_path_get(row_id);
            if (path == NULL) {
                continue;
            }

            if (strcasestr(path, cecup.search_query) == NULL) {
                continue;
            }
        }

        cecup.rows_visible[cecup.rows_visible_len] = row_id;
        cecup.rows_visible_len += 1;
    }

    SNPRINTF(button_label, "%s %d", EMOJI_NEW, count_new);
    gtk_button_set_label(GTK_BUTTON(cecup.filter_new), button_label);

    SNPRINTF(button_label, "%s/%s %d", EMOJI_LINK, EMOJI_SYMLINK, count_hard);
    gtk_button_set_label(GTK_BUTTON(cecup.filter_hard), button_label);

    SNPRINTF(button_label, "%s %d", EMOJI_UPDATE, count_update);
    gtk_button_set_label(GTK_BUTTON(cecup.filter_update), button_label);

    SNPRINTF(button_label, "%s %d", EMOJI_EQUAL, count_equal);
    gtk_button_set_label(GTK_BUTTON(cecup.filter_equal), button_label);

    SNPRINTF(button_label, "%s %d", EMOJI_DELETE, count_delete);
    gtk_button_set_label(GTK_BUTTON(cecup.filter_delete), button_label);

    SNPRINTF(button_label, "%s %d", EMOJI_IGNORE, count_ignore);
    gtk_button_set_label(GTK_BUTTON(cecup.filter_ignore), button_label);

    update_stats_text(count_selected, total_size_bytes);

    if (cecup.rows_visible_len > 0) {
        qsort64(cecup.rows_visible, cecup.rows_visible_len, SIZEOF(int32), cecup_item_compare);
    }

    cecup_list_model_update(CECUP_LIST_MODEL(cecup.store),
                            (int32)current_store_count, cecup.rows_visible_len);

    clock_gettime(CLOCK_MONOTONIC_RAW, &t1);
    PRINT_TIMINGS(cecup.rows_visible_len, t0, t1);
    return;
}

static void
update_stats_text(int32 count_selected, int64 total_size_bytes) {
    char pretty_size[16];
    char stats_text[512];

    bytes_pretty(pretty_size, total_size_bytes);

    if (count_selected > 0) {
        SNPRINTF(stats_text,
                 _("<span foreground='red'>"
                   "Selected files: %d"
                   "</span>\nTotal Transfer Size: 📦 %s"),
                 count_selected, pretty_size);
    } else {
        SNPRINTF(stats_text,
                 _("Selected files: %d\n"
                   "Total Transfer Size: 📦 %s"),
                 count_selected, pretty_size);
    }

    gtk_label_set_markup(GTK_LABEL(cecup.stats_label), stats_text);
    return;
}

static bool
update_ui_process_message(Message *message) {
    GtkTextIter end;
    GtkTextIter start_line;
    GtkTextTagTable *table;
    int32 current_store_count;
    bool is_cr;
    bool buffer_ends_in_lf;
    bool needs_update;

    needs_update = false;

    switch (message->type) {
    case MSG_LOG:
    case MSG_LOG_CMD:
    case MSG_LOG_ERROR:
        is_cr = false;

        if (message->text_len > 0) {
            if (message->text[message->text_len - 1] == '\r') {
                is_cr = true;
                message->text[message->text_len - 1] = '\0';
            }
        }

        gtk_text_buffer_get_end_iter(cecup.log_buffer, &end);

        buffer_ends_in_lf = true;
        if (gtk_text_iter_get_offset(&end) > 0) {
            GtkTextIter last_char;

            last_char = end;
            gtk_text_iter_backward_char(&last_char);
            if (gtk_text_iter_get_char(&last_char) != '\n') {
                buffer_ends_in_lf = false;
            }
        }

        // TODO: Logic Bug. If `!buffer_ends_in_lf` is true (meaning the previous log didn't end
        // with a newline), this block deletes the entire line before inserting the new message.
        // This will completely break piecewise logging (e.g., `LOG("Loading... "); LOG("Done\n");`)
        // by erasing the first part. You should likely remove the `|| !buffer_ends_in_lf`
        // condition.
        if (is_cr || !buffer_ends_in_lf) {
            start_line = end;
            gtk_text_iter_set_line_offset(&start_line, 0);
            gtk_text_buffer_delete(cecup.log_buffer, &start_line, &end);
            gtk_text_buffer_get_end_iter(cecup.log_buffer, &end);
        }

        table = gtk_text_buffer_get_tag_table(cecup.log_buffer);

        #pragma clang diagnostic push
        #pragma clang diagnostic ignored "-Wswitch-enum"
        switch (message->type) {
        case MSG_LOG_ERROR:
            if (gtk_text_tag_table_lookup(table, "err_red") == NULL) {
                gtk_text_buffer_create_tag(cecup.log_buffer, "err_red", "foreground", "red", NULL);
            }
            gtk_text_buffer_insert_with_tags_by_name(
                cecup.log_buffer, &end, message->text, -1, "err_red", NULL);
            break;
        case MSG_LOG_CMD:
            if (gtk_text_tag_table_lookup(table, "err_blue") == NULL) {
                gtk_text_buffer_create_tag(cecup.log_buffer, "err_blue", "foreground", "blue", NULL);
            }
            gtk_text_buffer_insert_with_tags_by_name(
                cecup.log_buffer, &end, message->text, -1, "err_blue", NULL);
            break;
        default:
            gtk_text_buffer_insert(cecup.log_buffer, &end, message->text, -1);
            break;
        }
        #pragma clang diagnostic pop

        gtk_text_buffer_get_end_iter(cecup.log_buffer, &end);
        gtk_text_buffer_place_cursor(cecup.log_buffer, &end);
        gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(cecup.log_view),
                                     gtk_text_buffer_get_insert(cecup.log_buffer), 0.0,
                                     FALSE, 0.0, 0.0);
        break;
    case MSG_PROGRESS:
        if (message->fraction >= 0.0) {
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(cecup.progress_bar), message->fraction);
        }
        if (message->text) {
            gtk_progress_bar_set_text(GTK_PROGRESS_BAR(cecup.progress_bar), message->text);
        }
        if (message->src_path) {
            gtk_widget_set_tooltip_text(cecup.progress_bar, message->src_path);
        }
        break;
    case MSG_ROW_REMOVE:
        needs_update = update_row_remove(message);
        break;
    case MSG_ROW_RENAME:
        needs_update = update_row_rename(message);
        break;
    case MSG_ROW_TRANSFER:
        needs_update = update_row_transfer(message);
        break;
    case MSG_IGNORE_PATTERN:
        needs_update = update_row_ignore(message);
        break;
    case MSG_ENABLE_BUTTONS:
        if (cecup.refresh_id != 0) {
            g_source_remove(cecup.refresh_id);
            cecup.refresh_id = 0;
        }
        update_list_from_rows();

        cecup.preview_dirty = !message->preview_clean;
        protect_interface_from_user(false);

        if (DEBUGGING) {
            check_consistent_state();
        }

        break;
    case MSG_CLEAR_TREES:
        if (cecup.refresh_id != 0) {
            g_source_remove(cecup.refresh_id);
            cecup.refresh_id = 0;
        }
        g_mutex_lock(&cecup.arena_mutex);

        current_store_count = (int32)g_list_model_get_n_items(cecup.store);

        for (int32 i = 0; i < cecup.traversal_src.nfiles; i += 1) {
            cecup.traversal_src.row_ids[i] = -1;
        }
        for (int32 i = 0; i < cecup.traversal_dst.nfiles; i += 1) {
            cecup.traversal_dst.row_ids[i] = -1;
        }

        cecup.rows_len = 0;
        cecup.rows_visible_len = 0;

        cecup_list_model_update(CECUP_LIST_MODEL(cecup.store), current_store_count, 0);

        g_mutex_unlock(&cecup.arena_mutex);

        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(cecup.progress_bar), 0.0);
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(cecup.progress_bar), "");
        gtk_widget_set_tooltip_text(cecup.progress_bar, "");
        break;
    case MSG_ADD_ROW:
    case MSG_BATCH:
    case MSG_LAST:
    default:
        LOG("Ignoring %s.\n", MSG_str(message->type));
        break;
    }

    free_message(message);
    return needs_update;
}

static gboolean
update_ui_handler(void *data) {
    MessageBatch *batch;
    Message *message;
    bool needs_update;

    message = data;
    needs_update = false;

    if (message->type == MSG_BATCH) {
        batch = data;
        for (int32 i = 0; i < batch->count; i += 1) {
            if (update_ui_process_message(batch->messages[i])) {
                needs_update = true;
            }
        }
        free(batch, SIZEOF(*batch));
    } else {
        needs_update = update_ui_process_message(message);
    }

    if (needs_update) {
        update_list_from_rows();
    }

    return G_SOURCE_REMOVE;
}

static void
update_progress_bar(enum MsgType type, double fraction) {
    Message *message;
    static double last_fractions[4] = {0.0, 0.0, 0.0, 0.0};
    int32 index;

    if (type == MSG_PROGRESS) {
        index = 3;
    } else {
        index = 0;
    }

    if (fraction < 1.0) {
        if ((fraction - last_fractions[index]) < 0.001) {
            if ((fraction - last_fractions[index]) > -0.001) {
                return;
            }
        }
    }
    last_fractions[index] = fraction;

    message = xmalloc(SIZEOF(*message));
    memset64(message, 0, SIZEOF(*message));

    message->type = type;
    message->fraction = fraction;
    g_idle_add(update_ui_handler, message);
    return;
}

static void
update_progress_state(char *text, char *tooltip) {
    Message *message;

    message = xmalloc(SIZEOF(*message));
    memset64(message, 0, SIZEOF(*message));

    message->type = MSG_PROGRESS;
    message->fraction = -1.0;

    if (text) {
        message->text_len = strlen32(text);
        message->text = xmalloc(message->text_len + 1);
        memcpy64(message->text, text, message->text_len + 1);
    }

    if (tooltip) {
        message->path_len = strlen32(tooltip);
        message->src_path = xmalloc(message->path_len + 1);
        memcpy64(message->src_path, tooltip, message->path_len + 1);
    }

    g_idle_add(update_ui_handler, message);
    return;
}

#if 0 == TESTING_update
static inline void
update_functions_sink(void) {
    (void)update_progress_bar;
    (void)cecup_get_dirs;
    (void)get_target_tasks;
    (void)free_task_list;
    (void)free_message;
    return;
}
#endif

#if TESTING_update
#include <assert.h>
#include <string.h>

int
main(void) {
    (void)update_progress_bar;
    (void)cecup_get_dirs;
    (void)get_target_tasks;
    (void)free_task_list;
    (void)free_message;
    ASSERT(true);
    exit(EXIT_SUCCESS);
}

#endif

#endif /* UPDATE_C */
