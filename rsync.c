#include <gtk/gtk.h>
#include <ctype.h>
#include <sys/wait.h>
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
single_sync_worker(gpointer user_data) {
    UIUpdateData *ud;
    char cmd[4096];
    char buffer[2048];
    FILE *rsync_pipe;
    char log_msg[5120];
    char *path_src;
    char *path_dst;
    UIUpdateData *remove_data;
    int32 status;

    ud = (UIUpdateData *)user_data;
    path_src = (char *)gtk_entry_get_text(GTK_ENTRY(ud->widgets->src_entry));
    path_dst = (char *)gtk_entry_get_text(GTK_ENTRY(ud->widgets->dst_entry));

    if (g_strcmp0(ud->action, "Delete") == 0) {
        char *full_dst;

        full_dst = g_build_filename(path_dst, ud->filepath, NULL);
        snprintf(cmd, sizeof(cmd), "rm -rfv '%s' 2>&1", full_dst);
        g_free(full_dst);
    } else {
        snprintf(cmd, sizeof(cmd),
                 "rsync -av --update --hard-links --include='%s' "
                 "--include='%s/**' --exclude='*' '%s/' '%s/' 2>&1",
                 ud->filepath, ud->filepath, path_src, path_dst);
    }

    snprintf(log_msg, sizeof(log_msg), "+ %s", cmd);
    dispatch_log(ud->widgets, log_msg);

    if ((rsync_pipe = popen(cmd, "r")) == NULL) {
        dispatch_log(ud->widgets,
                     "ERROR: Failed to open pipe for single sync.");
    } else {
        while (fgets(buffer, sizeof(buffer), rsync_pipe)) {
            buffer[strcspn(buffer, "\n")] = 0;
            dispatch_log(ud->widgets, buffer);
        }
        if ((status = pclose(rsync_pipe)) != 0) {
            snprintf(log_msg, sizeof(log_msg),
                     "ERROR: Command failed with exit code %d",
                     WEXITSTATUS(status));
            dispatch_log(ud->widgets, log_msg);
        }
    }

    remove_data = g_new0(UIUpdateData, 1);
    remove_data->widgets = ud->widgets;
    remove_data->type = DATA_TYPE_REMOVE_TREE_ROW;
    remove_data->filepath = g_strdup(ud->filepath);
    g_idle_add(update_ui_handler, remove_data);

    g_free(ud->filepath);
    g_free(ud->action);
    g_free(ud);
    return NULL;
}

static gpointer
sync_worker(gpointer user_data) {
    ThreadData *thread_data;
    char cmd[4096];
    char buffer[2048];
    FILE *rsync_pipe;
    char *exclude_flag;
    char log_msg[5120];
    int32 status;
    UIUpdateData *ready;

    thread_data = (ThreadData *)user_data;

    if (thread_data->check_different_fs) {
        struct stat st_src;
        struct stat st_dst;

        if (stat(thread_data->src_path, &st_src) == 0
            && stat(thread_data->dst_path, &st_dst) == 0) {
            if (st_src.st_dev == st_dst.st_dev) {
                UIUpdateData *fail_ready;

                dispatch_log(thread_data->widgets, "ERROR: Same filesystem.");
                fail_ready = g_new0(UIUpdateData, 1);
                fail_ready->widgets = thread_data->widgets;
                fail_ready->type = DATA_TYPE_ENABLE_BUTTONS;
                g_idle_add(update_ui_handler, fail_ready);
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

    snprintf(
        cmd, sizeof(cmd),
        "rsync --verbose --update --recursive --partial --links --hard-links "
        "--itemize-changes --perms --times --owner --group --delete "
        "--delete-after --delete-excluded --stats %s %s %s '%s/' '%s/' 2>&1",
        thread_data->is_preview ? "--dry-run" : "--info=progress2",
        exclude_flag, thread_data->is_preview ? "" : "| tee /tmp/rsyncfiles",
        thread_data->src_path, thread_data->dst_path);

    g_free(exclude_flag);
    snprintf(log_msg, sizeof(log_msg), "+ %s", cmd);
    dispatch_log(thread_data->widgets, log_msg);

    if ((rsync_pipe = popen(cmd, "r")) == NULL) {
        dispatch_log(thread_data->widgets, "ERROR: Failed to open rsync pipe.");
    } else {
        while (fgets(buffer, sizeof(buffer), rsync_pipe)) {
            buffer[strcspn(buffer, "\n")] = 0;
            if (thread_data->is_preview) {
                if (strncmp(buffer, "*deleting", 9) == 0) {
                    char full_src_path[2048];
                    char full_dst_path[2048];
                    struct stat st_src_check;
                    struct stat st_dst_file;
                    char *rel_path;
                    char *reason;
                    int64 sz;

                    rel_path = buffer + 9;
                    while (isspace((unsigned char)*rel_path)) {
                        rel_path++;
                    }

                    snprintf(full_src_path, sizeof(full_src_path), "%s/%s",
                             thread_data->src_path, rel_path);

                    if (stat(full_src_path, &st_src_check) == 0) {
                        reason = "Excluded by pattern";
                    } else {
                        reason = "Missing in source";
                    }

                    snprintf(full_dst_path, sizeof(full_dst_path), "%s/%s",
                             thread_data->dst_path, rel_path);
                    sz = (stat(full_dst_path, &st_dst_file) == 0)
                             ? st_dst_file.st_size
                             : 0;
                    dispatch_tree(thread_data->widgets, 1, "Delete", rel_path,
                                  sz, reason);
                } else if (strncmp(buffer, ">f", 2) == 0
                           || strncmp(buffer, ">c", 2) == 0
                           || strncmp(buffer, "hf", 2) == 0
                           || strncmp(buffer, "cd", 2) == 0
                           || strncmp(buffer, ".d", 2) == 0) {
                    char *space;

                    if ((space = strchr(buffer, ' ')) != NULL) {
                        char *act;
                        char *reason;
                        struct stat st_file;
                        char full_src_path[2048];
                        int64 sz;

                        if (strncmp(buffer, "hf", 2) == 0) {
                            act = "Hardlink";
                            reason = "Hardlink";
                        } else if (strncmp(buffer, "cd", 2) == 0) {
                            act = "New";
                            reason = "New directory";
                        } else if (strncmp(buffer, ".d", 2) == 0) {
                            act = "Update";
                            reason = "Attributes changed";
                        } else if (strncmp(buffer, ">f+++++", 7) == 0) {
                            act = "New";
                            reason = "New file";
                        } else {
                            act = "Update";
                            reason = "File changed";
                        }

                        snprintf(full_src_path, sizeof(full_src_path), "%s/%s",
                                 thread_data->src_path, space + 1);
                        sz = (stat(full_src_path, &st_file) == 0)
                                 ? st_file.st_size
                                 : 0;
                        dispatch_tree(thread_data->widgets, 0, act, space + 1,
                                      sz, reason);
                    }
                }
            } else {
                dispatch_log(thread_data->widgets, buffer);
            }
        }
        if ((status = pclose(rsync_pipe)) != 0) {
            snprintf(log_msg, sizeof(log_msg),
                     "ERROR: rsync failed with exit code %d",
                     WEXITSTATUS(status));
            dispatch_log(thread_data->widgets, log_msg);
        }
    }

    ready = g_new0(UIUpdateData, 1);
    ready->widgets = thread_data->widgets;
    ready->type = DATA_TYPE_ENABLE_BUTTONS;
    g_idle_add(update_ui_handler, ready);
    g_free(thread_data);
    return NULL;
}

static char *
find_terminal(void) {
    char *env_term;
    char *path;

    if ((env_term = getenv("TERMINAL")) != NULL && strlen64(env_term) > 0) {
        if ((path = g_find_program_in_path(env_term)) != NULL) {
            g_free(path);
            return env_term;
        }
    }
    if ((path = g_find_program_in_path("xterm")) != NULL) {
        g_free(path);
        return "xterm";
    }
    return NULL;
}

static gpointer
diff_worker(gpointer user_data) {
    UIUpdateData *ud;
    char cmd[4096];
    char term_cmd[8192];
    char *path_src;
    char *path_dst;
    char *diff_tool;
    char *terminal;

    ud = (UIUpdateData *)user_data;
    path_src = (char *)gtk_entry_get_text(GTK_ENTRY(ud->widgets->src_entry));
    path_dst = (char *)gtk_entry_get_text(GTK_ENTRY(ud->widgets->dst_entry));
    diff_tool = (char *)gtk_entry_get_text(GTK_ENTRY(ud->widgets->diff_entry));
    terminal = (char *)gtk_entry_get_text(GTK_ENTRY(ud->widgets->term_entry));

    snprintf(cmd, sizeof(cmd), "%s '%s/%s' '%s/%s'", diff_tool, path_src,
             ud->filepath, path_dst, ud->filepath);
    snprintf(term_cmd, sizeof(term_cmd),
             "%s -e bash -c \"%s; echo; echo '---'; read -n 1 -s -r -p "
             "'Press any key...'\" &",
             terminal, cmd);
    system(term_cmd);

    g_free(ud->filepath);
    g_free(ud->action);
    g_free(ud);
    return NULL;
}
