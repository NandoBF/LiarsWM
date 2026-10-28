#include "view/view.h"
#include "config.h"
#include "input/input.h"
#include "input/pointer_contraint.h"
#include "server.h"
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
    return focused_surface == view->surface;
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
        destroy_active_pointer_constraint(pc);
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
    wl_list_remove(&view->link);
    wl_list_insert(&view->workspace->views.tiles, &view->link);

    struct wlr_pointer_constraint_v1* new_constraint =
        wlr_pointer_constraints_v1_constraint_for_surface(pc->constraints,
                                                          surface, seat);
    if (new_constraint) {
        new_active_pointer_constraint(pc, new_constraint);
        wlr_log(WLR_INFO, "New pointer constraint for: %s",
                view->impl->get_title(view));
    }

    view->impl->set_activated(view, true);
    wlr_log(WLR_DEBUG, "Focused: %s", view->impl->get_title(view));

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
    wlr_scene_rect_set_size(v->borders[0], w, config->border_width);
    wlr_scene_node_set_position(&v->borders[0]->node, 0, 0);

    wlr_scene_rect_set_size(v->borders[1], w, config->border_width);
    wlr_scene_node_set_position(&v->borders[1]->node, 0,
                                h - config->border_width);

    wlr_scene_rect_set_size(v->borders[2], config->border_width, h);
    wlr_scene_node_set_position(&v->borders[2]->node, 0, 0);

    wlr_scene_rect_set_size(v->borders[3], config->border_width, h);
    wlr_scene_node_set_position(&v->borders[3]->node, w - config->border_width,
                                0);
}

void view_map(struct view* view) {
    view->mapped = true;
    map_borders(view);
    workspace_spawn(get_active_workspace(view->server), view);

    focus_view(view);
}

// This does not free the view. It must be freed after
void view_destroy(struct view* view) {

    if (view->commit.link.next != NULL) {
        wl_list_remove(&view->commit.link);
    }

    if (view->destroy.link.next != NULL) {
        wl_list_remove(&view->destroy.link);
    }

    if (view->request_move.link.next != NULL) {
        wl_list_remove(&view->request_move.link);
    }

    if (view->request_resize.link.next != NULL) {
        wl_list_remove(&view->request_resize.link);
    }

    if (view->request_maximize.link.prev != NULL) {
        wl_list_remove(&view->request_maximize.link);
    }

    if (view->request_fullscreen.link.prev != NULL) {
        wl_list_remove(&view->request_fullscreen.link);
    }

    wlr_scene_node_destroy(&view->scene_tree->node);
}

void view_set_floating(struct view* view, bool floating) {
    view->is_floating = floating;
    wl_list_remove(&view->link);
    if (floating) {
        wl_list_insert(&view->workspace->views.floating, &view->link);
        wlr_scene_node_reparent(&view->scene_tree->node,
                                view->workspace->views.layer_floating);
    } else {
        wl_list_insert(&view->workspace->views.tiles, &view->link);
        wlr_scene_node_reparent(&view->scene_tree->node,
                                view->workspace->views.layer_tiled);
    }
}

// TAG: Fullscreen
void view_set_fullscreen(struct view* view, bool fullscreen) {
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
