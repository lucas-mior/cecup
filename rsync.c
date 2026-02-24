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

static char *
rsync_errors(int32 status) {
    char *message;
    switch (status) {
    case 0:
        message = "Success";
        break;
    case 1:
        message = "Syntax or usage error";
        break;
    case 2:
        message = "Protocol incompatibility";
        break;
    case 3:
        message = "Errors selecting input/output files, dirs";
        break;
    case 4:
        message = "Requested action not supported";
        break;
    case 5:
        message = "Error starting client-server protocol";
        break;
    case 6:
        message = "Daemon unable to append to log-file";
        break;
    case 10:
        message = "Error in socket I/O";
        break;
    case 11:
        message = "Error in file I/O";
        break;
    case 12:
        message = "Error in rsync protocol data stream";
        break;
    case 13:
        message = "Errors with program diagnostics";
        break;
    case 14:
        message = "Error in IPC code";
        break;
    case 20:
        message = "Received SIGUSR1 or SIGINT";
        break;
    case 21:
        message = "Some error returned by waitpid()";
        break;
    case 22:
        message = "Error allocating core memory buffers";
        break;
    case 23:
        message = "Partial transfer due to error";
        break;
    case 24:
        message = "Partial transfer due to vanished source files";
        break;
    case 25:
        message = "The --max-delete limit stopped deletions";
        break;
    case 30:
        message = "Timeout in data send/receive";
        break;
    case 35:
        message = "Timeout waiting for daemon connection";
        break;
    default:
        message = "Invalid rsync exit code";
        break;
    }
    return message;
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
    char *full_dst;
    UIUpdateData *remove_data;

    ud = (UIUpdateData *)user_data;
    path_src = (char *)gtk_entry_get_text(GTK_ENTRY(ud->widgets->src_entry));
    path_dst = (char *)gtk_entry_get_text(GTK_ENTRY(ud->widgets->dst_entry));

    if (g_strcmp0(ud->action, "Delete") == 0) {
        /* If the file was removed from source,
         * we must delete it from destination */
        full_dst = g_build_filename(path_dst, ud->filepath, NULL);
        snprintf(cmd, sizeof(cmd), "rm -rfv '%s' 2>&1", full_dst);
        g_free(full_dst);
    } else {
        /* Standard update/new sync */
        snprintf(cmd, sizeof(cmd),
                 "rsync --verbose --update --recursive --links --hard-links "
                 "--perms --times --owner --group --include='%s' --exclude='*' "
                 "'%s/' '%s/' 2>&1",
                 ud->filepath, path_src, path_dst);
    }

    snprintf(log_msg, sizeof(log_msg), "+ %s", cmd);
    dispatch_log(ud->widgets, log_msg);

    if ((rsync_pipe = popen(cmd, "r")) == NULL) {
        error("Error opening rsync pipe: %s.\n", strerror(errno));
        return NULL;
    }

    {
        int32 rsync_exit;
        errno = 0;
        while (fgets(buffer, sizeof(buffer), rsync_pipe)) {
            buffer[strcspn(buffer, "\n")] = 0;
            dispatch_log(ud->widgets, buffer);
        }
        if (errno) {
            error("Error reading line from rsync pipe: %s.\n", strerror(errno));
            return NULL;
        }
        if ((rsync_exit = pclose(rsync_pipe)) < 0) {
            error("Error closing rsync pipe: %s.\n", strerror(errno));
            return NULL;
        }
        if (rsync_exit) {
            error("Error in rsync: %s.\n", rsync_errors(rsync_exit));
            return NULL;
        }
    }

    dispatch_log(ud->widgets,
                 ">>> Single file operation finished. Updating list...");

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
             " --itemize-changes --perms --times --owner --group"
             " --delete --delete-after --delete-excluded"
             " --stats %s %s %s '%s/' '%s/' 2>&1",
             thread_data->is_preview ? "--dry-run" : "--info=progress2",
             exclude_flag,
             thread_data->is_preview ? "" : "| tee /tmp/rsyncfiles",
             thread_data->src_path, thread_data->dst_path);

    g_free(exclude_flag);

    snprintf(log_msg, sizeof(log_msg), "+ %s", cmd);
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
                        dispatch_tree(
                            thread_data->widgets, 1, "Delete", rel_path, sz,
                            "File does not exist in source directory");
                    }
                } else if (strncmp(buffer, ">f", 2) == 0
                           || strncmp(buffer, ">c", 2) == 0
                           || strncmp(buffer, "hf", 2) == 0) {
                    char *space;
                    space = strchr(buffer, ' ');
                    if (space) {
                        char *act;
                        char full_path[2048];
                        struct stat st_file;
                        int64 sz;
                        char *reason;

                        if (strncmp(buffer, "hf", 2) == 0) {
                            act = "Hardlink";
                            reason = "File is a hardlink to another file";
                        } else if (strncmp(buffer, ">f+++++", 7) == 0) {
                            act = "New";
                            reason = "New file created in source directory";
                        } else {
                            act = "Update";
                            reason = "File changed in source directory";
                        }
                        snprintf(full_path, sizeof(full_path), "%s/%s",
                                 thread_data->src_path, space + 1);
                        sz = (stat(full_path, &st_file) == 0) ? st_file.st_size
                                                              : 0;
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

        snprintf(log_msg, sizeof(log_msg), "+ %s", cmd);
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

static char *
find_terminal(void) {
    char *env_term;
    char *path;

    env_term = getenv("TERMINAL");
    if (env_term && strlen64(env_term) > 0) {
        path = g_find_program_in_path(env_term);
        if (path) {
            g_free(path);
            return env_term;
        }
    }

    path = g_find_program_in_path("xterm");
    if (path) {
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
    char log_msg[5120];
    char *path_src;
    char *path_dst;
    char *diff_tool;
    char *terminal;

    ud = (UIUpdateData *)user_data;
    path_src = (char *)gtk_entry_get_text(GTK_ENTRY(ud->widgets->src_entry));
    path_dst = (char *)gtk_entry_get_text(GTK_ENTRY(ud->widgets->dst_entry));
    diff_tool = (char *)gtk_entry_get_text(GTK_ENTRY(ud->widgets->diff_entry));
    terminal = (char *)gtk_entry_get_text(GTK_ENTRY(ud->widgets->term_entry));

    if (!terminal || strlen(terminal) == 0) {
        dispatch_log(ud->widgets,
                     "ERROR: No terminal specified in the Terminal input box.");
        g_free(ud->filepath);
        g_free(ud->action);
        g_free(ud);
        return NULL;
    }

    snprintf(cmd, sizeof(cmd), "%s '%s/%s' '%s/%s'", diff_tool, path_src,
             ud->filepath, path_dst, ud->filepath);

    snprintf(term_cmd, sizeof(term_cmd),
             "%s -e bash -c \"%s; echo; echo "
             "'-----------------------'; read -n 1 -s -r -p 'Press any key to "
             "close...'\" &",
             terminal, cmd);

    snprintf(log_msg, sizeof(log_msg), ">>> Launching (%s): %s", terminal, cmd);
    dispatch_log(ud->widgets, log_msg);

    system(term_cmd);

    g_free(ud->filepath);
    g_free(ud->action);
    g_free(ud);
    return NULL;
}
