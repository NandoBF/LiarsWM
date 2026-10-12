#include "decoration.h"
#include <stdlib.h>

static void decoration_handle_request_mode(struct wl_listener* listener,
                                           void* data) {
    struct liars_decoration* deco =
        wl_container_of(listener, deco, request_mode);
    if (deco->wlr_decoration->toplevel->base->initialized) {
        wlr_xdg_toplevel_decoration_v1_set_mode(
            deco->wlr_decoration,
            WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    }
}

static void decoration_handle_surface_commit(struct wl_listener* listener,
                                             void* data) {
    struct liars_decoration* deco =
        wl_container_of(listener, deco, surface_commit);
    struct wlr_xdg_surface* surface = deco->wlr_decoration->toplevel->base;

    if (surface->initial_commit) {
        wlr_xdg_toplevel_decoration_v1_set_mode(
            deco->wlr_decoration,
            WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
        wl_list_remove(&deco->surface_commit.link);
        wl_list_init(&deco->surface_commit.link);
    }
}

static void decoration_handle_destroy(struct wl_listener* listener,
                                      void* data) {
    struct liars_decoration* deco = wl_container_of(listener, deco, destroy);
    wl_list_remove(&deco->request_mode.link);
    wl_list_remove(&deco->destroy.link);
    if (!wl_list_empty(&deco->surface_commit.link)) {
        wl_list_remove(&deco->surface_commit.link);
    }
    free(deco);
}

void new_view_decoration(struct wl_listener* listener, void* data) {
    struct wlr_xdg_toplevel_decoration_v1* decoration = data;

    struct liars_decoration* deco = calloc(1, sizeof(*deco));
    deco->wlr_decoration = decoration;
    wl_list_init(&deco->surface_commit.link);

    deco->request_mode.notify = decoration_handle_request_mode;
    wl_signal_add(&decoration->events.request_mode, &deco->request_mode);

    deco->destroy.notify = decoration_handle_destroy;
    wl_signal_add(&decoration->events.destroy, &deco->destroy);

    if (decoration->toplevel->base->initialized) {
        wlr_xdg_toplevel_decoration_v1_set_mode(
            decoration, WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    } else {
        deco->surface_commit.notify = decoration_handle_surface_commit;
        wl_signal_add(&decoration->toplevel->base->surface->events.commit,
                      &deco->surface_commit);
    }
}
