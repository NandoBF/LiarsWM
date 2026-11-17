#pragma once
#include "input/keybind.h"
#include "server.h"
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#define MODKEY WLR_MODIFIER_CTRL

struct config {
    char* file_path;
    int border_width;
    int inner_gap;
    int outer_gap;

    struct liars_keybind* keybinds;
    int keybind_num;

    char** execs;
    int execs_count;

    char** execs_once;
    int execs_once_count;

    int interactive_modifier;

    time_t last_modified_at;
};

struct config* load_config(const char* config_path);
void config_do_execs(struct liars_server* server, struct config* c);
void config_do_execs_once(struct liars_server* server, struct config* c);
void config_destroy(struct config* config);
