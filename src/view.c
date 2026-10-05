#include "view.h"
#include "pointer_contraint.h"
#include "server.h"
#include "wlr/util/log.h"
#include "workspace.h"
#include <wayland-util.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>

bool is_surface_focused(struct liars_seat* seat, struct wlr_surface* surface) {
    struct wlr_surface* focused = seat->seat->keyboard_state.focused_surface;
    if (!focused || !surface) return false;
    return wlr_surface_get_root_surface(surface) == wlr_surface_get_root_surface(focused);
}

bool is_view_focused(struct view* view) {
    if (view == NULL) {
        return false;
    }
    struct wlr_surface* focused_surface = view->server->seat->seat->keyboard_state.focused_surface;
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
    wl_list_insert(&view->workspace->tiles, &view->link);

    struct wlr_pointer_constraint_v1* new_constraint = wlr_pointer_constraints_v1_constraint_for_surface(pc->constraints, surface, seat);
    if (new_constraint) {
        new_active_pointer_constraint(pc, new_constraint);
        wlr_log(WLR_INFO, "New pointer constraint for: %s", view->impl->get_title(view));
    }

    view->impl->set_activated(view, true);
        wlr_log(WLR_INFO, "Focused: %s", view->impl->get_title(view));

    if (keyboard != NULL) {
        wlr_seat_keyboard_notify_enter(seat, surface, keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
    }
}
