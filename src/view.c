#include "view.h"
#include "server.h"
#include "workspace.h"
#include <wayland-util.h>



void focus_view(struct view* view) {
    if (view == NULL) {
        return;
    }

    struct liars_server* server = view->server;
    struct wlr_seat* seat = server->seat->seat;
    struct wlr_surface* prev_surface = seat->keyboard_state.focused_surface;
    struct wlr_surface* surface = view->impl->get_surface(view);
    if (prev_surface == surface) {
        return;
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

    view->impl->set_activated(view, true);

    if (keyboard != NULL) {
        wlr_seat_keyboard_notify_enter(seat, surface, keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
    }
}
