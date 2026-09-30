#include "xdg_view.h"
#include "input.h"
#include "server.h"
#include "view.h"
#include "workspace.h"
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/types/wlr_xdg_shell.h>

struct xdg_view {
    struct view base;
    struct wlr_xdg_toplevel* toplevel;
    uint32_t pending_configure_serial;
};

static void xdg_view_set_size(struct view* v, int w, int h) {
    struct xdg_view* xv = (struct xdg_view*)v;
    xv->pending_configure_serial = 
        wlr_xdg_toplevel_set_size(xv->toplevel, w, h);
}

static void xdg_toplevel_map(struct wl_listener* listener, void* data) {
    struct view* view = wl_container_of(listener, view, map);

    view->mapped = true;
    view->workspace = get_active_workspace(view->server);
    wlr_scene_node_reparent(&view->scene_tree->node, view->workspace->tree);
    wl_list_insert(&view->workspace->tiles, &view->link);

    focus_view(view);
}

static void xdg_toplevel_unmap(struct wl_listener* listener, void* data) {
    struct view* view = wl_container_of(listener, view, unmap);

    if (view == view->server->cursor->grabbed_view) {
        reset_cursor_mode(view->server->cursor); 
    }

    wl_list_remove(&view->link);
} 

static void xdg_toplevel_commit(struct wl_listener* listener, void* data){
    struct view* view = wl_container_of(listener, view, commit);
    struct xdg_view* xv = (struct xdg_view*)view;

    if (xv->toplevel->base->initial_commit) {
        wlr_xdg_toplevel_set_size(xv->toplevel, 0, 0);
    }
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

    free(view); // perhaps a destructor would be better
}

static void xdg_toplevel_request_move(struct wl_listener* listener, void* data){
    struct view* view = wl_container_of(listener, view, request_move);
    //begin_interactive(view, LIARSWM_CURSOR_MOVE, 0);
    // TODO:
}

static void xdg_toplevel_request_resize(struct wl_listener* listener, void* data) {
    //TODO:
}

static void xdg_toplevel_request_maximize(struct wl_listener* listener, void* data) {
    // TODO:
}

static void xdg_toplevel_request_fullscreen(struct wl_listener* listener, void* data) {
    // TODO:
}


// IMPL

static void xdg_view_get_geometry(struct view* v, struct wlr_box* box) {
    struct xdg_view* xv = (struct xdg_view*)v;
    *box = xv->toplevel->base->geometry;
}

static void xdg_view_set_activated(struct view* v, bool activated) {
    struct xdg_view* xv = (struct xdg_view*)v;
    wlr_xdg_toplevel_set_activated(xv->toplevel, activated);
} 

static void xdg_view_close(struct view* v) {
    struct xdg_view* xv = (struct xdg_view*)v;
    wlr_xdg_toplevel_send_close(xv->toplevel);
}

static const char* xdg_view_get_title(struct view* v) {
    struct xdg_view* xv = (struct xdg_view*)v;
    return xv->toplevel->title;
}

static const char* xdg_view_get_app_id(struct view* v) {
    struct xdg_view* xv = (struct xdg_view*)v;
    return xv->toplevel->app_id;
}

static bool xdg_view_want_fullscreen(struct view* v) {
    struct xdg_view* xv = (struct xdg_view*)v;
    return xv->toplevel->requested.fullscreen == true;
}

static struct wlr_surface* xdg_view_get_surface(struct view* v) {
    struct xdg_view* xv = (struct xdg_view*)v;
    return xv->toplevel->base->surface;
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
};

//


void server_new_xdg_toplevel(struct wl_listener* listener, void* data){
    struct wlr_xdg_toplevel* toplevel = data;
    struct liars_server* server = wl_container_of(listener, server, new_xdg_toplevel);

    struct xdg_view* xv = calloc(1, sizeof(*xv));
    xv->toplevel = toplevel;
    xv->base.impl = &xdg_view_impl;
    xv->base.surface = toplevel->base->surface;
    xv->base.server = server;
    toplevel->base->data = xv;

    xv->base.scene_tree = 
        wlr_scene_xdg_surface_create(&server->scene->tree, xv->toplevel->base);
    xv->base.scene_tree->node.data = &xv->base;

    xv->base.map.notify = xdg_toplevel_map;
    wl_signal_add(&toplevel->base->surface->events.map, &xv->base.map);

    xv->base.unmap.notify = xdg_toplevel_unmap;
    wl_signal_add(&toplevel->base->surface->events.unmap, &xv->base.unmap);

    xv->base.commit.notify = xdg_toplevel_commit;
    wl_signal_add(&toplevel->base->surface->events.commit, &xv->base.commit);

    xv->base.destroy.notify = xdg_toplevel_destroy;
    wl_signal_add(&toplevel->base->surface->events.destroy, &xv->base.destroy);

    xv->base.request_move.notify = xdg_toplevel_request_move;
    wl_signal_add(&toplevel->events.request_move, &xv->base.request_move);

    xv->base.request_resize.notify = xdg_toplevel_request_resize;
    wl_signal_add(&toplevel->events.request_resize, &xv->base.request_resize);
    
    xv->base.request_maximize.notify = xdg_toplevel_request_maximize;
    wl_signal_add(&toplevel->events.request_maximize, &xv->base.request_maximize);
    
    xv->base.request_fullscreen.notify = xdg_toplevel_request_fullscreen;
    wl_signal_add(&toplevel->events.request_fullscreen, &xv->base.request_fullscreen);

}

