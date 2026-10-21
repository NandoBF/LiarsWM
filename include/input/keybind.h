#pragma once
#include <stdint.h>
#include <xkbcommon/xkbcommon.h>

struct liars_server;

union keybind_arg {
    int i;
    unsigned int ui;
    float f;
    const char* s;
};

typedef void (*keybind_fn)(struct liars_server* server,
                           const union keybind_arg* arg);

struct keybind {
    uint32_t modifiers;
    xkb_keysym_t keysym;
    keybind_fn action;
    union keybind_arg arg;
};

struct liars_keybind {
    uint32_t modifiers;
    xkb_keysym_t keysym;
    const char* action;
    const char* args;
};
