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

static void warp_to_hint(struct pointer_constraints* pc,
                         struct wlr_pointer_constraint_v1* c) {
    if (c->type != WLR_POINTER_CONSTRAINT_V1_LOCKED)
        return;
    if (!(c->current.committed & WLR_POINTER_CONSTRAINT_V1_STATE_CURSOR_HINT))
        return;

    struct view* view = c->surface->data;
    if (!view)
        return;

    cursor_warp_to(pc->seat->server->cursor,
                   view->box.x + c->current.cursor_hint.x,
                   view->box.y + c->current.cursor_hint.y);
}

static void detach_active_constraint(struct pointer_constraints* pc) {
    wl_list_remove(&pc->remove_constraint.link);
    pc->active_constraint = NULL;
}

static void pointer_constraint_destroy(struct wl_listener* listener,
                                       void* data) {
    struct pointer_constraints* pc =
        wl_container_of(listener, pc, remove_constraint);
    warp_to_hint(pc, pc->active_constraint);
    detach_active_constraint(
        pc); // no need to send deactive since it dies by itself
}

void pointer_constraint_deactivate(struct pointer_constraints* pc) {
    struct wlr_pointer_constraint_v1* c = pc->active_constraint;
    if (!c)
        return;

    warp_to_hint(pc, c);
    detach_active_constraint(pc); // send_deactivated may destroy the contraint
                                  // so detach before calling it
    wlr_pointer_constraint_v1_send_deactivated(c);
}

static void pointer_constraint_activate(struct pointer_constraints* pc,
                                        struct wlr_pointer_constraint_v1* c) {
    if (pc->active_constraint == c)
        return;
    pointer_constraint_deactivate(pc);

    pc->active_constraint = c;
    pc->remove_constraint.notify = pointer_constraint_destroy;
    wl_signal_add(&c->events.destroy, &pc->remove_constraint);
    wlr_pointer_constraint_v1_send_activated(c);
}

void pointer_constraint_set_focus(struct pointer_constraints* pc,
                                  struct view* view) {
    struct wlr_pointer_constraint_v1* c = NULL;
    if (view && view->surface) {
        c = wlr_pointer_constraints_v1_constraint_for_surface(
            pc->constraints, view->surface, pc->seat->seat);
    }
    if (c)
        pointer_constraint_activate(pc, c);
    else
        pointer_constraint_deactivate(pc);
}

static void handle_new_constraint(struct wl_listener* listener, void* data) {
    struct pointer_constraints* pc =
        wl_container_of(listener, pc, new_constraint);
    struct wlr_pointer_constraint_v1* c = data;

    if (is_surface_focused(pc->seat, c->surface)) {
        pointer_constraint_activate(pc, c);
    }
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
