#include "layer_surface.h"
#include "input/input.h"
#include "monitor.h"
#include "server.h"
#include "windows/workspace.h"
#include "wlr-layer-shell-unstable-v1-protocol.h"
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wayland-util.h>

// TODO: Refactor popup structs ?

struct layer_surface_popup {
    struct wlr_xdg_popup* xdg_popup;
    struct layer_surface* parent_shell;
    struct wlr_scene_tree* scene_tree;

    struct wl_listener commit;
    struct wl_listener destroy;
    struct wl_listener reposition;
    struct wl_listener new_popup;
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
    struct wlr_layer_surface_v1* layer_surface = ls->wlr_layer_surface;

    if (!layer_surface->output) {
        return;
    }
    struct monitor* monitor = ls->monitor;
    monitor_arrange_layers(monitor);
}

static void layer_surface_map(struct wl_listener* listener, void* data) {
    struct layer_surface* ls = wl_container_of(listener, ls, map);
    struct wlr_layer_surface_v1* layer_surface = ls->wlr_layer_surface;

    // TODO: Prevent focus change when exclusive keyboard
    // Meaning, if the layer surface has exclusive surface, focus_view shouldn't
    // work
    if (layer_wants_exclusive_keyboard(layer_surface)) {
        struct wlr_seat* seat = ls->server->seat->seat;
        struct wlr_keyboard* keyboard = wlr_seat_get_keyboard(seat);
        if (keyboard) {
            wlr_seat_keyboard_notify_enter(
                seat, layer_surface->surface, keyboard->keycodes,
                keyboard->num_keycodes, &keyboard->modifiers);
        }
    }
}

static void layer_surface_popup_commit(struct wl_listener* listener,
                                       void* data) {
    struct layer_surface_popup* popup =
        wl_container_of(listener, popup, commit);
    if (!popup->xdg_popup->base->initial_commit)
        return;
    wlr_xdg_surface_schedule_configure(popup->xdg_popup->base);
}

static void layer_surface_popup_destroy(struct wl_listener* listener,
                                        void* data) {
    struct layer_surface_popup* popup =
        wl_container_of(listener, popup, destroy);

    wl_list_remove(&popup->commit.link);
    wl_list_remove(&popup->destroy.link);
    wl_list_remove(&popup->reposition.link);
    wl_list_remove(&popup->new_popup.link);

    free(popup);
    popup = NULL;
}

static void layer_surface_popup_reposition(struct wl_listener* listener,
                                           void* data) {}

static void layer_surface_new_popup(struct wl_listener* listener, void* data) {
    struct layer_surface* ls = wl_container_of(listener, ls, new_popup);
    struct wlr_xdg_popup* wlr_xdg_popup = data;

    struct layer_surface_popup* popup = calloc(1, sizeof(*popup));
    popup->parent_shell = ls;
    popup->xdg_popup = wlr_xdg_popup;
    wlr_xdg_popup->base->data = popup;
    wlr_xdg_popup->parent = ls->wlr_layer_surface->surface;

    struct wlr_scene_tree* parent_tree = ls->scene->tree;
    popup->scene_tree =
        wlr_scene_xdg_surface_create(parent_tree, wlr_xdg_popup->base);

    popup->commit.notify = layer_surface_popup_commit;
    wl_signal_add(&wlr_xdg_popup->base->surface->events.commit, &popup->commit);

    popup->destroy.notify = layer_surface_popup_destroy;
    wl_signal_add(&wlr_xdg_popup->events.destroy, &popup->destroy);

    popup->reposition.notify = layer_surface_popup_reposition;
    wl_signal_add(&wlr_xdg_popup->events.reposition, &popup->reposition);

    popup->new_popup.notify = layer_surface_new_popup;
    wl_signal_add(&wlr_xdg_popup->base->events.new_popup, &popup->new_popup);
}

static void layer_surface_unmap(struct wl_listener* listener, void* data) {
    struct layer_surface* ls = wl_container_of(listener, ls, unmap);

    if (layer_has_exclusive_keyboard(ls->wlr_layer_surface)) {
        struct wlr_seat* seat = ls->server->seat->seat;
        if (wlr_seat_focused_surface(seat) == ls->wlr_layer_surface->surface)
            wlr_seat_keyboard_notify_clear_focus(seat);
    }
}

static void layer_surface_destroy(struct wl_listener* listener, void* data) {
    struct layer_surface* ls = wl_container_of(listener, ls, destroy);

    wl_list_remove(&ls->map.link);
    wl_list_remove(&ls->unmap.link);
    wl_list_remove(&ls->commit.link);
    wl_list_remove(&ls->destroy.link);
    wl_list_remove(&ls->new_popup.link);
    wl_list_remove(&ls->link);

    monitor_arrange_layers(ls->monitor);
    free(ls);
    ls = NULL;
}

void server_new_layer_shell_surface(struct wl_listener* listener, void* data) {
    struct liars_server* server =
        wl_container_of(listener, server, new_layer_shell_surface);
    struct wlr_layer_surface_v1* wlr_layer_surface = data;
    struct layer_surface* ls = calloc(1, sizeof(*ls));
    ls->server = server;
    ls->wlr_layer_surface = wlr_layer_surface;

    struct wlr_output* output = wlr_layer_surface->output;
    if (output && output->data) {
        ls->monitor = output->data;
    } else {
        ls->monitor = get_active_workspace(server)->col->monitor;
        wlr_layer_surface->output = ls->monitor->output;
    }
    wl_list_insert(&ls->monitor->layer_surfaces, &ls->link);

    ls->scene = wlr_scene_layer_surface_v1_create(&server->scene->tree,
                                                  wlr_layer_surface);
    ls->popups_tree = wlr_scene_tree_create(&server->scene->tree);

    ls->commit.notify = layer_surface_commit;
    wl_signal_add(&wlr_layer_surface->surface->events.commit, &ls->commit);
    ls->map.notify = layer_surface_map;
    wl_signal_add(&wlr_layer_surface->surface->events.map, &ls->map);

    ls->unmap.notify = layer_surface_unmap;
    wl_signal_add(&wlr_layer_surface->surface->events.unmap, &ls->unmap);

    ls->destroy.notify = layer_surface_destroy;
    wl_signal_add(&wlr_layer_surface->events.destroy, &ls->destroy);

    ls->new_popup.notify = layer_surface_new_popup;
    wl_signal_add(&wlr_layer_surface->events.new_popup, &ls->new_popup);
}
