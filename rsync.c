#include <gtk/gtk.h>
#include <ctype.h>
#include <sys/wait.h>
#include <dirent.h>
#include "util.c"
#include "cecup.h"

static void
dispatch_log(AppWidgets *w, char *msg) {
    UIUpdateData *data;

    data = g_new0(UIUpdateData, 1);
    data->widgets = w;
    data->type = DATA_TYPE_LOG;
    data->message = g_strdup(msg);
    g_idle_add(update_ui_handler, data);
    return;
}

static void
dispatch_tree(AppWidgets *w, int32 side, char *act, char *path, int64 size,
              char *reason) {
    UIUpdateData *data;

    data = g_new0(UIUpdateData, 1);
    data->widgets = w;
    data->type = DATA_TYPE_TREE_ROW;
    data->side = side;
    data->action = g_strdup(act);
    data->filepath = g_strdup(path);
    data->reason = g_strdup(reason);
    data->size = size;
    g_idle_add(update_ui_handler, data);
    return;
}

static void
find_equal_files(AppWidgets *w, char *src_base, char *dst_base,
                 char *relative_path) {
    DIR *dir;
    struct dirent *entry;
    char *full_src;

    full_src = g_build_filename(src_base, relative_path, NULL);
    if (!(dir = opendir(full_src))) {
        g_free(full_src);
        return;
    }

    while ((entry = readdir(dir))) {
        char *name;
        char *sub_rel;
        char *s_path;
        char *d_path;
        struct stat st_s;
        struct stat st_d;

        name = entry->d_name;
        if (g_strcmp0(name, ".") == 0 || g_strcmp0(name, "..") == 0) {
            continue;
        }

        sub_rel = g_build_filename(relative_path, name, NULL);
        s_path = g_build_filename(src_base, sub_rel, NULL);
        d_path = g_build_filename(dst_base, sub_rel, NULL);

        if (stat(s_path, &st_s) == 0 && stat(d_path, &st_d) == 0) {
            if (S_ISDIR(st_s.st_mode)) {
                find_equal_files(w, src_base, dst_base, sub_rel);
            } else if (S_ISREG(st_s.st_mode)) {
                if (st_s.st_size == st_d.st_size
                    && st_s.st_mtime == st_d.st_mtime) {
                    dispatch_tree(w, 0, "Equal", sub_rel, st_s.st_size,
                                  "Identical");
                }
            }
        }
        g_free(sub_rel);
        g_free(s_path);
        g_free(d_path);
    }
    closedir(dir);
    g_free(full_src);
    return;
}

static gpointer
bulk_sync_worker(gpointer user_data) {
    GList *tasks;

    tasks = (GList *)user_data;

    for (GList *l = tasks; l != NULL; l = l->next) {
        UIUpdateData *ud;
        char cmd[4096];
        char buffer[2048];
        FILE *rsync_pipe;

        ud = (UIUpdateData *)l->data;

        if (g_strcmp0(ud->action, "Delete") == 0) {
            char *full_dst;

            full_dst = g_build_filename(ud->dst_base, ud->filepath, NULL);
            snprintf(cmd, sizeof(cmd), "rm -rfv '%s' 2>&1", full_dst);
            g_free(full_dst);
        } else {
            snprintf(cmd, sizeof(cmd),
                     "rsync --verbose --update --recursive "
                     "--partial --progress --info=progress2 "
                     "--links --hard-links --itemize-changes "
                     "--perms --times --owner --group "
                     "--include='%s' --include='%s/**' --exclude='*' '%s/' "
                     "'%s/' 2>&1",
                     ud->filepath, ud->filepath, ud->src_base, ud->dst_base);
        }

        dispatch_log(ud->widgets, cmd);
        if ((rsync_pipe = popen(cmd, "r")) != NULL) {
            while (fgets(buffer, sizeof(buffer), rsync_pipe)) {
                buffer[strcspn(buffer, "\n")] = 0;
                dispatch_log(ud->widgets, buffer);
            }
            pclose(rsync_pipe);
        }

        UIUpdateData *remove_data;

        remove_data = g_new0(UIUpdateData, 1);
        remove_data->widgets = ud->widgets;
        remove_data->type = DATA_TYPE_REMOVE_TREE_ROW;
        remove_data->filepath = g_strdup(ud->filepath);
        g_idle_add(update_ui_handler, remove_data);

        g_free(ud->filepath);
        g_free(ud->action);
        g_free(ud->src_base);
        g_free(ud->dst_base);
        g_free(ud);
    }
    g_list_free(tasks);
    return NULL;
}

static gpointer
sync_worker(gpointer user_data) {
    ThreadData *thread_data;
    char cmd[4096];
    char log_cmd[8192];
    char buffer[2048];
    FILE *pipe;
    char *ex;
    int32 i;
    int32 j;
    int32 line_len;
    int32 last_space_index;
    int32 word_len;

    thread_data = (ThreadData *)user_data;

    if (thread_data->is_preview) {
        UIUpdateData *clear;

        clear = g_new0(UIUpdateData, 1);
        clear->widgets = thread_data->widgets;
        clear->type = DATA_TYPE_CLEAR_TREES;
        g_idle_add(update_ui_handler, clear);
    }

    ex = (access(thread_data->widgets->exclude_path, F_OK) != -1)
             ? g_strdup_printf("--exclude-from='%s'",
                               thread_data->widgets->exclude_path)
             : g_strdup("");

    snprintf(cmd, sizeof(cmd),
             "rsync --verbose --update --recursive"
             " --partial --progress --info=progress2"
             " --links --hard-links --itemize-changes"
             " --perms --times --owner --group"
             " --delete-excluded %s %s "
             "'%s/' '%s/' 2>&1",
             thread_data->is_preview ? "--dry-run" : "", ex,
             thread_data->src_path, thread_data->dst_path);
    g_free(ex);

    i = 0;
    j = 0;
    line_len = 0;
    last_space_index = -1;

    while (cmd[i] != '\0') {
        log_cmd[j] = cmd[i];
        if (cmd[i] == ' ') {
            last_space_index = j;
        }
        line_len += 1;
        if (line_len > 120 && last_space_index != -1) {
            log_cmd[last_space_index] = '\n';
            word_len = j - last_space_index;
            for (int32 k = 0; k < word_len; k += 1) {
                log_cmd[j + 4 - k] = log_cmd[j - k];
            }
            log_cmd[last_space_index + 1] = ' ';
            log_cmd[last_space_index + 2] = ' ';
            log_cmd[last_space_index + 3] = ' ';
            log_cmd[last_space_index + 4] = ' ';
            j += 4;
            line_len = word_len + 4;
            last_space_index = -1;
        }
        i += 1;
        j += 1;
    }
    log_cmd[j] = '\0';

    dispatch_log(thread_data->widgets, log_cmd);

    if ((pipe = popen(cmd, "r")) != NULL) {
        while (fgets(buffer, sizeof(buffer), pipe)) {
            buffer[strcspn(buffer, "\n")] = 0;
            if (thread_data->is_preview) {
                if (strncmp(buffer, "*deleting", 9) == 0) {
                    char *relative_path;
                    char *full_src;
                    char *full_dst;
                    struct stat st_s;
                    struct stat st_d;
                    int64 sz;
                    char *reason;

                    relative_path = buffer + 10;
                    while (isspace(*relative_path)) {
                        relative_path++;
                    }

                    full_src = g_build_filename(thread_data->src_path,
                                                relative_path, NULL);
                    full_dst = g_build_filename(thread_data->dst_path,
                                                relative_path, NULL);
                    sz = (stat(full_dst, &st_d) == 0) ? st_d.st_size : 0;
                    reason = (stat(full_src, &st_s) == 0)
                                 ? "Excluded by pattern"
                                 : "Missing in source";

                    dispatch_tree(thread_data->widgets, 1, "Delete",
                                  relative_path, sz, reason);
                    g_free(full_src);
                    g_free(full_dst);
                } else if (strchr(buffer, ' ')
                           && (buffer[0] == '>' || buffer[0] == '.'
                               || buffer[0] == 'h' || buffer[0] == 'c')) {
                    char *relative_path;
                    char *act;
                    char *full_src;
                    struct stat st;
                    int64 sz;

                    relative_path = strchr(buffer, ' ') + 1;
                    act = "Update";

                    if (strncmp(buffer, "hf", 2) == 0) {
                        act = "Hardlink";
                    } else if (strncmp(buffer, "cd", 2) == 0
                               || strncmp(buffer, ">f+++++", 7) == 0) {
                        act = "New";
                    }

                    full_src = g_build_filename(thread_data->src_path,
                                                relative_path, NULL);
                    sz = (stat(full_src, &st) == 0) ? st.st_size : 0;
                    dispatch_tree(thread_data->widgets, 0, act, relative_path,
                                  sz, act);
                    g_free(full_src);
                }
            } else {
                dispatch_log(thread_data->widgets, buffer);
            }
        }
        pclose(pipe);
    }

    if (thread_data->is_preview && thread_data->show_equal) {
        find_equal_files(thread_data->widgets, thread_data->src_path,
                         thread_data->dst_path, "");
    }

    UIUpdateData *ready;

    ready = g_new0(UIUpdateData, 1);
    ready->widgets = thread_data->widgets;
    ready->type = DATA_TYPE_ENABLE_BUTTONS;
    g_idle_add(update_ui_handler, ready);
    g_free(thread_data);
    return NULL;
}

static gpointer
diff_worker(gpointer user_data) {
    UIUpdateData *ud;
    char cmd[8192];

    ud = (UIUpdateData *)user_data;
    snprintf(cmd, sizeof(cmd),
             "%s -e bash -c \"%s '%s/%s' '%s/%s'; read -p 'Press Enter...'\" &",
             ud->term_cmd, ud->diff_tool, ud->src_base, ud->filepath,
             ud->dst_base, ud->filepath);
    system(cmd);

    g_free(ud->filepath);
    g_free(ud->action);
    g_free(ud->src_base);
    g_free(ud->dst_base);
    g_free(ud->term_cmd);
    g_free(ud->diff_tool);
    g_free(ud);
    return NULL;
}
