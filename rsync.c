#include <gtk/gtk.h>
#include <ctype.h>
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

static gpointer
sync_worker(gpointer user_data) {
    ThreadData *thread_data;
    char cmd[4096];
    char buffer[2048];
    FILE *rsync_pipe;
    char *exclude_flag;
    struct stat st_src;
    struct stat st_dst;
    char log_msg[5120];

    thread_data = (ThreadData *)user_data;

    if (thread_data->check_different_fs) {
        if (stat(thread_data->src_path, &st_src) == 0
            && stat(thread_data->dst_path, &st_dst) == 0) {
            if (st_src.st_dev == st_dst.st_dev) {
                dispatch_log(thread_data->widgets,
                             "ERROR: Source and Destination are on the same "
                             "filesystem.");
                UIUpdateData *ready;
                ready = g_new0(UIUpdateData, 1);
                ready->widgets = thread_data->widgets;
                ready->type = DATA_TYPE_ENABLE_BUTTONS;
                g_idle_add(update_ui_handler, ready);
                g_free(thread_data);
                return NULL;
            }
        }
    }

    if (thread_data->is_preview) {
        UIUpdateData *clear;
        clear = g_new0(UIUpdateData, 1);
        clear->widgets = thread_data->widgets;
        clear->type = DATA_TYPE_CLEAR_TREES;
        g_idle_add(update_ui_handler, clear);
    }

    exclude_flag = (access(thread_data->widgets->exclude_path, F_OK) != -1)
                       ? g_strdup_printf("--exclude-from='%s'",
                                         thread_data->widgets->exclude_path)
                       : g_strdup("");

    snprintf(cmd, sizeof(cmd),
             "rsync --verbose --update --recursive --partial"
             " --links --hard-links"
             " --itemize-changes --perms --times --owner --group --delete"
             " --delete-after"
             " --delete-excluded --stats %s %s %s '%s/' '%s/' 2>&1",
             thread_data->is_preview ? "--dry-run" : "--info=progress2",
             exclude_flag,
             thread_data->is_preview ? "" : "| tee /tmp/rsyncfiles",
             thread_data->src_path, thread_data->dst_path);

    g_free(exclude_flag);

    snprintf(log_msg, sizeof(log_msg), "RUNNING: %s", cmd);
    dispatch_log(thread_data->widgets, log_msg);

    rsync_pipe = popen(cmd, "r");
    if (!rsync_pipe) {
        dispatch_log(thread_data->widgets,
                     "ERROR: Failed to start rsync process.");
    } else {
        while (fgets(buffer, sizeof(buffer), rsync_pipe)) {
            buffer[strcspn(buffer, "\n")] = 0;
            if (thread_data->is_preview) {
                if (strncmp(buffer, "*deleting", 9) == 0) {
                    char full_dst_path[2048];
                    char full_src_path[2048];
                    struct stat st_dst_file;
                    char *rel_path;
                    int64 sz;

                    rel_path = buffer + 9;
                    while (isspace((unsigned char)*rel_path)) {
                        rel_path++;
                    }
                    snprintf(full_dst_path, sizeof(full_dst_path), "%s/%s",
                             thread_data->dst_path, rel_path);
                    sz = (stat(full_dst_path, &st_dst_file) == 0)
                             ? st_dst_file.st_size
                             : 0;
                    snprintf(full_src_path, sizeof(full_src_path), "%s/%s",
                             thread_data->src_path, rel_path);

                    if (access(full_src_path, F_OK) == 0) {
                        dispatch_tree(thread_data->widgets, 1, "Delete",
                                      rel_path, sz,
                                      "File is excluded by configuration");
                    } else {
                        dispatch_tree(thread_data->widgets, 1, "Delete",
                                      rel_path, sz,
                                      "File removed from source directory");
                    }
                } else if (strncmp(buffer, ">f", 2) == 0
                           || strncmp(buffer, ">c", 2) == 0) {
                    char *space;
                    space = strchr(buffer, ' ');
                    if (space) {
                        char *act;
                        char full_path[2048];
                        struct stat st_file;
                        int64 sz;
                        char *reason;

                        act = (strncmp(buffer, ">f+++++", 7) == 0) ? "New"
                                                                   : "Update";
                        snprintf(full_path, sizeof(full_path), "%s/%s",
                                 thread_data->src_path, space + 1);
                        sz = (stat(full_path, &st_file) == 0) ? st_file.st_size
                                                              : 0;
                        reason = (strncmp(buffer, ">f+++++", 7) == 0)
                                     ? "New file in source directory"
                                     : "File updated in source directory";
                        dispatch_tree(thread_data->widgets, 0, act, space + 1,
                                      sz, reason);
                    }
                }
            } else {
                dispatch_log(thread_data->widgets, buffer);
            }
        }
        pclose(rsync_pipe);
    }

    if (!thread_data->is_preview) {
        dispatch_log(thread_data->widgets,
                     ">>> Starting Checksum Verification...");
        system("sed -nE '/^[>ch]f/{s/^[^ ]+ //; p}' /tmp/rsyncfiles > "
               "/tmp/sync.files");
        snprintf(cmd, sizeof(cmd),
                 "rsync --verbose --checksum --files-from=/tmp/sync.files "
                 "'%s/' '%s/' 2>&1",
                 thread_data->src_path, thread_data->dst_path);

        snprintf(log_msg, sizeof(log_msg), "RUNNING: %s", cmd);
        dispatch_log(thread_data->widgets, log_msg);

        rsync_pipe = popen(cmd, "r");
        if (!rsync_pipe) {
            dispatch_log(thread_data->widgets,
                         "ERROR: Failed to start checksum verification.");
        } else {
            while (fgets(buffer, sizeof(buffer), rsync_pipe)) {
                buffer[strcspn(buffer, "\n")] = 0;
                dispatch_log(thread_data->widgets, buffer);
            }
            pclose(rsync_pipe);
        }
    }

    dispatch_log(thread_data->widgets, ">>> Finished.");
    UIUpdateData *ready;
    ready = g_new0(UIUpdateData, 1);
    ready->widgets = thread_data->widgets;
    ready->type = DATA_TYPE_ENABLE_BUTTONS;
    g_idle_add(update_ui_handler, ready);
    g_free(thread_data);
    return NULL;
}
