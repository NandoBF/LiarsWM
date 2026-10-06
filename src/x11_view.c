#include "x11_view.h"
#include "input.h"
#include "layout.h"
#include "view.h"
#include "server.h"
#include "wlr/util/log.h"
#include "workspace.h"
#include "config.h"
#include <wayland-server-core.h>
#include <wayland-util.h>

struct unmanaged_x11 {
    struct wlr_xwayland_surface* xsurface;
    struct wlr_scene_tree* scene_tree;
    struct liars_server* server;
    struct wl_listener associate;
    struct wl_listener dissociate;
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener destroy;
    struct wl_listener request_configure;
};

static void unmanaged_x11_map(struct wl_listener* listener, void* data) {
    struct unmanaged_x11* unmanaged = wl_container_of(listener, unmanaged, map);

    unmanaged->scene_tree = wlr_scene_subsurface_tree_create(&unmanaged->server->scene->tree, unmanaged->xsurface->surface);
    unmanaged->scene_tree->node.data = data;

    wlr_scene_node_set_position(&unmanaged->scene_tree->node, unmanaged->xsurface->x, unmanaged->xsurface->y);
}

static void unmanaged_x11_unmap(struct wl_listener* listener, void* data) {
    struct unmanaged_x11* unmanaged = wl_container_of(listener, unmanaged, unmap);

    wlr_scene_node_destroy(&unmanaged->scene_tree->node);
    unmanaged->scene_tree = NULL;
}

static void unmanaged_x11_destroy(struct wl_listener* listener, void* data) {
    struct unmanaged_x11* unmanaged = wl_container_of(listener, unmanaged, destroy);

    wl_list_remove(&unmanaged->associate.link);
    wl_list_remove(&unmanaged->dissociate.link);
    wl_list_remove(&unmanaged->destroy.link);
    wl_list_remove(&unmanaged->request_configure.link);

    free(unmanaged);
}

static void unmanaged_x11_request_configure(struct wl_listener* listener, void* data) {
    struct unmanaged_x11* unmanaged = wl_container_of(listener, unmanaged, request_configure);
    struct wlr_xwayland_surface_configure_event* event = data;

    wlr_xwayland_surface_configure(unmanaged->xsurface, event->x, event->y, event->width, event->height);
    if (unmanaged->scene_tree) {
        wlr_scene_node_set_position(&unmanaged->scene_tree->node, event->x, event->y);
    }
}

static void unmanaged_x11_associate(struct wl_listener* listener, void* data) {
    struct unmanaged_x11* unmanaged = wl_container_of(listener, unmanaged, associate);
    wl_signal_add(&unmanaged->xsurface->surface->events.map, &unmanaged->map);
    wl_signal_add(&unmanaged->xsurface->surface->events.unmap, &unmanaged->unmap);
}

static void unmanaged_x11_dissociate(struct wl_listener* listener, void* data) {
    struct unmanaged_x11* unmanaged = wl_container_of(listener, unmanaged, dissociate);
    wl_list_remove(&unmanaged->map.link);
    wl_list_remove(&unmanaged->unmap.link);
}

static void handle_unmanaged_x11(struct liars_server* server, struct wlr_xwayland_surface* xsurface) {
    struct unmanaged_x11* unmanaged = calloc(1, sizeof(*unmanaged));
    unmanaged->xsurface = xsurface;
    unmanaged->server = server;

    unmanaged->map.notify = unmanaged_x11_map;
    unmanaged->unmap.notify = unmanaged_x11_unmap;

    unmanaged->associate.notify = unmanaged_x11_associate;
    wl_signal_add(&xsurface->events.associate, &unmanaged->associate);

    unmanaged->dissociate.notify = unmanaged_x11_dissociate;
    wl_signal_add(&xsurface->events.dissociate, &unmanaged->dissociate);

    unmanaged->destroy.notify = unmanaged_x11_destroy;
    wl_signal_add(&xsurface->events.destroy, &unmanaged->destroy);

    unmanaged->request_configure.notify = unmanaged_x11_request_configure;
    wl_signal_add(&xsurface->events.request_configure, &unmanaged->request_configure);
}


static void x11_view_map(struct wl_listener* listener, void* data) {
    struct view* view = wl_container_of(listener, view, map);
    struct x11_view* x11_v = (struct x11_view *)view; 

    view->surface = x11_v->xsurface->surface;
    view->scene_tree = wlr_scene_tree_create(&view->server->scene->tree);
    view->scene_tree->node.data = view;

    x11_v->surface_tree = wlr_scene_subsurface_tree_create(view->scene_tree, view->surface);
    wlr_scene_node_set_position(&x11_v->surface_tree->node, 5, 5);

    view_map(view);
}

static void x11_view_unmap(struct wl_listener* listener, void* data) {
    struct view *view = wl_container_of(listener, view, unmap);

    if (view == view->server->cursor->grabbed_view) {
    reset_cursor_mode(view->server->cursor);
    }
    struct wlr_seat *seat = view->server->seat->seat;
    if (seat->keyboard_state.focused_surface == view->impl->get_surface(view)) {
    wlr_seat_keyboard_clear_focus(seat);
    }

    wl_list_remove(&view->link);
    workspace_arrange(view->workspace);
}

static void x11_view_destroy(struct wl_listener* listener, void* data) {
    struct view *view = wl_container_of(listener, view, unmap);
    struct x11_view* x11_v = (struct x11_view *)view; 

    wl_list_remove(&x11_v->associate.link);
    wl_list_remove(&x11_v->dissociate.link);
    wl_list_remove(&view->commit.link);
    wl_list_remove(&view->destroy.link);
    wl_list_remove(&view->request_move.link);
    wl_list_remove(&view->request_resize.link);
    wl_list_remove(&view->request_maximize.link);
    wl_list_remove(&view->request_fullscreen.link);
    wl_list_remove(&x11_v->request_configure.link);
    wl_list_remove(&x11_v->grab_focus.link);

    wlr_scene_node_destroy(&view->scene_tree->node);
    free(view); // perhaps a destructor would be better
}

static void x11_view_request_configure(struct wl_listener* listener, void* data) {
    //TODO:
    struct x11_view* x11_v = wl_container_of(listener, x11_v, request_configure);
    struct wlr_xwayland_surface_configure_event* event = data;
}



static void x11_view_get_geometry(struct view* v, struct wlr_box* box) {
    struct x11_view* x11_v = (struct x11_view *)v; 
    box->x = x11_v->xsurface->x;
    box->y = x11_v->xsurface->y;
    box->width = x11_v->xsurface->width;
    box->height = x11_v->xsurface->height;

}


static void x11_view_set_activated(struct view* v, bool activated) {
    struct x11_view* x11_v = (struct x11_view *)v; 
    wlr_xwayland_surface_activate(x11_v->xsurface, activated);
}

static void x11_view_close(struct view* v) {
    struct x11_view* x11_v = (struct x11_view *)v; 
    wlr_xwayland_surface_close(x11_v->xsurface);
}

static const char* x11_view_get_title(struct view* v) {
    struct x11_view* x11_v = (struct x11_view *)v; 
    return x11_v->xsurface->title;
}

static const char* x11_view_get_app_id(struct view* v) {
    struct x11_view* x11_v = (struct x11_view *)v; 
    return x11_v->xsurface->class;
}

static struct wlr_surface* x11_view_get_surface(struct view* v) {
    struct x11_view* x11_v = (struct x11_view *)v; 
    return x11_v->xsurface->surface;
}

static void x11_view_set_size(struct view* v, int w, int h) {
    struct x11_view* x11_v = (struct x11_view *)v; 
    
    if (v->borders[0]) {
        view_set_borders(v, w, h);
    }
    int view_width = w - (2 * BORDER_WIDTH);
    int view_height = h - (2 * BORDER_WIDTH);
    
    if (view_width <= 0) view_width = 1;
    if (view_height <= 0) view_height = 1;

    wlr_xwayland_surface_configure(x11_v->xsurface, v->scene_tree->node.x + BORDER_WIDTH, v->scene_tree->node.y + BORDER_WIDTH, view_width, view_height);
}


static bool x11_view_want_fullscreen(struct view *v) {
    struct x11_view* x11_v = (struct x11_view *)v; 
    return x11_v->xsurface->fullscreen;
}

static const struct view_impl x11_view_impl = {
    .get_geometry = x11_view_get_geometry,
    .set_size = x11_view_set_size,
    .set_activated = x11_view_set_activated,
    .close = x11_view_close,
    .get_title = x11_view_get_title,
    .get_app_id = x11_view_get_app_id,
    .want_fullscreen = x11_view_want_fullscreen,
    .get_surface = x11_view_get_surface,
};


static void x11_view_grab_focus(struct wl_listener* listener, void* data) {
    struct x11_view* xv = wl_container_of(listener, xv, grab_focus);
    focus_view(&xv->base);
}

static void x11_view_associate(struct wl_listener* listener, void* data) {
    struct x11_view* xv = wl_container_of(listener, xv, associate);
    wl_signal_add(&xv->xsurface->surface->events.map, &xv->base.map);
    wl_signal_add(&xv->xsurface->surface->events.unmap, &xv->base.unmap);
}

static void x11_view_dissociate(struct wl_listener* listener, void* data) {
    struct x11_view* xv = wl_container_of(listener, xv, dissociate);
    wl_list_remove(&xv->base.map.link);
    wl_list_remove(&xv->base.unmap.link);
}

void server_new_xwayland_surface(struct wl_listener* listener, void* data) {
    struct wlr_xwayland_surface* xsurface = data;
    struct liars_server* server = 
        wl_container_of(listener, server, new_xwayland_surface);

    if (xsurface->override_redirect) {
        handle_unmanaged_x11(server, xsurface);
        return;
    }

    struct x11_view* xv = calloc(1, sizeof(*xv));
    wl_list_init(&xv->base.link);
    xv->xsurface = xsurface;
    xv->base.impl = &x11_view_impl;
    xv->base.server = server;
    xsurface->data = xv;

    xv->base.map.notify = x11_view_map;
    xv->base.unmap.notify = x11_view_unmap;

    xv->associate.notify = x11_view_associate;
    wl_signal_add(&xsurface->events.associate, &xv->associate);

    xv->dissociate.notify = x11_view_dissociate;
    wl_signal_add(&xsurface->events.dissociate, &xv->dissociate);

    xv->base.destroy.notify = x11_view_destroy;
    wl_signal_add(&xsurface->events.destroy, &xv->base.destroy);

    xv->request_configure.notify = x11_view_request_configure;
    wl_signal_add(&xsurface->events.request_configure, &xv->request_configure);

    xv->grab_focus.notify = x11_view_grab_focus;
    wl_signal_add(&xsurface->events.grab_focus, &xv->grab_focus);

    wlr_log(WLR_DEBUG, "Started x11 surface");
}

