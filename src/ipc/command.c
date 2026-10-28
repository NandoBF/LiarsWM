#include "ipc/command.h"
#include "input/input.h"
#include "input/keybind.h"
#include "server.h"
#include "view/view.h"
#include "windows/workspace.h"
#include "wlr/util/log.h"
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <wayland-util.h>
#include <xkbcommon/xkbcommon.h>

void spawn_command(struct liars_server* server, const char* command) {
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

void workspace_command(struct liars_server* server, const char* command) {
    switch_to_column(server, command);
}

void send_to_workspace_command(struct liars_server* server,
                               const char* command) {
    struct view* view = get_focused_view(server);
    if (!view)
        return;

    struct monitor* monitor;
    wl_list_for_each(monitor, &server->monitors, link) {
        struct column* col = column_get_or_create(server, monitor, command);
        if (col) {
            wlr_log(WLR_INFO, "Sending to workspace: %s", command);
            struct workspace* target_ws =
                wl_container_of(col->levels.next, target_ws, link);
            send_to_workspace(target_ws, view);
            focus_view_under_cursor(server);
            return;
        }
    }
}

void close_focused(struct liars_server* server, const char* command) {
    struct view* view = get_focused_view(server);
    if (view == NULL)
        return;
    view->impl->close(view);
}

void fullscreen(struct liars_server* server, const char* command) {
    struct view* view = get_focused_view(server);
    view->impl->set_fullscreen(view, !view->is_fullscreen);
}

static const struct command_def commands[] = {
    {"spawn", spawn_command},
    {"workspace", workspace_command},
    {"close", close_focused},
    {"workspacesend", send_to_workspace_command},
    {"fullscreen", fullscreen}};

const char* handle_command_input(struct liars_server* server, const char* cmd) {

    char cmd_copy[strlen(cmd) + 1];
    strcpy(cmd_copy, cmd);

    char* parsed_command_name = strtok(cmd_copy, " ");
    char* command_args = strtok(NULL, "");

    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
        if (!strcmp(parsed_command_name, commands[i].name)) {
            commands[i].execute(server, command_args);
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
            commands[i].execute(server, bind->args);
            return;
        }
    }

    wlr_log(WLR_ERROR, "Keybind triggered unknown action: %s", bind->action);
}
