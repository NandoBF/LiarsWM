#pragma once

#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-util.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_touch.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>

enum liars_cursor_mode {
    LIARSWM_CURSOR_PASSTHROUGH,
    LIARSWM_CURSOR_MOVE,
    LIARSWM_CURSOR_RESIZE,
};

struct liars_seat {
    struct wlr_seat* seat;
    struct liars_server* server;
    struct pointer_constraints* pointer_constraints;

    struct wl_list keyboards;
    struct wl_listener new_input;

    struct wl_listener request_cursor;
    struct wl_listener request_set_selection;
    struct wl_listener request_set_primary_selection;
    struct wl_listener request_start_drag;
    struct wl_listener start_drag;
    struct wl_listener pointer_focus_change;
};

struct liars_cursor {
    struct wlr_cursor* cursor;
    enum liars_cursor_mode cursor_mode;
    struct wlr_xcursor_manager* cursor_manager;
    struct liars_seat* seat;

    struct view* grabbed_view;
    double grab_x, grab_y;
    struct wlr_box grab_geobox;
    uint32_t resize_edges;

    struct wl_listener motion;
    struct wl_listener motion_absolute;
    struct wl_listener button;
    struct wl_listener axis;
    struct wl_listener frame;
};

struct liars_keyboard {
    struct wl_list link;
    struct liars_seat* seat;
    struct wlr_keyboard* wlr_keyboard;

    struct wl_listener modifiers;
    struct wl_listener key;
    struct wl_listener destroy;
};

struct liars_seat* create_seat(struct liars_server* server);
struct liars_keyboard* create_keyboard(struct liars_seat* seat,
                                       struct wlr_input_device* device);

struct liars_cursor* create_cursor(struct liars_seat* seat,
                                   struct wlr_output_layout* output_layout);

void reset_cursor_mode(struct liars_cursor* cursor);
void begin_interactive(struct view* view, enum liars_cursor_mode mode,
                       uint32_t edges);

struct wlr_surface* seat_focused_surface(struct liars_seat* seat);

struct wlr_surface* wlr_seat_focused_surface(struct wlr_seat* seat);
