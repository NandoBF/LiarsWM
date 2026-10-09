#include "command.h"
#include "input.h"
#include "keybind.h"
#include "server.h"
#include "view.h"
#include "wlr/util/log.h"
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon.h>

void spawn(struct liars_server* server, const char* command) {
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

void close_focused(struct liars_server* server, const char* command) {
    struct wlr_surface* focused = seat_focused_surface(server->seat);
    if (!focused)
        return;

    struct wlr_xdg_toplevel* toplevel =
        wlr_xdg_toplevel_try_from_wlr_surface(focused);
    if (!toplevel)
        return;

    wlr_xdg_toplevel_send_close(toplevel);
}

static const struct command_def commands[] = {{"spawn", spawn},
                                              {"workspace", workspace_command},
                                              {"close", close_focused}};

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
