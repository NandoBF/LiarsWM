#pragma once
#include <wayland-server-core.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_scene.h>

// TODO: cleanup
struct layer_surface {
    struct wl_list link;
    struct wlr_layer_surface_v1* wlr_layer_surface; // data -> NULL
    struct wlr_scene_layer_surface_v1* scene;
    struct liars_server* server;
    struct monitor* monitor;

    uint32_t current_exclusive_zone;

    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener destroy;
    struct wl_listener commit;
    struct wl_listener new_popup;
};

void server_new_layer_shell_surface(struct wl_listener* listener, void* data);
