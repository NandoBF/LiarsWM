#include "ipc/command.h"
#include "config.h"
#include "input/input.h"
#include "input/keybind.h"
#include "server.h"
#include "view/view.h"
#include "windows/workspace.h"
#include "wlr/util/log.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-util.h>
#include <xkbcommon/xkbcommon.h>

// TODO: Standardize command functions name

void spawn_command(struct liars_server* server, const char* command,
                   int client_fd) {
    wlr_log(WLR_INFO, "Spawning: %s", command);
    if (fork() == 0) {
        setsid();

        if (fork() == 0) {
            sigset_t set;
            sigemptyset(&set);
            sigprocmask(SIG_SETMASK, &set, NULL);

            int fd = open("/dev/null", O_RDWR);
            if (fd >= 0) {
                dup2(fd, STDIN_FILENO);
                dup2(fd, STDOUT_FILENO);
                dup2(fd, STDERR_FILENO);
                close(fd);
            }

            execl("/bin/sh", "/bin/sh", "-c", command, (char*)NULL);

            wlr_log(WLR_ERROR, "Failed to exec shell for command: %s", command);
            _exit(1);
        }
        _exit(0);
    }
}

void workspace_command(struct liars_server* server, const char* command,
                       int client_fd) {
    switch_to_column(server, command);
}

void send_to_workspace_command(struct liars_server* server, const char* command,
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
            focus_view_under_cursor(server);
            return;
        }
    }
}

void close_focused(struct liars_server* server, const char* command,
                   int client_fd) {
    struct view* view = get_focused_view(server);
    if (view == NULL)
        return;
    view->impl->close(view);
}

void fullscreen(struct liars_server* server, const char* command,
                int client_fd) {
    struct view* view = get_focused_view(server);
    if (view == NULL)
        return;
    view->impl->set_fullscreen(view, !view->is_fullscreen);
}

void floating(struct liars_server* server, const char* command, int client_fd) {
    struct view* view = get_focused_view(server);
    if (view == NULL)
        return;
    view_set_floating(view, !view->is_floating);
}

void reload_config(struct liars_server* server, const char* command,
                   int client_fd) {
    struct config* new_conf = load_config("./config/example.toml");
    if (new_conf) {
        free(server->config);
        server->config = new_conf;
    }
}

void get_workspaces(struct liars_server* server, const char* command,
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
    wlr_log(WLR_INFO, "%s", command);
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

static const struct command_def commands[] = {
    {"spawn", spawn_command},
    {"workspace", workspace_command},
    {"close", close_focused},
    {"workspacesend", send_to_workspace_command},
    {"fullscreen", fullscreen},
    {"float", floating},
    {"reload", reload_config},
    {"all-workspaces", get_workspaces},
    {"level", command_move_to_level}};

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
