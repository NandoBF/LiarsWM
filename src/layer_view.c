#include "layer_shell.h"
#include "server.h"
#include "wlr-layer-shell-unstable-v1-protocol.h"
#include "wlr/util/log.h"
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wayland-util.h>

struct layer_surface {
    struct wl_list link;
    struct wlr_layer_surface_v1* layer_surface;
    struct wlr_scene_layer_surface_v1* scene;
    struct liars_server* server;

    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener destroy;
    struct wl_listener commit;
};

static bool layer_wants_exclusive_keyboard(struct wlr_layer_surface_v1* layer) {
    bool t = layer->pending.keyboard_interactive ==
             ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE;
    return t;
}

static bool layer_has_exclusive_keyboard(struct wlr_layer_surface_v1* layer) {
    bool t = layer->current.keyboard_interactive ==
             ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE;
    return t;
}

static void layer_surface_commit(struct wl_listener* listener, void* data) {
    struct layer_surface* ls = wl_container_of(listener, ls, commit);
    struct wlr_layer_surface_v1* layer_surface = ls->layer_surface;

    if (!layer_surface->output) {
        return;
    }

    struct wlr_box output_box;
    wlr_output_layout_get_box(ls->server->output_layout, layer_surface->output,
                              &output_box);

    struct wlr_box usable = output_box;
    wlr_scene_layer_surface_v1_configure(ls->scene, &output_box, &usable);
}

static void layer_surface_map(struct wl_listener* listener, void* data) {
    struct layer_surface* ls = wl_container_of(listener, ls, map);
    struct wlr_layer_surface_v1* layer_surface = ls->layer_surface;

    if (layer_wants_exclusive_keyboard(layer_surface)) {
        struct wlr_seat* seat = ls->server->seat->seat;
        struct wlr_keyboard* keyboard = wlr_seat_get_keyboard(seat);
        if (keyboard) {
            wlr_seat_keyboard_notify_enter(
                seat, layer_surface->surface, keyboard->keycodes,
                keyboard->num_keycodes, &keyboard->modifiers);
        }
    }

    wlr_log(WLR_DEBUG, "Layer surface mapped");
}

static void layer_surface_unmap(struct wl_listener* listener, void* data) {
    struct layer_surface* ls = wl_container_of(listener, ls, unmap);

    if (layer_has_exclusive_keyboard(ls->layer_surface)) {
        struct wlr_seat* seat = ls->server->seat->seat;
        if (seat->keyboard_state.focused_surface == ls->layer_surface->surface)
            wlr_seat_keyboard_notify_clear_focus(seat);
    }
}

static void layer_surface_destroy(struct wl_listener* listener, void* data) {
    struct layer_surface* ls = wl_container_of(listener, ls, destroy);

    wl_list_remove(&ls->map.link);
    wl_list_remove(&ls->unmap.link);
    wl_list_remove(&ls->commit.link);
    wl_list_remove(&ls->destroy.link);

    free(ls);
}

void server_new_layer_shell_surface(struct wl_listener* listener, void* data) {
    struct liars_server* server =
        wl_container_of(listener, server, new_layer_shell_surface);
    struct wlr_layer_surface_v1* layer_surface = data;
    struct layer_surface* ls = calloc(1, sizeof(*ls));
    ls->server = server;
    ls->layer_surface = layer_surface;

    struct wlr_output* output = layer_surface->output;

    if (!output) {
        struct monitor* monitor =
            wl_container_of(server->monitors.next, monitor, link);
        layer_surface->output = monitor->output;
    }

    ls->scene =
        wlr_scene_layer_surface_v1_create(&server->scene->tree, layer_surface);

    ls->commit.notify = layer_surface_commit;
    wl_signal_add(&layer_surface->surface->events.commit, &ls->commit);
    ls->map.notify = layer_surface_map;
    wl_signal_add(&layer_surface->surface->events.map, &ls->map);

    ls->unmap.notify = layer_surface_unmap;
    wl_signal_add(&layer_surface->surface->events.unmap, &ls->unmap);

    ls->destroy.notify = layer_surface_destroy;
    wl_signal_add(&layer_surface->events.destroy, &ls->destroy);

    wlr_log(WLR_DEBUG, "New layer shell surface");
}
