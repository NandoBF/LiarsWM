#pragma once
struct liars_server;

#define CMD_BUFFER_MAX 128

enum input_mode {
    MODE_NORMAL,
    MODE_COMMAND,
};

struct command_state {
    enum input_mode mode;
    char buffer[CMD_BUFFER_MAX];
    int len;
};

typedef void (*command_fn)(struct liars_server* server, const char* args);

struct command_def {
    const char* name;
    command_fn execute;
};

struct alias {
    const char* name;
    const char* command;
};


const char* handle_command_input(struct liars_server* server, const char* cmd);
