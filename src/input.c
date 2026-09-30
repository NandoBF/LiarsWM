#include "input.h"
#include "server.h"
#include "view.h"
#include "wlr/util/log.h"
#include "xdg_view.h"
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-util.h>
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
        wlr_cursor_set_surface(seat->server->cursor->cursor, event->surface, event->hotspot_x, event->hotspot_y);
    }
}

static void seat_pointer_focus_change(struct wl_listener* listener, void* data) {
    struct liars_seat* seat = wl_container_of(listener, seat, pointer_focus_change);
    struct liars_cursor* cursor = seat->server->cursor;

    struct wlr_seat_pointer_focus_change_event* event = data;
    if (event->new_surface == NULL) {
        wlr_cursor_set_xcursor(cursor->cursor, cursor->cursor_manager, "default");
    }
}

static void seat_request_set_selection(struct wl_listener* listener, void* data) {
    struct liars_seat* seat = wl_container_of(listener, seat, request_set_selection);
    struct wlr_seat_request_set_selection_event* event = data;
    wlr_seat_set_selection(seat->seat, event->source, event->serial);
}

struct liars_seat* create_seat(struct liars_server* server) {
    struct liars_seat* seat = calloc(1, sizeof(*seat)); 
    seat->server = server;
    seat->seat = wlr_seat_create(server->wl_display, "seat0");
    wl_list_init(&seat->keyboards);

    seat->request_cursor.notify = seat_request_cursor;
    wl_signal_add(&seat->seat->events.request_set_cursor, &seat->request_cursor);

    seat->pointer_focus_change.notify = seat_pointer_focus_change;
    wl_signal_add(&seat->seat->pointer_state.events.focus_change, &seat->pointer_focus_change);

    seat->request_set_selection.notify = seat_request_set_selection;
    wl_signal_add(&seat->seat->events.request_set_selection, &seat->request_set_selection);

    return seat;
}


static void keyboard_handle_modifiers(
        struct wl_listener* listener, void* data) {
    struct liars_keyboard* keyboard = wl_container_of(listener, keyboard, modifiers);

    wlr_seat_set_keyboard(keyboard->seat->seat, keyboard->wlr_keyboard);
    wlr_seat_keyboard_notify_modifiers(keyboard->seat->seat, &keyboard->wlr_keyboard->modifiers);
}

static void keyboard_handle_key(struct wl_listener* listener, void* data) {
    struct liars_keyboard* keyboard =
        wl_container_of(listener, keyboard, key);
    struct liars_seat* seat = keyboard->seat;
    struct liars_server* server = seat->server;
    struct wlr_keyboard_key_event* event = data;

    uint32_t keycode = event->keycode + 8;

    const xkb_keysym_t* syms;
    int nsyms = xkb_state_key_get_syms(keyboard->wlr_keyboard->xkb_state, keycode, &syms);
    bool handled = false;
    uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);
    if ((modifiers & WLR_MODIFIER_ALT) &&
            event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        for (int i = 0; i < nsyms; i++) {
            // TODO: Handle keybinds
            wlr_log(WLR_DEBUG, "Keybinding pressed");
        }

    }

    if (!handled) {
        wlr_seat_set_keyboard(seat->seat, keyboard->wlr_keyboard);
        wlr_seat_keyboard_notify_key(seat->seat, event->time_msec, event->keycode, event->state);
    }
}

static void keyboard_handle_destroy(struct wl_listener* listener, void* data) {
    struct liars_keyboard* keyboard = wl_container_of(listener, keyboard, destroy);
    wl_list_remove(&keyboard->modifiers.link);
    wl_list_remove(&keyboard->key.link);
    wl_list_remove(&keyboard->destroy.link);
    wl_list_remove(&keyboard->link);
    free(keyboard);
}

struct liars_keyboard* create_keyboard(struct liars_seat* seat, struct wlr_input_device* device) {
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


