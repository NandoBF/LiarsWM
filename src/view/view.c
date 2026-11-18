#include "view/view.h"
#include "config.h"
#include "input/input.h"
#include "input/pointer_contraint.h"
#include "monitor.h"
#include "server.h"
#include "windows/layout.h"
#include "windows/workspace.h"
#include <wayland-util.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/xwayland/xwayland.h>

bool is_surface_focused(struct liars_seat* seat, struct wlr_surface* surface) {
    struct wlr_surface* focused = seat_focused_surface(seat);
    if (!focused || !surface)
        return false;
    return wlr_surface_get_root_surface(surface) ==
           wlr_surface_get_root_surface(focused);
}

bool is_view_focused(struct view* view) {
    if (view == NULL) {
        return false;
    }
    struct wlr_surface* focused_surface =
        seat_focused_surface(view->server->seat);
    return wlr_surface_get_root_surface(focused_surface) ==
           wlr_surface_get_root_surface(view->surface);
}

struct monitor* view_get_monitor(struct view* view) {
    return view->workspace->col->monitor;
}

void view_move_to_center(struct view* view) {
    struct monitor* monitor = view_get_monitor(view);
    struct wlr_box box;
    view->impl->get_geometry(view, &box);
    int x = (monitor->output->width - box.width) / 2;
    int y = (monitor->output->height - box.height) / 2;
    wlr_scene_node_set_position(&view->scene_tree->node, x, y);
}

void focus_view(struct view* view) {
    if (view == NULL) {
        return;
    }

    struct liars_server* server = view->server;
    struct wlr_seat* seat = server->seat->seat;
    struct pointer_constraints* pc = server->seat->pointer_constraints;
    struct wlr_surface* prev_surface = seat->keyboard_state.focused_surface;
    struct wlr_surface* surface = view->impl->get_surface(view);
    if (prev_surface == surface) {
        return;
    }

    if (pc->active_constraint) {
        pointer_constraint_deactivate(pc);
    }

    if (prev_surface) {
        struct wlr_xdg_toplevel* prev_toplevel =
            wlr_xdg_toplevel_try_from_wlr_surface(prev_surface);
        if (prev_toplevel != NULL) {
            wlr_xdg_toplevel_set_activated(prev_toplevel, false);
        }
    }

    struct wlr_keyboard* keyboard = wlr_seat_get_keyboard(seat);
    wlr_scene_node_raise_to_top(&view->scene_tree->node);

    pointer_constraint_set_focus(pc, view);

    view->impl->set_activated(view, true);

    if (keyboard != NULL) {
        wlr_seat_keyboard_notify_enter(seat, surface, keyboard->keycodes,
                                       keyboard->num_keycodes,
                                       &keyboard->modifiers);
    }
}

struct view* view_from_activation_surface(struct wlr_surface* surface) {
    if (!surface)
        return NULL;

    struct wlr_surface* root = wlr_surface_get_root_surface(surface);
    struct wlr_xdg_surface* xdg = wlr_xdg_surface_try_from_wlr_surface(root);

    if (xdg) {
        if (xdg->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL)
            return NULL;

        struct view* view = root->data;
        return (view && view->surface == root) ? view : NULL;
    }

    struct wlr_xwayland_surface* x11 =
        wlr_xwayland_surface_try_from_wlr_surface(root);
    if (x11) {
        struct view* view = x11->data;
        return (view && view->surface == root) ? view : NULL;
    }

    return NULL;
}

static void map_borders(struct view* view) {
    if (view->is_fullscreen)
        return;
    float color[4] = {0.6, 0.0, 0.0, 1.0};
    for (int i = 0; i < 4; i++) {
        view->borders[i] = wlr_scene_rect_create(view->scene_tree, 0, 0, color);
        wlr_scene_node_lower_to_bottom(&view->borders[i]->node);
    }
}

void view_borders_set_enabled(struct view* view, bool enabled) {
    for (int i = 0; i < 4; i++) {
        wlr_scene_node_set_enabled(&view->borders[i]->node, enabled);
    }
}

void view_set_borders(struct view* v, int w, int h) {
    if (w < 0)
        w = 0;
    if (h < 0)
        h = 0;
    struct config* config = v->server->config;
    int bw = config->border_width;

    wlr_scene_rect_set_size(v->borders[0], w, bw);
    wlr_scene_node_set_position(&v->borders[0]->node, -bw, -bw);

    wlr_scene_rect_set_size(v->borders[1], w, bw);
    wlr_scene_node_set_position(&v->borders[1]->node, -bw, h - 2 * bw);

    wlr_scene_rect_set_size(v->borders[2], bw, h);
    wlr_scene_node_set_position(&v->borders[2]->node, -bw, -bw);

    wlr_scene_rect_set_size(v->borders[3], bw, h);
    wlr_scene_node_set_position(&v->borders[3]->node, w - 2 * bw, -bw);
}

void view_map(struct view* view) {
    view->mapped = true;
    map_borders(view);
    workspace_spawn(get_active_workspace(view->server), view);

    if (!view->toplevel_handle && view->server->toplevel_list) {
        struct wlr_ext_foreign_toplevel_handle_v1_state state = {
            .title = view->impl->get_title(view),
            .app_id = view->impl->get_app_id(view)};
        view->toplevel_handle = wlr_ext_foreign_toplevel_handle_v1_create(
            view->server->toplevel_list, &state);
        if (view->toplevel_handle) {
            view->toplevel_handle->data = view;
        }
    }

    if (view->impl->want_fullscreen(view)) {
        view->impl->set_fullscreen(view, true);
    } else if (view->impl->want_maximized(view)) {
        view_set_maximized(view, true);
    }

    bool activate = view->pending_activate;
    view->pending_activate = false;

    if (activate || view->server->active_workspace == view->workspace) {
        focus_view(view);
    } else {
        // maybe mark it urgent in the future?
    }
}

void view_set_title(struct wl_listener* listener, void* data) {
    struct view* view = wl_container_of(listener, view, set_title);
    struct wlr_ext_foreign_toplevel_handle_v1* handle = view->toplevel_handle;
    (void)data;
    if (!handle)
        return;

    struct wlr_ext_foreign_toplevel_handle_v1_state new_state = {
        .title = view->impl->get_title(view) ? view->impl->get_title(view) : "",
        .app_id = handle->app_id};

    wlr_ext_foreign_toplevel_handle_v1_update_state(handle, &new_state);
}

void view_set_app_id(struct wl_listener* listener, void* data) {
    struct view* view = wl_container_of(listener, view, set_app_id);
    struct wlr_ext_foreign_toplevel_handle_v1* handle = view->toplevel_handle;
    (void)data;
    if (!handle)
        return;

    struct wlr_ext_foreign_toplevel_handle_v1_state new_state = {
        .title = handle->title,
        .app_id =
            view->impl->get_app_id(view) ? view->impl->get_app_id(view) : ""};

    wlr_ext_foreign_toplevel_handle_v1_update_state(handle, &new_state);
}

static void view_capture_source_destroy(struct wl_listener* listener,
                                        void* data) {
    struct view* view = wl_container_of(listener, view, capture_source_destroy);
    (void)data;

    wl_list_remove(&view->capture_source_destroy.link);
    wl_list_init(&view->capture_source_destroy.link);
    view->capture_source = NULL;
}

bool view_ensure_capture_source(struct view* view) {
    if (view->capture_source)
        return true;

    struct liars_server* server = view->server;
    if (!server->renderer || !server->allocator)
        return false;

    if (!view->capture_scene) {
        view->capture_scene = view->impl->create_capture_scene(view);
        if (!view->capture_scene)
            return false;
    }

    view->capture_source =
        wlr_ext_image_capture_source_v1_create_with_scene_node(
            &view->capture_scene->tree.node, server->event_loop,
            server->allocator, server->renderer);
    if (!view->capture_source) {
        wlr_log(WLR_ERROR, "Failed to create window capture source");
        return false;
    }

    view->capture_source_destroy.notify = view_capture_source_destroy;
    wl_signal_add(&view->capture_source->events.destroy,
                  &view->capture_source_destroy);
    return true;
}

void view_release_capture(struct view* view) {
    if (view->capture_scene) {
        wlr_scene_node_destroy(&view->capture_scene->tree.node);
        view->capture_scene = NULL;
    }
    view->capture_source = NULL;

    if (view->toplevel_handle) {
        wlr_ext_foreign_toplevel_handle_v1_destroy(view->toplevel_handle);
        view->toplevel_handle = NULL;
    }
}

// This does not free the view. It must be freed after
// Common destroy behavior for all views
// All behavior in this function must be safe to run by all view types
void view_destroy(struct view* view) {
    if (view->is_fullscreen) {
        workspace_unset_fullscreen(view->workspace, view);
    }
    view_release_capture(view);
    focus_view_under_cursor(view->server);
}

void view_set_floating(struct view* view, bool floating) {
    if (view->is_fullscreen)
        return; // I prefer that it does nothing

    view->is_floating = floating;
    wl_list_remove(&view->link);
    if (floating) {
        wl_list_insert(&view->workspace->views.floating, &view->link);
        wlr_scene_node_reparent(&view->scene_tree->node,
                                view->workspace->views.layer_floating);
        view_move_to_center(view);
    } else {
        wl_list_insert(&view->workspace->views.tiles, &view->link);
        wlr_scene_node_reparent(&view->scene_tree->node,
                                view->workspace->views.layer_tiled);
    }
    workspace_arrange(view->workspace);
}

void view_set_fullscreen(struct view* view, bool fullscreen) {
    if (fullscreen && view->is_maximized)
        view_set_maximized(view, false);

    if (view->is_floating)
        view_set_floating(view, false);

    view->is_fullscreen = fullscreen;

    // no need to remove the view from its previous layer I believe
    if (fullscreen) {
        workspace_set_fullscreen(view);
    } else {
        workspace_unset_fullscreen(view->workspace, view);
    }
    view_borders_set_enabled(view, !fullscreen);
    // focus_view_under_cursor(view->server); // FIX:
}

void view_set_maximized(struct view* view, bool maximized) {
    if (view->is_maximized == maximized)
        return;

    if (maximized) {
        if (view->is_fullscreen)
            view->impl->set_fullscreen(view, false);

        view->saved_geometry.x = view->scene_tree->node.x;
        view->saved_geometry.y = view->scene_tree->node.y;
        struct wlr_box geo;
        view->impl->get_geometry(view, &geo);

        struct config* config = view->server->config;
        int bw = view->is_fullscreen ? 0 : 2 * config->border_width;
        view->saved_geometry.width = geo.width + bw;
        view->saved_geometry.height = geo.height + bw;

        if (!view->is_floating)
            view_set_floating(view, true);

        view->is_maximized = true;

        struct monitor* monitor = view_get_monitor(view);
        wlr_scene_node_set_position(&view->scene_tree->node, monitor->usable.x,
                                    monitor->usable.y);
        view->impl->set_size(view, monitor->usable.width,
                             monitor->usable.height);
    } else {
        view->is_maximized = false;

        wlr_scene_node_set_position(&view->scene_tree->node,
                                    view->saved_geometry.x,
                                    view->saved_geometry.y);
        view->impl->set_size(view, view->saved_geometry.width,
                             view->saved_geometry.height);
    }

    if (view->impl->set_maximized)
        view->impl->set_maximized(view, maximized);
}
