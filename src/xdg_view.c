#include "xdg_view.h"
#include "config.h"
#include "input.h"
#include "layout.h"
#include "server.h"
#include "view.h"
#include "wlr/util/edges.h"
#include "wlr/util/log.h"
#include "workspace.h"
#include <stddef.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/types/wlr_xdg_shell.h>

#define XDG_VIEW(ptr) ((struct xdg_view*)(ptr))

struct xdg_view {
    struct view base;
    struct wlr_xdg_toplevel* toplevel;
    struct wlr_scene_tree* surface_tree;
    uint32_t pending_configure_serial;
};

struct xdg_popup {
    struct wlr_xdg_popup* xdg_popup;
    struct wl_listener commit;
    struct wl_listener destroy;
};

static void xdg_view_set_size(struct view* v, int w, int h) {
    struct xdg_view* xv = XDG_VIEW(v);

    if (v->borders[0]) {
        view_set_borders(v, w, h);
    }
    struct config* config = v->server->config;

    int view_width = w - (2 * config->border_width);
    int view_height = h - (2 * config->border_width);

    if (view_width <= 0)
        view_width = 1;
    if (view_height <= 0)
        view_height = 1;

    xv->pending_configure_serial =
        wlr_xdg_toplevel_set_size(xv->toplevel, view_width, view_height);
}

static void xdg_toplevel_map(struct wl_listener* listener, void* data) {
    struct view* view = wl_container_of(listener, view, map);

    view_map(view);
}

static void xdg_toplevel_unmap(struct wl_listener* listener, void* data) {
    struct view* view = wl_container_of(listener, view, unmap);

    if (view == view->server->cursor->grabbed_view) {
        reset_cursor_mode(view->server->cursor);
    }
    struct wlr_seat* seat = view->server->seat->seat;
    if (wlr_seat_focused_surface(seat) == view->impl->get_surface(view)) {
        wlr_seat_keyboard_clear_focus(seat);
    }

    wl_list_remove(&view->link);
    workspace_arrange(view->workspace);
}

static void xdg_toplevel_commit(struct wl_listener* listener, void* data) {
    struct view* view = wl_container_of(listener, view, commit);
    struct xdg_view* xv = XDG_VIEW(view);

    if (xv->toplevel->base->initial_commit) {
        wlr_xdg_toplevel_set_size(xv->toplevel, 0, 0);
    }
}

static void xdg_toplevel_destroy(struct wl_listener* listener, void* data) {
    struct view* view = wl_container_of(listener, view, destroy);

    wl_list_remove(&view->map.link);
    wl_list_remove(&view->unmap.link);
    view_destroy(view);

    focus_view_under_cursor(view->server);
    free(view);
}

static void xdg_toplevel_request_move(struct wl_listener* listener,
                                      void* data) {
    struct view* view = wl_container_of(listener, view, request_move);
    // TODO: Empty? Perhaps ignoring is the correct thing
}

static void xdg_toplevel_request_resize(struct wl_listener* listener,
                                        void* data) {

    struct view* view = wl_container_of(listener, view, request_resize);
    struct wlr_xdg_toplevel_resize_event* event = data;
}

static void xdg_toplevel_request_maximize(struct wl_listener* listener,
                                          void* data) {
    struct view* view = wl_container_of(listener, view, request_maximize);
    struct xdg_view* xdg_view = XDG_VIEW(view);
    if (xdg_view->toplevel->base->initialized) {
        wlr_xdg_surface_schedule_configure(xdg_view->toplevel->base);
    }
}

static void xdg_toplevel_request_fullscreen(struct wl_listener* listener,
                                            void* data) {
    struct view* view = wl_container_of(listener, view, request_fullscreen);
    struct xdg_view* xdg_view = XDG_VIEW(view);
    if (xdg_view->toplevel->base->initialized) {
        wlr_xdg_surface_schedule_configure(xdg_view->toplevel->base);
    }
}

// IMPL

static void xdg_view_get_geometry(struct view* v, struct wlr_box* box) {
    struct xdg_view* xv = XDG_VIEW(v);
    *box = xv->toplevel->base->geometry;
}

static void xdg_view_set_activated(struct view* v, bool activated) {
    struct xdg_view* xv = XDG_VIEW(v);
    wlr_xdg_toplevel_set_activated(xv->toplevel, activated);
}

static void xdg_view_close(struct view* v) {
    struct xdg_view* xv = XDG_VIEW(v);
    wlr_xdg_toplevel_send_close(xv->toplevel);
}

static const char* xdg_view_get_title(struct view* v) {
    struct xdg_view* xv = XDG_VIEW(v);
    return xv->toplevel->title;
}

static const char* xdg_view_get_app_id(struct view* v) {
    struct xdg_view* xv = XDG_VIEW(v);
    return xv->toplevel->app_id;
}

static bool xdg_view_want_fullscreen(struct view* v) {
    struct xdg_view* xv = XDG_VIEW(v);
    return xv->toplevel->requested.fullscreen == true;
}

static struct wlr_surface* xdg_view_get_surface(struct view* v) {
    struct xdg_view* xv = XDG_VIEW(v);
    return xv->toplevel->base->surface;
}

// TODO: Stop arrange on fullscreen windows
static void xdg_view_set_fullscreen(struct view* v, bool fullscreen) {
    struct xdg_view* xdg_view = XDG_VIEW(v);

    v->is_fullscreen = fullscreen;
    wlr_xdg_toplevel_set_fullscreen(xdg_view->toplevel, fullscreen);
    if (fullscreen) {
        wlr_scene_node_raise_to_top(&v->scene_tree->node);
        struct monitor* monitor = v->workspace->col->monitor;
        xdg_view->base.impl->set_size(v, monitor->output->width,
                                      monitor->output->height);
        wlr_scene_node_set_position(&v->scene_tree->node, 0, 0);
    } else {
        workspace_arrange(v->workspace);
    }

    view_borders_set_enabled(v, !fullscreen);
    wlr_scene_node_reparent(&v->scene_tree->node, v->workspace->tree);
}

static const struct view_impl xdg_view_impl = {
    .get_geometry = xdg_view_get_geometry,
    .set_size = xdg_view_set_size,
    .set_activated = xdg_view_set_activated,
    .close = xdg_view_close,
    .get_title = xdg_view_get_title,
    .get_app_id = xdg_view_get_app_id,
    .want_fullscreen = xdg_view_want_fullscreen,
    .get_surface = xdg_view_get_surface,
    .set_fullscreen = xdg_view_set_fullscreen,
};

//

void server_new_xdg_toplevel(struct wl_listener* listener, void* data) {
    struct wlr_xdg_toplevel* toplevel = data;
    struct liars_server* server =
        wl_container_of(listener, server, new_xdg_toplevel);
    struct config* config = server->config;

    struct xdg_view* xv = calloc(1, sizeof(*xv));
    wl_list_init(&xv->base.link);
    xv->toplevel = toplevel;
    xv->base.impl = &xdg_view_impl;
    xv->base.surface = toplevel->base->surface;
    xv->base.server = server;

    xv->base.scene_tree = wlr_scene_tree_create(&server->scene->tree);
    xv->base.scene_tree->node.data = &xv->base;

    xv->surface_tree =
        wlr_scene_xdg_surface_create(xv->base.scene_tree, xv->toplevel->base);
    toplevel->base->data = xv->surface_tree;
    wlr_scene_node_set_position(&xv->surface_tree->node, config->border_width,
                                config->border_width);

    xv->base.map.notify = xdg_toplevel_map;
    wl_signal_add(&toplevel->base->surface->events.map, &xv->base.map);

    xv->base.unmap.notify = xdg_toplevel_unmap;
    wl_signal_add(&toplevel->base->surface->events.unmap, &xv->base.unmap);

    xv->base.commit.notify = xdg_toplevel_commit;
    wl_signal_add(&toplevel->base->surface->events.commit, &xv->base.commit);

    xv->base.destroy.notify = xdg_toplevel_destroy;
    wl_signal_add(&toplevel->events.destroy, &xv->base.destroy);

    xv->base.request_move.notify = xdg_toplevel_request_move;
    wl_signal_add(&toplevel->events.request_move, &xv->base.request_move);

    xv->base.request_resize.notify = xdg_toplevel_request_resize;
    wl_signal_add(&toplevel->events.request_resize, &xv->base.request_resize);

    xv->base.request_maximize.notify = xdg_toplevel_request_maximize;
    wl_signal_add(&toplevel->events.request_maximize,
                  &xv->base.request_maximize);

    xv->base.request_fullscreen.notify = xdg_toplevel_request_fullscreen;
    wl_signal_add(&toplevel->events.request_fullscreen,
                  &xv->base.request_fullscreen);
}

static void xdg_popup_commit(struct wl_listener* listener, void* data) {
    struct xdg_popup* popup = wl_container_of(listener, popup, commit);

    if (popup->xdg_popup->base->initial_commit) {
        // TODO: NEEDS to change the popups geometry. Sometimes renders
        // offscreen
        wlr_xdg_surface_schedule_configure(popup->xdg_popup->base);
    }
}

static void xdg_popup_destroy(struct wl_listener* listener, void* data) {
    struct xdg_popup* popup = wl_container_of(listener, popup, destroy);

    wl_list_remove(&popup->commit.link);
    wl_list_remove(&popup->destroy.link);

    free(popup);
}

void server_new_xdg_popup(struct wl_listener* listener, void* data) {
    struct wlr_xdg_popup* wlr_xdg_popup = data;

    struct xdg_popup* popup = calloc(1, sizeof(*popup));
    popup->xdg_popup = wlr_xdg_popup;

    struct wlr_xdg_surface* parent =
        wlr_xdg_surface_try_from_wlr_surface(wlr_xdg_popup->parent);

    if (!parent) {
        wlr_log(WLR_ERROR, "Xdg popup has no parent!");
        free(popup);
        return;
    }

    struct wlr_scene_tree* parent_tree = parent->data;
    if (!parent_tree) {
        wlr_log(WLR_ERROR, "Xdg popup parent has no scene tree!");
        free(popup);
        return;
    }

    wlr_xdg_popup->base->data =
        wlr_scene_xdg_surface_create(parent_tree, wlr_xdg_popup->base);

    popup->commit.notify = xdg_popup_commit;
    wl_signal_add(&wlr_xdg_popup->base->surface->events.commit, &popup->commit);

    popup->destroy.notify = xdg_popup_destroy;
    wl_signal_add(&wlr_xdg_popup->events.destroy, &popup->destroy);
}
