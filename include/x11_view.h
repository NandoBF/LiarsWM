#pragma once
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/xwayland/xwayland.h>
#include "view.h"


struct x11_view {
    struct view base;
    struct wlr_xwayland_surface* xsurface;

    struct wl_listener request_configure;
    struct wl_listener associate;
    struct wl_listener dissociate;
    struct wl_listener grab_focus;
};

void server_new_xwayland_surface(struct wl_listener* listener, void* data); 

