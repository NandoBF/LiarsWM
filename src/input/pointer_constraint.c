#include "input/input.h"
#include "input/pointer_contraint.h"
#include "server.h"
#include "view/view.h"
#include "wlr/util/log.h"
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>

// TODO: Handle cursor hints

void destroy_active_pointer_constraint(struct pointer_constraints* pc) {
    if (pc->active_constraint == NULL)
        return;

    if (pc->active_constraint->current.committed &
        WLR_POINTER_CONSTRAINT_V1_STATE_CURSOR_HINT) {
        double hint_x = pc->active_constraint->current.cursor_hint.x;
        double hint_y = pc->active_constraint->current.cursor_hint.y;

        struct view* view = pc->active_constraint->surface->data;
        if (!view) {
            wlr_log(WLR_ERROR, "No view associated with surface!");
        } else {
            double global_x = view->box.x + hint_x;
            double global_y = view->box.y + hint_y;

            cursor_warp_to(pc->seat->server->cursor, global_x, global_y);
        }
    }

    wlr_pointer_constraint_v1_send_deactivated(pc->active_constraint);
    wl_list_remove(&pc->remove_constraint.link);

    pc->active_constraint = NULL;
}

static void remove_active_constraint(struct wl_listener* listener, void* data) {
    struct pointer_constraints* pc =
        wl_container_of(listener, pc, remove_constraint);
    destroy_active_pointer_constraint(pc);
}

void new_active_pointer_constraint(
    struct pointer_constraints* pc,
    struct wlr_pointer_constraint_v1* constraint) {

    pc->active_constraint = constraint;
    pc->remove_constraint.notify = remove_active_constraint;
    wl_signal_add(&pc->active_constraint->events.destroy,
                  &pc->remove_constraint);

    wlr_pointer_constraint_v1_send_activated(pc->active_constraint);
}

static void handle_new_constraint(struct wl_listener* listener, void* data) {
    struct pointer_constraints* pc =
        wl_container_of(listener, pc, new_constraint);

    struct wlr_pointer_constraint_v1* new_constraint = data;
    if (!is_surface_focused(pc->seat, new_constraint->surface))
        return;

    new_active_pointer_constraint(pc, new_constraint);
}

struct pointer_constraints*
create_pointer_constraints(struct liars_seat* seat) {
    struct pointer_constraints* pc = calloc(1, sizeof(*pc));
    pc->seat = seat;
    pc->constraints =
        wlr_pointer_constraints_v1_create(seat->server->wl_display);

    pc->new_constraint.notify = handle_new_constraint;
    wl_signal_add(&pc->constraints->events.new_constraint, &pc->new_constraint);

    pc->relative_pointer_manager =
        wlr_relative_pointer_manager_v1_create(seat->server->wl_display);

    return pc;
}
