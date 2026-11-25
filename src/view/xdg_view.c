#include "view/xdg_view.h"
#include "config.h"
#include "input/input.h"
#include "monitor.h"
#include "server.h"
#include "view/view.h"
#include "windows/layout.h"
#include "windows/workspace.h"
#include "wlr/util/log.h"
#include <stdbool.h>
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

static void xdg_view_set_size(struct view* v, int w, int h) {
    struct xdg_view* xv = XDG_VIEW(v);

    if (v->borders[0]) {
        view_set_borders(v, w, h);
    }
    struct config* config = v->server->config;
    int bw = v->is_fullscreen ? 0 : 2 * config->border_width;

    int view_width = w - bw;
    int view_height = h - bw;

    if (view_width <= 0)
        view_width = 1;
    if (view_height <= 0)
        view_height = 1;

    xv->pending_configure_serial =
        wlr_xdg_toplevel_set_size(xv->toplevel, view_width, view_height);
}

static bool xdg_view_geometry_is_current(struct view* v) {
    struct xdg_view* xv = XDG_VIEW(v);
    uint32_t serial = xv->pending_configure_serial;

    return serial == 0 ||
           xv->toplevel->base->current.configure_serial >= serial;
}

static void xdg_toplevel_map(struct wl_listener* listener, void* data) {
    struct view* view = wl_container_of(listener, view, map);

    view_map(view);
}

static void xdg_toplevel_unmap(struct wl_listener* listener, void* data) {
    struct view* view = wl_container_of(listener, view, unmap);

    view->mapped = false;

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

    view_apply_pending_move(view);
}

static void xdg_toplevel_destroy(struct wl_listener* listener, void* data) {
    struct view* view = wl_container_of(listener, view, destroy);

    wl_list_remove(&view->map.link);
    wl_list_remove(&view->unmap.link);
    wl_list_remove(&view->commit.link);
    wl_list_remove(&view->destroy.link);
    wl_list_remove(&view->request_move.link);
    wl_list_remove(&view->request_resize.link);
    wl_list_remove(&view->request_maximize.link);
    wl_list_remove(&view->request_fullscreen.link);
    wl_list_remove(&view->set_title.link);
    wl_list_remove(&view->set_app_id.link);

    view_destroy(view);
    wlr_scene_node_destroy(&view->scene_tree->node);
    free(view);
}

static void xdg_toplevel_request_move(struct wl_listener* listener,
                                      void* data) {
    struct view* view = wl_container_of(listener, view, request_move);
    struct xdg_view* xdg_view = XDG_VIEW(view);
    if (xdg_view->toplevel->base->initialized) {
        wlr_xdg_surface_schedule_configure(xdg_view->toplevel->base);
    }
}

static void xdg_toplevel_request_resize(struct wl_listener* listener,
                                        void* data) {

    struct view* view = wl_container_of(listener, view, request_resize);
    struct wlr_xdg_toplevel_resize_event* event = data;

    struct xdg_view* xdg_view = XDG_VIEW(view);
    if (xdg_view->toplevel->base->initialized) {
        wlr_xdg_surface_schedule_configure(xdg_view->toplevel->base);
    }
}

static void xdg_toplevel_request_maximize(struct wl_listener* listener,
                                          void* data) {
    struct view* view = wl_container_of(listener, view, request_maximize);
    struct xdg_view* xdg_view = XDG_VIEW(view);
    (void)data;
    if (!xdg_view->toplevel->base->initialized)
        return;

    view_set_maximized(view, xdg_view->toplevel->requested.maximized);
}

static void xdg_toplevel_request_fullscreen(struct wl_listener* listener,
                                            void* data) {
    struct view* view = wl_container_of(listener, view, request_fullscreen);
    struct xdg_view* xdg_view = XDG_VIEW(view);
    (void)data;
    if (!xdg_view->toplevel->base->initialized)
        return;

    xdg_view->base.impl->set_fullscreen(
        view, xdg_view->toplevel->requested.fullscreen);
}

// IMPL

static void xdg_view_get_geometry(struct view* v, struct wlr_box* box) {
    struct xdg_view* xv = XDG_VIEW(v);
    *box = xv->toplevel->base->geometry;
    (*box).x = v->scene_tree->node.x;
    (*box).y = v->scene_tree->node.y;
}

static void xdg_view_get_surface_layout_pos(struct view* v, double* x,
                                            double* y) {
    struct xdg_view* xv = XDG_VIEW(v);
    struct wlr_box geo = xv->toplevel->base->geometry;
    *x = v->scene_tree->node.x - geo.x;
    *y = v->scene_tree->node.y - geo.y;
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

static bool xdg_view_want_maximized(struct view* v) {
    struct xdg_view* xv = XDG_VIEW(v);
    return xv->toplevel->requested.maximized == true;
}

static bool xdg_view_want_floating(struct view* v) {
    struct xdg_view* xv = XDG_VIEW(v);
    // Dialogs declare their owning toplevel through xdg_toplevel.set_parent
    return xv->toplevel->parent != NULL;
}

static struct wlr_surface* xdg_view_get_surface(struct view* v) {
    struct xdg_view* xv = XDG_VIEW(v);
    return xv->toplevel->base->surface;
}

static void xdg_view_set_fullscreen(struct view* v, bool fullscreen) {
    if (v->is_fullscreen == fullscreen)
        return;
    struct xdg_view* xdg_view = XDG_VIEW(v);

    wlr_xdg_toplevel_set_fullscreen(xdg_view->toplevel, fullscreen);
    view_set_fullscreen(v, fullscreen);
    if (fullscreen) {
        struct monitor* monitor = v->workspace->col->monitor;
        xdg_view->base.impl->set_size(v, monitor->box.width,
                                      monitor->box.height);
    }
}

static void xdg_view_set_maximized(struct view* v, bool maximized) {
    struct xdg_view* xdg_view = XDG_VIEW(v);
    wlr_xdg_toplevel_set_maximized(xdg_view->toplevel, maximized);
}

static struct wlr_scene* xdg_view_create_capture_scene(struct view* v) {
    struct xdg_view* xdg_view = XDG_VIEW(v);
    if (!xdg_view->toplevel || !xdg_view->toplevel->base)
        return NULL;

    struct wlr_scene* capture_scene = wlr_scene_create();
    if (!capture_scene)
        return NULL;

    if (!wlr_scene_xdg_surface_create(&capture_scene->tree,
                                      xdg_view->toplevel->base)) {
        wlr_scene_node_destroy(&capture_scene->tree.node);
        return NULL;
    }

    return capture_scene;
}

static const struct view_impl xdg_view_impl = {
    .get_geometry = xdg_view_get_geometry,
    .get_surface_layout_pos = xdg_view_get_surface_layout_pos,
    .set_size = xdg_view_set_size,
    .geometry_is_current = xdg_view_geometry_is_current,
    .set_activated = xdg_view_set_activated,
    .close = xdg_view_close,
    .get_title = xdg_view_get_title,
    .get_app_id = xdg_view_get_app_id,
    .want_fullscreen = xdg_view_want_fullscreen,
    .want_maximized = xdg_view_want_maximized,
    .want_floating = xdg_view_want_floating,
    .get_surface = xdg_view_get_surface,
    .set_fullscreen = xdg_view_set_fullscreen,
    .set_maximized = xdg_view_set_maximized,
    .create_capture_scene = xdg_view_create_capture_scene,
};

//

void server_new_xdg_toplevel(struct wl_listener* listener, void* data) {
    struct wlr_xdg_toplevel* toplevel = data;
    struct liars_server* server =
        wl_container_of(listener, server, new_xdg_toplevel);

    struct xdg_view* xv = calloc(1, sizeof(*xv));
    wl_list_init(&xv->base.link);
    xv->toplevel = toplevel;
    xv->base.impl = &xdg_view_impl;
    xv->base.surface = toplevel->base->surface;
    xv->base.surface->data = &xv->base;
    xv->base.server = server;

    xv->base.scene_tree = wlr_scene_tree_create(&server->scene->tree);
    xv->base.scene_tree->node.data = &xv->base;

    xv->surface_tree =
        wlr_scene_xdg_surface_create(xv->base.scene_tree, xv->toplevel->base);

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

    xv->base.set_title.notify = view_set_title;
    wl_signal_add(&toplevel->events.set_title, &xv->base.set_title);

    xv->base.set_app_id.notify = view_set_app_id;
    wl_signal_add(&toplevel->events.set_app_id, &xv->base.set_app_id);

    toplevel->base->data = xv;
}

static void xdg_popup_unconstrain(struct xdg_popup* popup) {
    struct wlr_xdg_popup* xdg_popup = popup->xdg_popup;
    struct monitor* monitor =
        popup->parent_view ? popup->parent_view->workspace->col->monitor
                           : get_active_workspace(popup->server)->col->monitor;
    struct wlr_box constraint_box;

    double root_global_x = 0, root_global_y = 0;
    if (popup->parent_view) {
        popup->parent_view->impl->get_surface_layout_pos(
            popup->parent_view, &root_global_x, &root_global_y);
    } else {
        int x = 0, y = 0;
        wlr_scene_node_coords(&popup->root_tree->node, &x, &y);
        root_global_x = x;
        root_global_y = y;
    }

    constraint_box.x = monitor->box.x - (int)root_global_x;
    constraint_box.y = monitor->box.y - (int)root_global_y;
    constraint_box.width = monitor->box.width;
    constraint_box.height = monitor->box.height;
    wlr_xdg_popup_unconstrain_from_box(xdg_popup, &constraint_box);
}

static void xdg_popup_commit(struct wl_listener* listener, void* data) {
    struct xdg_popup* popup = wl_container_of(listener, popup, commit);
    if (!popup->xdg_popup->base->initial_commit)
        return;

    xdg_popup_unconstrain(popup);
    wlr_xdg_surface_schedule_configure(popup->xdg_popup->base);
}

static void xdg_popup_reposition(struct wl_listener* listener, void* data) {
    struct xdg_popup* popup = wl_container_of(listener, popup, reposition);
    xdg_popup_unconstrain(popup);
}

static void xdg_popup_destroy(struct wl_listener* listener, void* data) {
    struct xdg_popup* popup = wl_container_of(listener, popup, destroy);

    wl_list_remove(&popup->commit.link);
    wl_list_remove(&popup->destroy.link);
    wl_list_remove(&popup->reposition.link);

    free(popup);
}

void xdg_popup_create(struct wlr_xdg_popup* wlr_xdg_popup,
                      struct wlr_scene_tree* parent_tree,
                      struct wlr_scene_tree* root_tree,
                      struct view* parent_view, struct liars_server* server) {
    struct xdg_popup* popup = calloc(1, sizeof(*popup));
    if (!popup)
        return;

    popup->server = server;
    popup->parent_view = parent_view;
    popup->root_tree = root_tree;
    popup->xdg_popup = wlr_xdg_popup;

    struct wlr_scene_tree* scene_tree =
        wlr_scene_xdg_surface_create(parent_tree, wlr_xdg_popup->base);
    if (!scene_tree) {
        wlr_log(WLR_ERROR, "Failed to create popup scene tree");
        free(popup);
        return;
    }
    popup->scene_tree = scene_tree;
    wlr_xdg_popup->base->data = popup;

    popup->commit.notify = xdg_popup_commit;
    wl_signal_add(&wlr_xdg_popup->base->surface->events.commit, &popup->commit);

    popup->destroy.notify = xdg_popup_destroy;
    wl_signal_add(&wlr_xdg_popup->events.destroy, &popup->destroy);

    popup->reposition.notify = xdg_popup_reposition;
    wl_signal_add(&wlr_xdg_popup->events.reposition, &popup->reposition);
}

void server_new_xdg_popup(struct wl_listener* listener, void* data) {
    struct liars_server* server =
        wl_container_of(listener, server, new_xdg_popup);
    struct wlr_xdg_popup* wlr_xdg_popup = data;
    if (!wlr_xdg_popup->parent)
        return; // layer surface popups are handled by their layer surface

    struct wlr_xdg_surface* parent =
        wlr_xdg_surface_try_from_wlr_surface(wlr_xdg_popup->parent);
    if (!parent) {
        wlr_log(WLR_ERROR, "Xdg popup has no parent!");
        return;
    }

    struct wlr_scene_tree* parent_tree = NULL;
    struct wlr_scene_tree* root_tree = NULL;
    struct view* parent_view = NULL;
    if (parent->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
        struct xdg_view* parent_xdg_view = parent->data;
        if (parent_xdg_view) {
            parent_view = &parent_xdg_view->base;
            parent_tree = parent_xdg_view->surface_tree;
            root_tree = parent_xdg_view->surface_tree;
        }
    } else if (parent->role == WLR_XDG_SURFACE_ROLE_POPUP) {
        struct xdg_popup* parent_popup = parent->data;
        if (parent_popup) {
            parent_view = parent_popup->parent_view;
            parent_tree = parent_popup->scene_tree;
            root_tree = parent_popup->root_tree;
        }
    }

    if (!parent_tree || !root_tree) {
        wlr_log(WLR_ERROR, "Xdg popup has no usable parent tree!");
        return;
    }

    xdg_popup_create(wlr_xdg_popup, parent_tree, root_tree, parent_view,
                     server);
}
