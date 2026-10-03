#include "command.h"
#include "server.h"
#include "wlr/util/log.h"
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon.h>


void spawn(struct liars_server* server, const char* command) {
    wlr_log(WLR_INFO, "Spawning: %s", command);
    if (fork() == 0) {
        setsid();

        sigset_t set;
        sigemptyset(&set);
        sigprocmask(SIG_SETMASK, &set, NULL);

        execl("/bin/sh", "/bin/sh", "-c", command, (char*)NULL);
        
        wlr_log(WLR_ERROR, "Failed to exec shell for command: %s", command);
        _exit(1);
    }
}

static const struct command_def commands[] = {
    { "spawn", spawn }
};

const char* handle_command_input(struct liars_server* server, const char* cmd) {

    char cmd_copy[strlen(cmd) + 1]; 
    strcpy(cmd_copy, cmd);

    char* parsed_command_name = strtok(cmd_copy, " ");
    char* command_args = strtok(NULL, "");


    for(size_t i = 0; i < sizeof(commands)/sizeof(commands[0]); i++) {
        if (!strcmp(parsed_command_name, commands[i].name)) {
            commands[i].execute(server, command_args);
            break;
        }
    }
    return "?\n";
}



