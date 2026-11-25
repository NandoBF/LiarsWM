#include "ipc/command.h"
#include "config.h"
#include "input/keybind.h"
#include "monitor.h"
#include "server.h"
#include "view/view.h"
#include "windows/workspace.h"
#include "wlr/util/log.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <xkbcommon/xkbcommon.h>

// NOTE: Command functions should start with the 'command' word

void command_spawn(struct liars_server* server, const char* command,
                   int client_fd) {
    wlr_log(WLR_INFO, "Spawning: %s", command);

    int err_pipe[2];
    if (pipe2(err_pipe, O_CLOEXEC) < 0) {
        wlr_log_errno(WLR_ERROR, "pipe2 failed for command: %s", command);
        return;
    }

    struct wlr_xdg_activation_token_v1* token =
        wlr_xdg_activation_token_v1_create(server->xdg_activation);
    const char* token_name = NULL;

    if (token) {
        token->data = server;
        token_name = wlr_xdg_activation_token_v1_get_name(token);
    }
    pid_t pid = fork();

    if (pid == 0) {
        close(err_pipe[0]);
        setsid();

        pid_t pid2 = fork();
        if (pid2 == 0) {
            sigset_t set;
            sigemptyset(&set);
            sigprocmask(SIG_SETMASK, &set, NULL);

            signal(SIGPIPE, SIG_DFL);

            int fd = open("/dev/null", O_RDWR);
            if (fd >= 0) {
                dup2(fd, STDIN_FILENO);
                dup2(fd, STDOUT_FILENO);
                dup2(fd, STDERR_FILENO);
                if (fd > STDERR_FILENO)
                    close(fd);
            }
            if (token_name) {
                setenv("XDG_ACTIVATION_TOKEN", token_name, 1);
            }

            execl("/bin/sh", "/bin/sh", "-c", command, (char*)NULL);

            int e = errno;
            (void)!write(err_pipe[1], &e, sizeof(e));
            _exit(1);
        }

        _exit(pid2 < 0 ? 1 : 0);

    } else if (pid > 0) {
        close(err_pipe[1]);
        waitpid(pid, NULL, 0);

        int e;
        ssize_t n = read(err_pipe[0], &e, sizeof(e));
        if (n == (ssize_t)sizeof(e)) {
            errno = e;
            wlr_log_errno(WLR_ERROR, "Failed to exec shell for command: %s",
                          command);
        }
        close(err_pipe[0]);

    } else {
        wlr_log_errno(WLR_ERROR, "Failed to fork first child for command: %s",
                      command);
        close(err_pipe[0]);
        close(err_pipe[1]);
    }
}

void command_to_column(struct liars_server* server, const char* command,
                       int client_fd) {
    switch_to_column(server, command);
}

void command_send_to_workspace(struct liars_server* server, const char* command,
                               int client_fd) {
    struct view* view = get_focused_view(server);
    if (!view)
        return;

    struct monitor* monitor;
    wl_list_for_each(monitor, &server->monitors, link) {
        struct column* col = column_get_or_create(server, monitor, command);
        if (col) {
            wlr_log(WLR_INFO, "Sending to workspace: %s", command);
            struct workspace* target_ws = column_get_or_create_level(col, 0);
            send_to_workspace(target_ws, view);
            return;
        }
    }
}

void command_close_focused(struct liars_server* server, const char* command,
                           int client_fd) {
    struct view* view = get_focused_view(server);
    if (view == NULL)
        return;
    view->impl->close(view);
}

void command_set_fullscreen(struct liars_server* server, const char* command,
                            int client_fd) {
    struct view* view = get_focused_view(server);
    if (view == NULL)
        return;
    view->impl->set_fullscreen(view, !view->is_fullscreen);
}

void command_set_floating(struct liars_server* server, const char* command,
                          int client_fd) {
    struct view* view = get_focused_view(server);
    if (view == NULL)
        return;
    view_set_floating(view, !view->is_floating);
}

void command_reload_config(struct liars_server* server, const char* command,
                           int client_fd) {
    struct config* new_conf = load_config(server->config->file_path);
    if (new_conf) {
        config_destroy(server->config);
        server->config = new_conf;
        config_do_execs(server, new_conf);
    }
}

void command_get_workspaces(struct liars_server* server, const char* command,
                            int client_fd) {
    struct column* col;
    wl_list_for_each(col, &server->columns, link) {
        size_t size = col->name_size + 15;
        char str[size];
        for (int i = 0; i < MAX_LEVELS; i++) {
            if (!col->levels[i])
                continue;
            int len = snprintf(str, size, "%s at level %d\n", col->name, i);
            if (len > 0 && client_fd != -1) {
                write(client_fd, str, len);
            }
        }
    }
}

void command_move_to_level(struct liars_server* server, const char* command,
                           int client_fd) {
    char* end;
    errno = 0;
    const int target_level = strtol(command, &end, 10);

    if (errno == ERANGE || !column_is_level_valid(target_level)) {
        wlr_log(WLR_ERROR, "Tried moving to out of range level!");
        return;
    }

    struct workspace* ws = get_active_workspace(server);
    struct column* col = ws->col;
    struct workspace* new = column_get_or_create_level(col, target_level);
    switch_to_workspace(server, new);
}

void command_move_to_level_offset(struct liars_server* server,
                                  const char* command, int client_fd) {
    char* end;
    errno = 0;
    const int offset = strtol(command, &end, 10);
    if (errno == ERANGE) {
        wlr_log(WLR_ERROR, "Invalid level offset!");
        return;
    }

    struct workspace* ws = get_active_workspace(server);
    struct workspace* new = column_get_offset_level(ws->col, offset);
    if (!new) {
        wlr_log(WLR_ERROR, "Invalid target level!");
        return;
    }
    switch_to_workspace(server, new);
}

void command_workspace_reparent(struct liars_server* server,
                                const char* command, int client_fd) {
    struct monitor* monitor;
    wl_list_for_each(monitor, &server->monitors, link) {
        struct column* col = column_get_or_create(server, monitor, command);
        if (col) {
            workspace_send_to_column(get_active_workspace(server), col);
        }
    }
}

void command_exit(struct liars_server* server, const char* command,
                  int client_fd) {
    wl_display_terminate(server->wl_display);
}

static const struct command_def commands[] = {
    {"exit", command_exit},
    {"spawn", command_spawn},
    {"workspace", command_to_column},
    {"close", command_close_focused},
    {"workspacesend", command_send_to_workspace},
    {"fullscreen", command_set_fullscreen},
    {"float", command_set_floating},
    {"reload", command_reload_config},
    {"all-workspaces", command_get_workspaces},
    {"level", command_move_to_level},
    {"level-offset", command_move_to_level_offset},
    {"workspace-reparent", command_workspace_reparent}};

const char* handle_command_input(struct liars_server* server, const char* cmd,
                                 int client_fd) {

    char cmd_copy[strlen(cmd) + 1];
    strcpy(cmd_copy, cmd);

    char* parsed_command_name = strtok(cmd_copy, " ");
    char* command_args = strtok(NULL, "");

    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
        if (!strcmp(parsed_command_name, commands[i].name)) {
            commands[i].execute(server, command_args, client_fd);
            break;
        }
    }
    return "?\n";
}

void execute_keybind(struct liars_server* server,
                     const struct liars_keybind* bind) {
    if (!bind || !bind->action)
        return;

    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
        if (!strcmp(bind->action, commands[i].name)) {
            commands[i].execute(
                server, bind->args,
                -1); // all writes need to check if the fd is -1!
            return;
        }
    }

    wlr_log(WLR_ERROR, "Keybind triggered unknown action: %s", bind->action);
}
