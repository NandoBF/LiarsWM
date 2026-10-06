#pragma once
#include "command.h"
#include "keybind.h"
#include "server.h"
#include "workspace.h"
#include <unistd.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <wlr/util/log.h>

#define MODKEY WLR_MODIFIER_CTRL
#define BORDER_WIDTH 5
#define INNER_GAP 2
#define OUTER_GAP 5


static void spawn(struct liars_server* server, const union keybind_arg* arg) {
    if (fork() == 0) {
        execlp(arg->s, arg->s, (char*)NULL);
        _exit(1);
    }
}

static void switch_column(struct liars_server* server, const union keybind_arg* arg) {
    switch_to_column(server, arg->s);
}

static void close_view(struct liars_server* server, const union keybind_arg* arg) {
    struct wlr_surface* surface = server->seat->seat->keyboard_state.focused_surface;
    if (!surface) return;

    struct wlr_xdg_toplevel* toplevel = 
        wlr_xdg_toplevel_try_from_wlr_surface(surface);
    if (!toplevel) return;

    wlr_xdg_toplevel_send_close(toplevel);
}

static void toggle_command_mode(struct liars_server* server, const union keybind_arg* arg) {
    server->cmd.mode = server->cmd.mode == MODE_NORMAL ?  MODE_COMMAND :  MODE_COMMAND;
    wlr_log(WLR_DEBUG, "Switched mode.");
}


static const struct keybind keybinds[] = {
    { MODKEY,   XKB_KEY_Return, spawn, { .s = "kitty" } },
    { MODKEY|WLR_MODIFIER_ALT,   XKB_KEY_c, close_view, { 0 } },
    { MODKEY,   XKB_KEY_1,  switch_column, { .s = "1" } },
    { MODKEY,   XKB_KEY_2,  switch_column, { .s = "2" } },
    { MODKEY,   XKB_KEY_t,  toggle_command_mode, { 0 } },
};
