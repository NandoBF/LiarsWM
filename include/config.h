#pragma once
#include "keybind.h"
#include "server.h"
#include "workspace.h"
#include <unistd.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#define MODKEY WLR_MODIFIER_CTRL


static const char* terminal[] = { "kitty", NULL };


static void spawn(struct liars_server* server, const union keybind_arg* arg) {
    if (fork() == 0) {
        execlp(arg->s, arg->s, (char*)NULL);
        _exit(1);
    }
}

static void switch_column(struct liars_server* server, const union keybind_arg* arg) {
    struct monitor* monitor;
    struct column* col;
    wl_list_for_each(monitor, &server->monitors, link) {
         col = column_get_or_create(server, monitor, arg->s); 
         if (col) {
            server->active_workspace = wl_container_of(col->levels.next, (struct workspace*)NULL, link);
            return;
         }
    }
}

static void close_view(struct liars_server* server, const union keybind_arg* arg) {
    struct wlr_surface* surface = server->seat->seat->keyboard_state.focused_surface;
    if (!surface) return;

    struct wlr_xdg_toplevel* toplevel = 
        wlr_xdg_toplevel_try_from_wlr_surface(surface);
    if (!toplevel) return;

    wlr_xdg_toplevel_send_close(toplevel);
}


static const struct keybind keybinds[] = {
    { MODKEY,   XKB_KEY_Return, spawn, { .s = "kitty" } },
    { MODKEY|WLR_MODIFIER_ALT,   XKB_KEY_q, close_view, { 0 } },
    { MODKEY,   XKB_KEY_1,  switch_column, { .s = "1" } },
    { MODKEY,   XKB_KEY_2,  switch_column, { .s = "2" } },
};
