#if !defined ITEM_C
#define ITEM_C

#include "cecup.h"

static char *
item_path_get(CecupItem *item) {
    if (item->src_idx >= 0) {
        return cecup.traversal_src.paths[item->src_idx];
    } else if (item->dst_idx >= 0) {
        return cecup.traversal_dst.paths[item->dst_idx];
    } else {
        error2("Error: src_path and dst_path are NULL.\n");
        fatal(EXIT_FAILURE);
    }
}

static int32
item_path_len_get(CecupItem *item) {
    if (item->src_idx >= 0) {
        return cecup.traversal_src.paths_lens[item->src_idx];
    }
    if (item->dst_idx >= 0) {
        return cecup.traversal_dst.paths_lens[item->dst_idx];
    }
    return 0;
}

static char *
item_path_side(CecupItem *item, int32 side) {
    if (side == L) {
        if (item->src_idx >= 0) {
            return cecup.traversal_src.paths[item->src_idx];
        }
    } else {
        if (item->dst_idx >= 0) {
            return cecup.traversal_dst.paths[item->dst_idx];
        }
    }
    return NULL;
}

static int64
item_size_side(CecupItem *item, int32 side) {
    if (side == L) {
        if (item->src_idx >= 0) {
            if (!S_ISDIR(cecup.traversal_src.stats[item->src_idx].st_mode)) {
                return cecup.traversal_src.stats[item->src_idx].st_size;
            }
        }
    } else {
        if (item->dst_idx >= 0) {
            if (!S_ISDIR(cecup.traversal_dst.stats[item->dst_idx].st_mode)) {
                return cecup.traversal_dst.stats[item->dst_idx].st_size;
            }
        }
    }
    return -1;
}

static int64
item_mtime_side(CecupItem *item, int32 side) {
    if (side == L) {
        if (item->src_idx >= 0) {
            return cecup.traversal_src.stats[item->src_idx].st_mtime;
        }
    } else {
        if (item->dst_idx >= 0) {
            return cecup.traversal_dst.stats[item->dst_idx].st_mtime;
        }
    }
    return 0;
}

static char *
item_ignore_pattern_side(CecupItem *item, int32 side) {
    if (side == L) {
        if (item->src_idx >= 0) {
            return cecup.traversal_src.matched_patterns[item->src_idx];
        }
    } else {
        if (item->dst_idx >= 0) {
            return cecup.traversal_dst.matched_patterns[item->dst_idx];
        }
    }
    return NULL;
}

static int32
item_path_len_side(CecupItem *item, int32 side) {
    if (side == L) {
        if (item->src_idx >= 0) {
            return (int32)cecup.traversal_src.paths_lens[item->src_idx];
        }
    } else {
        if (item->dst_idx >= 0) {
            return (int32)cecup.traversal_dst.paths_lens[item->dst_idx];
        }
    }
    return 0;
}

static char *
item_link_target_side(CecupItem *item, int32 side) {
    if (side == L) {
        if (item->src_idx >= 0) {
            return cecup.traversal_src.link_targets[item->src_idx];
        }
    } else {
        if (item->dst_idx >= 0) {
            return cecup.traversal_dst.link_targets[item->dst_idx];
        }
    }
    return NULL;
}

static int32
item_link_target_len_side(CecupItem *item, int32 side) {
    if (side == L) {
        if (item->src_idx >= 0) {
            return (int32)cecup.traversal_src.link_targets_lens[item->src_idx];
        }
    } else {
        if (item->dst_idx >= 0) {
            return (int32)cecup.traversal_dst.link_targets_lens[item->dst_idx];
        }
    }
    return 0;
}

static void
item_get_actions_reasons(CecupItem *item, enum Action *action_src,
                         enum Action *action_dst, enum Reason *reason) {
    int32 src_idx = item->src_idx;
    int32 dst_idx = item->dst_idx;
    bool delete_excluded
        = gtk_check_button_get_active(GTK_CHECK_BUTTON(cecup.delete_excluded));

    *reason = 0;

    if ((cecup.traversal_src.stats == NULL)
         || (cecup.traversal_dst.stats == NULL)) {
        error("Function %s called while the traversal stats array is null.",
              __func__);
        error("This probably means that there is some race condition.\n");
        error("Or another bug.\n");
        fatal(EXIT_FAILURE);
    }

    if (src_idx < 0) {
        char *matched_dst;

        matched_dst = cecup.traversal_dst.matched_patterns[dst_idx];
        *reason |= REASON_MISSING;

        if (matched_dst) {
            *reason |= REASON_IGNORED;
        }

        *action_src = ACTION_IGNORE;

        if (delete_excluded) {
            *action_dst = ACTION_DELETE;
        } else {
            *action_dst = ACTION_IGNORE;
        }

        return;
    }

    if (dst_idx < 0) {
        char *matched_src = cecup.traversal_src.matched_patterns[src_idx];
        struct stat *stat_src = &cecup.traversal_src.stats[src_idx];
        bool is_symlink = S_ISLNK(stat_src->st_mode);
        bool is_hardlink = S_ISREG(stat_src->st_mode)
                           && cecup.traversal_src.link_targets[src_idx];

        if (matched_src) {
            *action_src = ACTION_IGNORE;
            *action_dst = ACTION_IGNORE;
            *reason |= REASON_IGNORED;
            return;
        }

        *reason |= REASON_NEW;

        if (is_hardlink) {
            *action_src = ACTION_HARDLINK;
            *action_dst = ACTION_HARDLINK;
            *reason |= REASON_HARDLINK;
        } else if (is_symlink) {
            *action_src = ACTION_SYMLINK;
            *action_dst = ACTION_SYMLINK;
            *reason |= REASON_SYMLINK;
        } else {
            *action_src = ACTION_NEW;
            *action_dst = ACTION_NEW;
        }

        return;
    }

    {
        char *matched_src = cecup.traversal_src.matched_patterns[src_idx];
        struct stat *stat_src = &cecup.traversal_src.stats[src_idx];
        struct stat *stat_dst = &cecup.traversal_dst.stats[dst_idx];
        char *path_src = cecup.traversal_src.paths[src_idx];
        char *target_src = cecup.traversal_src.link_targets[src_idx];
        char *target_dst = cecup.traversal_dst.link_targets[dst_idx];
        int32 nlinks_src = cecup.traversal_src.nlinks[src_idx];
        int32 nlinks_dst = cecup.traversal_dst.nlinks[dst_idx];
        bool is_symlink = S_ISLNK(stat_src->st_mode);
        bool is_hardlink = cecup.traversal_src.nlinks[src_idx] > 1;
        bool is_dir = S_ISDIR(stat_src->st_mode);
        bool equal = false;
        bool attributes_differ = false;
        bool delete_after
            = gtk_check_button_get_active(GTK_CHECK_BUTTON(cecup.delete_after));

        if (matched_src) {
            *action_src = ACTION_IGNORE;
            *reason |= REASON_IGNORED;

            if (delete_excluded) {
                *action_dst = ACTION_DELETE;
            } else {
                *action_dst = ACTION_IGNORE;
            }

            return;
        }

        if (is_symlink) {
            *reason |= REASON_SYMLINK;

            if (S_ISLNK(stat_dst->st_mode)) {
                if (target_src) {
                    if (target_dst) {
                        if (strcmp(target_src, target_dst) == 0) {
                            equal = true;
                        }
                    }
                }
            }
        } else {
            if (is_hardlink) {
                *reason |= REASON_HARDLINK;
            }

            if (!is_dir) {
                if (stat_src->st_size != stat_dst->st_size) {
                    *reason |= REASON_SIZE;
                    attributes_differ = true;
                }
            }

            if (stat_src->st_mtime > stat_dst->st_mtime) {
                *reason |= REASON_MTIME_NEWER;
                attributes_differ = true;
            }

            if (stat_src->st_mtime < stat_dst->st_mtime) {
                *reason |= REASON_MTIME_OLDER;
                if (delete_after) {
                    attributes_differ = true;
                } else {
                    *action_src = ACTION_IGNORE;
                    *action_dst = ACTION_IGNORE;
                    return;
                }
            }

            if (is_dir) {
                if (stat_src->st_ctime > stat_dst->st_ctime) {
                    *reason |= REASON_CTIME;
                    attributes_differ = true;
                }
            }

            if (stat_src->st_uid != stat_dst->st_uid) {
                *reason |= REASON_OWNER;
                attributes_differ = true;
            }

            if (stat_src->st_gid != stat_dst->st_gid) {
                *reason |= REASON_GROUP;
                attributes_differ = true;
            }

            if ((stat_src->st_mode & 07777) != (stat_dst->st_mode & 07777)) {
                *reason |= REASON_PERM;
                attributes_differ = true;
            }

            if (is_hardlink) {
                if (!S_ISREG(stat_dst->st_mode)) {
                    equal = false;
                    *reason |= REASON_HARDLINK_NOT_REGULAR;
                    attributes_differ = true;
                } else if (target_dst == NULL) {
                    equal = false;
                    attributes_differ = true;
                    *reason |= REASON_HARDLINK_MISSING_LINK;
                } else if (
                        strcmp(target_src, target_dst)
                        && strcmp(path_src, target_dst)
                        && (nlinks_src != nlinks_dst)
                        ) {
                    equal = false;
                    attributes_differ = true;
                    *reason |= REASON_HARDLINK_NOT_MATCH;
                }
            }

            if (!attributes_differ) {
                equal = true;
            }
        }

        if (equal) {
            *action_src = ACTION_EQUAL;
            *action_dst = ACTION_EQUAL;
            *reason |= REASON_EQUAL;
        } else {
            if (is_hardlink) {
                *action_src = ACTION_HARDLINK;
                *action_dst = ACTION_HARDLINK;
            } else if (is_symlink) {
                *action_src = ACTION_SYMLINK;
                *action_dst = ACTION_SYMLINK;
            } else {
                *action_src = ACTION_UPDATE;
                *action_dst = ACTION_UPDATE;
            }
        }
    }

    return;
}

#endif
