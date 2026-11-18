#include "view/x11_view.h"
#include "config.h"
#include "input/input.h"
#include "monitor.h"
#include "view/view.h"
#include "windows/layout.h"
#include "windows/workspace.h"
#include "wlr/util/log.h"
#include <wayland-server-core.h>
#include <wayland-util.h>

#define X11_VIEW(ptr) ((struct x11_view*)(ptr))

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

    unmanaged->scene_tree = wlr_scene_subsurface_tree_create(
        &unmanaged->server->scene->tree, unmanaged->xsurface->surface);
    unmanaged->scene_tree->node.data = data;

    wlr_scene_node_set_position(&unmanaged->scene_tree->node,
                                unmanaged->xsurface->x, unmanaged->xsurface->y);
}

static void unmanaged_x11_unmap(struct wl_listener* listener, void* data) {
    struct unmanaged_x11* unmanaged =
        wl_container_of(listener, unmanaged, unmap);

    wlr_scene_node_destroy(&unmanaged->scene_tree->node);
    unmanaged->scene_tree = NULL;
}

static void unmanaged_x11_destroy(struct wl_listener* listener, void* data) {
    struct unmanaged_x11* unmanaged =
        wl_container_of(listener, unmanaged, destroy);

    wl_list_remove(&unmanaged->associate.link);
    wl_list_remove(&unmanaged->dissociate.link);
    wl_list_remove(&unmanaged->destroy.link);
    wl_list_remove(&unmanaged->request_configure.link);

    free(unmanaged);
}

static void unmanaged_x11_request_configure(struct wl_listener* listener,
                                            void* data) {
    struct unmanaged_x11* unmanaged =
        wl_container_of(listener, unmanaged, request_configure);
    struct wlr_xwayland_surface_configure_event* event = data;

    wlr_xwayland_surface_configure(unmanaged->xsurface, event->x, event->y,
                                   event->width, event->height);
    if (unmanaged->scene_tree) {
        wlr_scene_node_set_position(&unmanaged->scene_tree->node, event->x,
                                    event->y);
    }
}

static void unmanaged_x11_associate(struct wl_listener* listener, void* data) {
    struct unmanaged_x11* unmanaged =
        wl_container_of(listener, unmanaged, associate);
    wl_signal_add(&unmanaged->xsurface->surface->events.map, &unmanaged->map);
    wl_signal_add(&unmanaged->xsurface->surface->events.unmap,
                  &unmanaged->unmap);
}

static void unmanaged_x11_dissociate(struct wl_listener* listener, void* data) {
    struct unmanaged_x11* unmanaged =
        wl_container_of(listener, unmanaged, dissociate);
    wl_list_remove(&unmanaged->map.link);
    wl_list_remove(&unmanaged->unmap.link);
}

static void handle_unmanaged_x11(struct liars_server* server,
                                 struct wlr_xwayland_surface* xsurface) {
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
    wl_signal_add(&xsurface->events.request_configure,
                  &unmanaged->request_configure);
}

static void x11_view_map(struct wl_listener* listener, void* data) {
    struct view* view = wl_container_of(listener, view, map);
    struct x11_view* x11_v = X11_VIEW(view);

    view->surface = x11_v->xsurface->surface;
    view->surface->data = x11_v->xsurface->data;
    view->scene_tree = wlr_scene_tree_create(&view->server->scene->tree);
    view->scene_tree->node.data = view;

    x11_v->surface_tree =
        wlr_scene_subsurface_tree_create(view->scene_tree, view->surface);
    view_map(view);
}

static void x11_view_unmap(struct wl_listener* listener, void* data) {
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

static void x11_view_destroy(struct wl_listener* listener, void* data) {
    struct view* view = wl_container_of(listener, view, destroy);
    struct x11_view* x11_v = X11_VIEW(view);

    wl_list_remove(&x11_v->associate.link);
    wl_list_remove(&x11_v->dissociate.link);
    wl_list_remove(&x11_v->request_configure.link);
    wl_list_remove(&x11_v->request_maximize.link);
    wl_list_remove(&x11_v->request_fullscreen.link);
    wl_list_remove(&x11_v->grab_focus.link);
    wl_list_remove(&view->set_title.link);
    wl_list_remove(&view->destroy.link);

    view_destroy(view);
    wlr_scene_node_destroy(&view->scene_tree->node);
    free(view);
}

static void x11_view_request_configure(struct wl_listener* listener,
                                       void* data) {
    struct x11_view* x11_v =
        wl_container_of(listener, x11_v, request_configure);
    struct view* view = &x11_v->base;
    struct wlr_xwayland_surface_configure_event* event = data;

    if (!view->mapped || !view->scene_tree) {
        wlr_xwayland_surface_configure(x11_v->xsurface, event->x, event->y,
                                       event->width, event->height);
        return;
    }

    if (!view->is_floating || view->is_maximized || view->is_fullscreen) {
        wlr_xwayland_surface_configure(
            x11_v->xsurface, view->scene_tree->node.x, view->scene_tree->node.y,
            x11_v->xsurface->width, x11_v->xsurface->height);
        return;
    }

    wlr_scene_node_set_position(&view->scene_tree->node, event->x, event->y);
    int borders = 2 * view->server->config->border_width;
    view->impl->set_size(view, event->width + borders, event->height + borders);
}

static void x11_view_request_maximize(struct wl_listener* listener,
                                      void* data) {
    struct x11_view* xv = wl_container_of(listener, xv, request_maximize);
    (void)data;
    view_set_maximized(&xv->base, xv->xsurface->maximized_horz &&
                                      xv->xsurface->maximized_vert);
}

static void x11_view_request_fullscreen(struct wl_listener* listener,
                                        void* data) {
    struct x11_view* xv = wl_container_of(listener, xv, request_fullscreen);
    (void)data;
    xv->base.impl->set_fullscreen(&xv->base, xv->xsurface->fullscreen);
}

static void x11_view_get_geometry(struct view* v, struct wlr_box* box) {
    struct x11_view* x11_v = X11_VIEW(v);
    box->x = v->scene_tree->node.x;
    box->y = v->scene_tree->node.y;
    box->width = x11_v->xsurface->width;
    box->height = x11_v->xsurface->height;
}

static void x11_view_get_surface_layout_pos(struct view* v, double* x,
                                            double* y) {
    *x = v->scene_tree->node.x;
    *y = v->scene_tree->node.y;
}

static void x11_view_set_activated(struct view* v, bool activated) {
    struct x11_view* x11_v = X11_VIEW(v);
    wlr_xwayland_surface_activate(x11_v->xsurface, activated);
}

static void x11_view_close(struct view* v) {
    struct x11_view* x11_v = X11_VIEW(v);
    wlr_xwayland_surface_close(x11_v->xsurface);
}

static const char* x11_view_get_title(struct view* v) {
    struct x11_view* x11_v = X11_VIEW(v);
    return x11_v->xsurface->title;
}

static const char* x11_view_get_app_id(struct view* v) {
    struct x11_view* x11_v = X11_VIEW(v);
    return x11_v->xsurface->class;
}

static struct wlr_surface* x11_view_get_surface(struct view* v) {
    struct x11_view* x11_v = X11_VIEW(v);
    return x11_v->xsurface->surface;
}

static void x11_view_set_size(struct view* v, int w, int h) {
    struct x11_view* x11_v = X11_VIEW(v);

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

    wlr_xwayland_surface_configure(x11_v->xsurface, v->scene_tree->node.x,
                                   v->scene_tree->node.y, view_width,
                                   view_height);
}

static bool x11_view_want_fullscreen(struct view* v) {
    struct x11_view* x11_v = X11_VIEW(v);
    return x11_v->xsurface->fullscreen;
}

static bool x11_view_want_maximized(struct view* v) {
    struct x11_view* x11_v = X11_VIEW(v);
    return x11_v->xsurface->maximized_horz && x11_v->xsurface->maximized_vert;
}

static void x11_view_set_fullscreen(struct view* v, bool fullscreen) {
    struct x11_view* x11_view = X11_VIEW(v);
    v->is_fullscreen = fullscreen;
    wlr_xwayland_surface_set_fullscreen(x11_view->xsurface, fullscreen);
    view_set_fullscreen(v, fullscreen);
    if (fullscreen) {
        struct monitor* monitor = v->workspace->col->monitor;
        x11_view->base.impl->set_size(v, monitor->output->width,
                                      monitor->output->height);
    }
}

static void x11_view_set_maximized(struct view* v, bool maximized) {
    struct x11_view* x11_v = X11_VIEW(v);
    wlr_xwayland_surface_set_maximized(x11_v->xsurface, maximized, maximized);
}

static struct wlr_scene* x11_view_create_capture_scene(struct view* v) {
    struct x11_view* x11_view = X11_VIEW(v);
    if (!x11_view->xsurface || !x11_view->xsurface->surface)
        return NULL;

    struct wlr_scene* capture_scene = wlr_scene_create();
    if (!capture_scene)
        return NULL;

    if (!wlr_scene_surface_create(&capture_scene->tree,
                                  x11_view->xsurface->surface)) {
        wlr_scene_node_destroy(&capture_scene->tree.node);
        return NULL;
    }

    return capture_scene;
}

static const struct view_impl x11_view_impl = {
    .get_geometry = x11_view_get_geometry,
    .get_surface_layout_pos = x11_view_get_surface_layout_pos,
    .set_size = x11_view_set_size,
    .set_activated = x11_view_set_activated,
    .close = x11_view_close,
    .get_title = x11_view_get_title,
    .get_app_id = x11_view_get_app_id,
    .want_fullscreen = x11_view_want_fullscreen,
    .want_maximized = x11_view_want_maximized,
    .get_surface = x11_view_get_surface,
    .set_fullscreen = x11_view_set_fullscreen,
    .set_maximized = x11_view_set_maximized,
    .create_capture_scene = x11_view_create_capture_scene};

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
    xv->xsurface->data = &xv->base;
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

    xv->request_maximize.notify = x11_view_request_maximize;
    wl_signal_add(&xsurface->events.request_maximize, &xv->request_maximize);

    xv->request_fullscreen.notify = x11_view_request_fullscreen;
    wl_signal_add(&xsurface->events.request_fullscreen,
                  &xv->request_fullscreen);

    xv->grab_focus.notify = x11_view_grab_focus;
    wl_signal_add(&xsurface->events.grab_focus, &xv->grab_focus);

    xv->base.set_title.notify = view_set_title;
    wl_signal_add(&xsurface->events.set_title, &xv->base.set_title);

    wlr_log(WLR_DEBUG, "Started x11 surface");
}
