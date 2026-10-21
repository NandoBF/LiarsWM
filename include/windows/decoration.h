#pragma once
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>

struct liars_decoration {
    struct wlr_xdg_toplevel_decoration_v1* wlr_decoration;
    struct wl_listener request_mode;
    struct wl_listener destroy;
    struct wl_listener surface_commit;
};
void new_view_decoration(struct wl_listener* listener, void* data);
