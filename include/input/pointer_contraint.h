#pragma once
#include <wayland-server-core.h>

struct liars_seat;
struct liars_server;
struct view;

struct pointer_constraints {
    struct liars_seat* seat;

    struct wlr_pointer_constraints_v1* constraints;
    struct wl_listener new_constraint;

    struct wlr_pointer_constraint_v1* active_constraint;
    struct wl_listener remove_constraint;
    struct wlr_pointer_constraint_v1* dying;

    struct wlr_relative_pointer_manager_v1* relative_pointer_manager;
};

void pointer_constraint_set_focus(struct pointer_constraints* pc,
                                  struct view* view);

void pointer_constraint_deactivate(struct pointer_constraints* pc);
struct pointer_constraints* create_pointer_constraints(struct liars_seat* seat);
