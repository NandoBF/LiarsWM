#include "view/view.h"
#include "config.h"
#include "input/input.h"
#include "input/pointer_contraint.h"
#include "monitor.h"
#include "windows/layout.h"
#include "windows/workspace.h"
#include "wlr/util/log.h"
#include <stdlib.h>
#include <wayland-util.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>

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
    // wl_list_remove(&view->link); ??? What was this here for
    // wl_list_insert(&view->workspace->views.tiles, &view->link);

    pointer_constraint_set_focus(pc, view);

    view->impl->set_activated(view, true);

    if (keyboard != NULL) {
        wlr_seat_keyboard_notify_enter(seat, surface, keyboard->keycodes,
                                       keyboard->num_keycodes,
                                       &keyboard->modifiers);
    }
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

    wlr_scene_rect_set_size(v->borders[3], config->border_width, h);
    wlr_scene_node_set_position(&v->borders[3]->node, w - 2 * bw, -bw);
}

void view_map(struct view* view) {
    view->mapped = true;
    map_borders(view);
    workspace_spawn(get_active_workspace(view->server), view);

    focus_view(view);
}

// This does not free the view. It must be freed after
// Common destroy behavior for all views
// All behavior in this function must be safe to run by all view types
void view_destroy(struct view* view) {
    if (view->is_fullscreen) {
        workspace_unset_fullscreen(view->workspace, view);
    }
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
