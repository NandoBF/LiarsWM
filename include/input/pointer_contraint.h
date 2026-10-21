#pragma once
#include <wayland-server-core.h>

struct liars_seat;
struct liars_server;

struct pointer_constraints {
    struct liars_seat* seat;

    struct wlr_pointer_constraints_v1* constraints;
    struct wl_listener new_constraint;

    struct wlr_pointer_constraint_v1* active_constraint;
    struct wl_listener remove_constraint;

    struct wlr_relative_pointer_manager_v1* relative_pointer_manager;
};

void destroy_active_pointer_constraint(struct pointer_constraints* pc);
void new_active_pointer_constraint(
    struct pointer_constraints* pc,
    struct wlr_pointer_constraint_v1* constraint);
struct pointer_constraints* create_pointer_constraints(struct liars_seat* seat);
