#include "input.h"
#include "command.h"
#include "pointer_contraint.h"
#include "server.h"
#include "view.h"
#include "config.h"
#include "wlr/util/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-util.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <xkbcommon/xkbcommon.h>

void reset_cursor_mode(struct liars_cursor* cursor) {
    cursor->cursor_mode = LIARSWM_CURSOR_PASSTHROUGH;
    cursor->grabbed_view = NULL;
}

static void seat_request_cursor(struct wl_listener* listener, void* data) {
    struct liars_seat* seat = wl_container_of(listener, seat, request_cursor);

    struct wlr_seat_pointer_request_set_cursor_event* event = data;
    struct wlr_seat_client* focused_client =
        seat->seat->pointer_state.focused_client;

    if (focused_client == event->seat_client) {
        wlr_cursor_set_surface(seat->server->cursor->cursor, event->surface,
                               event->hotspot_x, event->hotspot_y);
    }
}

static void seat_pointer_focus_change(struct wl_listener* listener,
                                      void* data) {
    struct liars_seat* seat =
        wl_container_of(listener, seat, pointer_focus_change);
    struct liars_cursor* cursor = seat->server->cursor;

    struct wlr_seat_pointer_focus_change_event* event = data;
    if (event->new_surface == NULL) {
        wlr_cursor_set_xcursor(cursor->cursor, cursor->cursor_manager,
                               "default");
    }
}

static void seat_request_set_selection(struct wl_listener* listener,
                                       void* data) {
    struct liars_seat* seat =
        wl_container_of(listener, seat, request_set_selection);
    struct wlr_seat_request_set_selection_event* event = data;
    wlr_seat_set_selection(seat->seat, event->source, event->serial);
}

static void seat_new_keyboard(struct liars_seat* seat,
                              struct wlr_input_device* device) {

    struct liars_keyboard* keyboard = create_keyboard(seat, device);

    struct xkb_context* context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    struct xkb_keymap* keymap =
        xkb_keymap_new_from_names(context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
    wlr_keyboard_set_keymap(keyboard->wlr_keyboard, keymap);
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);
    wlr_keyboard_set_repeat_info(keyboard->wlr_keyboard, 25, 600);
}

static void seat_new_pointer(struct liars_cursor* cursor,
                             struct wlr_input_device* device) {
    wlr_cursor_attach_input_device(cursor->cursor, device);
}

static void seat_new_input(struct wl_listener* listener, void* data) {
    struct liars_seat* seat = wl_container_of(listener, seat, new_input);
    struct liars_server* server = seat->server;
    struct wlr_input_device* device = data;
    switch (device->type) {
    case WLR_INPUT_DEVICE_KEYBOARD:
        seat_new_keyboard(seat, device);
        break;
    case WLR_INPUT_DEVICE_POINTER:
        seat_new_pointer(server->cursor, device);
        break;
    default:
        break;
    }

    uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
    if (!wl_list_empty(&seat->keyboards)) {
        caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    }
    wlr_seat_set_capabilities(seat->seat, caps);
}

struct liars_seat* create_seat(struct liars_server* server) {
    struct liars_seat* seat = calloc(1, sizeof(*seat));
    seat->server = server;
    seat->seat = wlr_seat_create(server->wl_display, "seat0");
    wl_list_init(&seat->keyboards);

    seat->request_cursor.notify = seat_request_cursor;
    wl_signal_add(&seat->seat->events.request_set_cursor,
                  &seat->request_cursor);

    seat->pointer_focus_change.notify = seat_pointer_focus_change;
    wl_signal_add(&seat->seat->pointer_state.events.focus_change,
                  &seat->pointer_focus_change);

    seat->request_set_selection.notify = seat_request_set_selection;
    wl_signal_add(&seat->seat->events.request_set_selection,
                  &seat->request_set_selection);

    seat->new_input.notify = seat_new_input;
    wl_signal_add(&server->backend->events.new_input, &seat->new_input);

    seat->pointer_constraints = create_pointer_constraints(seat);

    return seat;
}

static void keyboard_handle_modifiers(struct wl_listener* listener,
                                      void* data) {
    struct liars_keyboard* keyboard =
        wl_container_of(listener, keyboard, modifiers);

    wlr_seat_set_keyboard(keyboard->seat->seat, keyboard->wlr_keyboard);
    wlr_seat_keyboard_notify_modifiers(keyboard->seat->seat,
                                       &keyboard->wlr_keyboard->modifiers);
}

static void keyboard_handle_key(struct wl_listener* listener, void* data) {
    struct liars_keyboard* keyboard = wl_container_of(listener, keyboard, key);
    struct liars_seat* seat = keyboard->seat;
    struct liars_server* server = seat->server;
    struct config* config = server->config;
    struct wlr_keyboard_key_event* event = data;

    uint32_t keycode = event->keycode + 8;

    const xkb_keysym_t* syms;
    int nsyms = xkb_state_key_get_syms(keyboard->wlr_keyboard->xkb_state,
                                       keycode, &syms);
    bool handled = false;
    uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);

    // mask off CAPS and NUM_LOCK. They break the modifier if turned on
    uint32_t masked_modifiers = modifiers & ~WLR_MODIFIER_MOD2 & ~WLR_MODIFIER_CAPS;

    for (int i = 0; i < nsyms; i++) {
        xkb_keysym_t sym = xkb_keysym_to_lower(syms[i]);
        for (int j = 0; j < config->keybind_num; j++) {
            xkb_keysym_t bind_sym = xkb_keysym_to_lower(config->keybinds[j]->keysym);
            if (masked_modifiers == config->keybinds[j]->modifiers &&
                sym == bind_sym &&
                event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
                
                wlr_log(WLR_DEBUG, "Keybinding matched!");
                execute_keybind(server, config->keybinds[j]);
                handled = true;
            }
        }
    }

    if (!handled) {
        wlr_seat_set_keyboard(seat->seat, keyboard->wlr_keyboard);
        wlr_seat_keyboard_notify_key(seat->seat, event->time_msec,
                                     event->keycode, event->state);
    }
}

static void keyboard_handle_destroy(struct wl_listener* listener, void* data) {
    struct liars_keyboard* keyboard =
        wl_container_of(listener, keyboard, destroy);
    wl_list_remove(&keyboard->modifiers.link);
    wl_list_remove(&keyboard->key.link);
    wl_list_remove(&keyboard->destroy.link);
    wl_list_remove(&keyboard->link);
    free(keyboard);
}

struct liars_keyboard* create_keyboard(struct liars_seat* seat,
                                       struct wlr_input_device* device) {
    struct liars_keyboard* keyboard = calloc(1, sizeof(*keyboard));
    struct wlr_keyboard* wlr_k = wlr_keyboard_from_input_device(device);

    keyboard->seat = seat;
    keyboard->wlr_keyboard = wlr_k;

    keyboard->modifiers.notify = keyboard_handle_modifiers;
    wl_signal_add(&wlr_k->events.modifiers, &keyboard->modifiers);

    keyboard->key.notify = keyboard_handle_key;
    wl_signal_add(&wlr_k->events.key, &keyboard->key);

    keyboard->destroy.notify = keyboard_handle_destroy;
    wl_signal_add(&device->events.destroy, &keyboard->destroy);

    wlr_seat_set_keyboard(keyboard->seat->seat, wlr_k);

    wl_list_insert(&seat->keyboards, &keyboard->link);
    return keyboard;
}

// Cursor

static void process_cursor_move(struct liars_cursor* cursor) {
    struct view* view = cursor->grabbed_view;
    wlr_scene_node_set_position(&view->scene_tree->node,
                                cursor->cursor->x - cursor->grab_x,
                                cursor->cursor->y - cursor->grab_y);
}

static void process_cursor_resize(struct liars_cursor* cursor) {
    struct view* view = cursor->grabbed_view;
    double border_x = cursor->cursor->x - cursor->grab_x;
    double border_y = cursor->cursor->y - cursor->grab_y;
    int new_left = cursor->grab_geobox.x;
    int new_right = cursor->grab_geobox.x + cursor->grab_geobox.width;
    int new_top = cursor->grab_geobox.y;
    int new_bottom = cursor->grab_geobox.y + cursor->grab_geobox.height;

    if (cursor->resize_edges & WLR_EDGE_TOP) {
        new_top = border_y;
        if (new_top >= new_bottom) {
            new_top = new_bottom - 1;
        }
    } else if (cursor->resize_edges & WLR_EDGE_BOTTOM) {
        new_bottom = border_y;
        if (new_bottom <= new_top) {
            new_bottom = new_top + 1;
        }
    }
    if (cursor->resize_edges & WLR_EDGE_LEFT) {
        new_left = border_x;
        if (new_left >= new_right) {
            new_left = new_right - 1;
        }
    } else if (cursor->resize_edges & WLR_EDGE_RIGHT) {
        new_right = border_x;
        if (new_right <= new_left) {
            new_right = new_left + 1;
        }
    }

    struct wlr_box geo_box;
    view->impl->get_geometry(view, &geo_box);
    wlr_scene_node_set_position(&view->scene_tree->node, new_left - geo_box.x,
                                new_top - geo_box.y);

    int new_width = new_right - new_left;
    int new_height = new_bottom - new_top;
    view->impl->set_size(view, new_width, new_height);
}

static void process_cursor_motion(struct liars_cursor* cursor, uint32_t time) {
    if (cursor->cursor_mode == LIARSWM_CURSOR_MOVE) {
        process_cursor_move(cursor);
        return;
    } else if (cursor->cursor_mode == LIARSWM_CURSOR_RESIZE) {
        process_cursor_resize(cursor);
        return;
    }

    double sx, sy;
    struct wlr_seat* seat = cursor->seat->seat;
    struct wlr_surface* surface = NULL;
    struct view* view = desktop_view_at(cursor->seat->server, cursor->cursor->x,
                                        cursor->cursor->y, &surface, &sx, &sy);

    if (!view) {
        wlr_cursor_set_xcursor(cursor->cursor, cursor->cursor_manager,
                               "default");
    }

    if (surface) {
        wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
        wlr_seat_pointer_notify_motion(seat, time, sx, sy);
    } else {
        wlr_seat_pointer_clear_focus(seat);
    }
}

static void server_cursor_motion(struct wl_listener* listener, void* data) {
    struct liars_cursor* cursor = wl_container_of(listener, cursor, motion);
    struct wlr_pointer_motion_event* event = data;

    wlr_relative_pointer_manager_v1_send_relative_motion(
        cursor->seat->pointer_constraints->relative_pointer_manager,
        cursor->seat->seat, (uint64_t)event->time_msec * 1000, event->delta_x,
        event->delta_y, event->unaccel_dx, event->unaccel_dy);

    struct wlr_pointer_constraint_v1* constraint =
        cursor->seat->pointer_constraints->active_constraint;
    if (!constraint || constraint->type != WLR_POINTER_CONSTRAINT_V1_LOCKED) {
        wlr_cursor_move(cursor->cursor, &event->pointer->base, event->delta_x,
                        event->delta_y);
    }
    process_cursor_motion(cursor, event->time_msec);
}

static void server_cursor_motion_absolute(struct wl_listener* listener,
                                          void* data) {
    struct liars_cursor* cursor =
        wl_container_of(listener, cursor, motion_absolute);
    struct wlr_pointer_motion_absolute_event* event = data;

    struct wlr_pointer_constraint_v1* constraint =
        cursor->seat->pointer_constraints->active_constraint;
    if (!constraint || constraint->type != WLR_POINTER_CONSTRAINT_V1_LOCKED) {
        wlr_cursor_warp_absolute(cursor->cursor, &event->pointer->base,
                                 event->x, event->y);
    }
    process_cursor_motion(cursor, event->time_msec);
}

static void server_cursor_button(struct wl_listener* listener, void* data) {
    struct liars_cursor* cursor = wl_container_of(listener, cursor, button);
    struct liars_server* server = cursor->seat->server;
    struct wlr_pointer_button_event* event = data;

    wlr_seat_pointer_notify_button(cursor->seat->seat, event->time_msec,
                                   event->button, event->state);
    if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
        reset_cursor_mode(cursor);
    } else {
        double sx, sy;
        struct wlr_surface* surface = NULL;
        struct view* view = desktop_view_at(
            server, cursor->cursor->x, cursor->cursor->y, &surface, &sx, &sy);
        focus_view(view);
    }
}

static void server_cursor_axis(struct wl_listener* listener, void* data) {
    struct liars_cursor* cursor = wl_container_of(listener, cursor, axis);
    struct wlr_pointer_axis_event* event = data;

    wlr_seat_pointer_notify_axis(
        cursor->seat->seat, event->time_msec, event->orientation, event->delta,
        event->delta_discrete, event->source, event->relative_direction);
}

static void server_cursor_frame(struct wl_listener* listener, void* data) {
    struct liars_cursor* cursor = wl_container_of(listener, cursor, frame);
    wlr_seat_pointer_notify_frame(cursor->seat->seat);
}

struct liars_cursor* create_cursor(struct liars_seat* seat,
                                   struct wlr_output_layout* output_layout) {
    struct liars_cursor* cursor = calloc(1, sizeof(*cursor));
    cursor->seat = seat;
    cursor->cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(cursor->cursor, output_layout);

    cursor->cursor_manager = wlr_xcursor_manager_create(NULL, 24);
    cursor->cursor_mode = LIARSWM_CURSOR_PASSTHROUGH;

    cursor->motion.notify = server_cursor_motion;
    wl_signal_add(&cursor->cursor->events.motion, &cursor->motion);

    cursor->motion_absolute.notify = server_cursor_motion_absolute;
    wl_signal_add(&cursor->cursor->events.motion_absolute,
                  &cursor->motion_absolute);

    cursor->button.notify = server_cursor_button;
    wl_signal_add(&cursor->cursor->events.button, &cursor->button);

    cursor->axis.notify = server_cursor_axis;
    wl_signal_add(&cursor->cursor->events.axis, &cursor->axis);

    cursor->frame.notify = server_cursor_frame;
    wl_signal_add(&cursor->cursor->events.frame, &cursor->frame);

    return cursor;
}
