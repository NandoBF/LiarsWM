#pragma once
#include "wlr/util/box.h"
#include <wayland-server-core.h>
#include <wayland-util.h>

struct liars_server;

struct monitor {
    struct wl_list link;
    struct liars_server* server;
    struct wlr_output* output;
    struct wlr_scene_output* scene_output;
    struct wl_listener frame;
    struct wl_listener request_state;
    struct wl_listener destroy;

    struct wlr_box box;
    struct wlr_box usable;
    struct wl_list layer_surfaces;
};

struct monitor* monitor_create(struct liars_server* server,
                               struct wlr_output* output);
void monitor_arrange_layers(struct monitor* monitor);

void monitor_set_scale(struct monitor* monitor, float scale);
