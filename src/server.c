#include "server.h"
#include "input.h"
#include "view.h"
#include "wlr/util/edges.h"
#include "wlr/util/log.h"
#include <bits/time.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-util.h>
#include <xkbcommon/xkbcommon.h>
#include "workspace.h"
#include "xdg_view.h"


struct workspace* get_active_workspace(struct liars_server* server) {
    return server->active_workspace; 
}

static void output_destroy(struct wl_listener* listener, void* data){
    struct monitor* monitor = wl_container_of(listener, monitor, destroy);

    wl_list_remove(&monitor->frame.link);
    wl_list_remove(&monitor->request_state.link);
    wl_list_remove(&monitor->destroy.link);
    wl_list_remove(&monitor->link);
    free(monitor);
}


// Called when the backend requests a new state for the output.
static void output_request_state(struct wl_listener* listener, void* data){
    struct monitor* monitor = wl_container_of(listener, monitor, request_state);
    const struct wlr_output_event_request_state* event = data;
    wlr_output_commit_state(monitor->output, event->state);
}


// Called every time an output is ready to display a frame
static void output_frame(struct wl_listener* listener, void* data) {
    struct monitor* monitor = wl_container_of(listener, monitor, frame);
    struct wlr_scene_output* scene_output = monitor->scene_output;

    wlr_scene_output_commit(scene_output, NULL);
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(scene_output, &now);
}


static void server_new_output(struct wl_listener* listener, void* data) {
    struct liars_server* server = wl_container_of(listener, server, new_output);
    struct monitor* monitor = calloc(1, sizeof(*monitor));
    if (monitor == NULL) {
        wlr_log(WLR_ERROR, "Monitor Calloc return null");
        return;
    }
    monitor->output = data;
    monitor->server = server;

    wlr_output_init_render(monitor->output, server->allocator, server->renderer);

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);

    struct wlr_output_mode* mode = wlr_output_preferred_mode(monitor->output);
    if (mode != NULL) {
        wlr_output_state_set_mode(&state, mode);
    }

    wlr_output_commit_state(monitor->output, &state);
    wlr_output_state_finish(&state);

    monitor->frame.notify = output_frame;
    wl_signal_add(&monitor->output->events.frame, &monitor->frame);

    monitor->request_state.notify = output_request_state;
    wl_signal_add(&monitor->output->events.request_state, &monitor->request_state);

    monitor->destroy.notify = output_destroy;
    wl_signal_add(&monitor->output->events.destroy, &monitor->destroy);

    wl_list_insert(&server->monitors, &monitor->link);

    struct wlr_output_layout_output* l_output = wlr_output_layout_add_auto(server->output_layout, monitor->output);
    struct wlr_scene_output* scene_output = wlr_scene_output_create(server->scene, monitor->output);
    wlr_scene_output_layout_add_output(server->scene_layout, l_output, scene_output);
    monitor->scene_output = scene_output;

    if (wl_list_length(&server->monitors) == 1) {
        struct column* col = column_get_or_create(server, monitor, "1");

        server->active_workspace = wl_container_of(col->levels.next, (struct workspace*)NULL, link);
    }


}

static struct view* desktop_view_at(
        struct liars_server* server, double lx, double ly,
        struct wlr_surface** surface, double* sx, double* sy) {

    struct wlr_scene_node* node = wlr_scene_node_at(&server->scene->tree.node, lx, ly, sx, sy);

    if (node == NULL || node->type != WLR_SCENE_NODE_BUFFER) {
         return NULL;
    }

    struct wlr_scene_buffer* scene_buffer = wlr_scene_buffer_from_node(node);
    struct wlr_scene_surface* scene_surface =
        wlr_scene_surface_try_from_buffer(scene_buffer);
    if (!scene_surface) return NULL;

    *surface = scene_surface->surface;

    struct wlr_scene_tree* tree = node->parent;
    while (tree != NULL && tree->node.data == NULL) {
        tree = tree->node.parent;
    }
    if (tree == NULL) {
        return NULL;
    }

    return tree->node.data;
}

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
    wlr_scene_node_set_position(&view->scene_tree->node, 
            new_left - geo_box.x,
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
    struct view* view = desktop_view_at(cursor->seat->server, 
            cursor->cursor->x, cursor->cursor->y, &surface, &sx, &sy);

    if (!view) {
        wlr_cursor_set_xcursor(cursor->cursor, cursor->cursor_manager, "default");
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

    wlr_cursor_move(cursor->cursor, &event->pointer->base,
            event->delta_x, event->delta_y);
    process_cursor_motion(cursor, event->time_msec);
}

static void server_cursor_motion_absolute(struct wl_listener* listener, void* data) {
    struct liars_cursor* cursor = wl_container_of(listener, cursor, motion_absolute);
    struct wlr_pointer_motion_absolute_event* event = data;
    wlr_cursor_warp_absolute(cursor->cursor, &event->pointer->base, event->x, event->y);
    process_cursor_motion(cursor, event->time_msec);
}

static void server_cursor_button(struct wl_listener* listener, void* data) {
    struct liars_cursor* cursor = wl_container_of(listener, cursor, button);
    struct liars_server* server = cursor->seat->server;
    struct wlr_pointer_button_event* event = data;

    wlr_seat_pointer_notify_button(cursor->seat->seat, event->time_msec, event->button, event->state);
    if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
        reset_cursor_mode(cursor);
    } else {
        double sx, sy;
        struct wlr_surface* surface = NULL;
        struct view* view = desktop_view_at(server, cursor->cursor->x, cursor->cursor->y, &surface, &sx, &sy);
        focus_view(view);
    }
}

static void server_cursor_axis(struct wl_listener* listener, void* data) {
    struct liars_cursor* cursor = wl_container_of(listener, cursor, axis);
    struct wlr_pointer_axis_event* event = data;

    wlr_seat_pointer_notify_axis(cursor->seat->seat,
            event->time_msec, event->orientation, event->delta,
            event->delta_discrete, event->source, event->relative_direction);
}

static void server_cursor_frame(struct wl_listener* listener, void* data) {
    struct liars_cursor* cursor = wl_container_of(listener, cursor, frame);
    wlr_seat_pointer_notify_frame(cursor->seat->seat);
}


static void server_new_keyboard(struct liars_server* server, struct wlr_input_device* device) {

    struct liars_keyboard* keyboard = create_keyboard(server->seat, device); 

    struct xkb_context* context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    struct xkb_keymap* keymap = xkb_keymap_new_from_names(context, NULL,
            XKB_KEYMAP_COMPILE_NO_FLAGS);
    wlr_keyboard_set_keymap(keyboard->wlr_keyboard, keymap);
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);
    wlr_keyboard_set_repeat_info(keyboard->wlr_keyboard, 25, 600);
}

static void server_new_pointer(struct liars_server* server, struct wlr_input_device* device) {
    wlr_cursor_attach_input_device(server->cursor->cursor, device);
}


static void server_new_input(struct wl_listener* listener, void* data) {
    struct liars_seat* seat = wl_container_of(listener, seat, new_input);
    struct liars_server* server = seat->server;
    struct wlr_input_device* device = data;
    switch (device->type) {
        case WLR_INPUT_DEVICE_KEYBOARD:
            server_new_keyboard(server, device);
            break;
        case WLR_INPUT_DEVICE_POINTER:
            server_new_pointer(server, device);
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

int init_liar_server(struct liars_server* server, enum wlr_log_importance log_level) {

    server->wl_display = wl_display_create();
    server->event_loop = wl_display_get_event_loop(server->wl_display);
    wlr_log_init(log_level, NULL);

    server->backend = wlr_backend_autocreate(server->event_loop, NULL);
    if (server->backend == NULL) {
        wlr_log(WLR_ERROR, "Could not start backend"); 
        return 1;
    }

    server->renderer = wlr_renderer_autocreate(server->backend);
    if (server->renderer == NULL) {
        wlr_log(WLR_ERROR, "Could not start renderer");
        return 2;
    }
    wlr_renderer_init_wl_display(server->renderer, server->wl_display);

    server->allocator = wlr_allocator_autocreate(server->backend, server->renderer);
    if (server->allocator == NULL) {
        wlr_log(WLR_ERROR, "Could not start allocator");
        return 3;
    }

    wlr_compositor_create(server->wl_display, 5, server->renderer);
    wlr_subcompositor_create(server->wl_display);
    wlr_data_device_manager_create(server->wl_display);

    wl_list_init(&server->monitors);
    wl_list_init(&server->columns);

    server->output_layout = wlr_output_layout_create(server->wl_display);

    server->new_output.notify = server_new_output;
    wl_signal_add(&server->backend->events.new_output, &server->new_output);
    server->scene = wlr_scene_create();
    server->scene_layout = wlr_scene_attach_output_layout(server->scene, server->output_layout);

    //FIX:

    // init toplevels?
    server->xdg_shell = wlr_xdg_shell_create(server->wl_display, 3);
    server->new_xdg_toplevel.notify = server_new_xdg_toplevel;
    wl_signal_add(&server->xdg_shell->events.new_toplevel, &server->new_xdg_toplevel);
    // Seat
    struct liars_seat* seat = create_seat(server);
    server->seat = seat;

    // create cursor
    struct liars_cursor* cursor = calloc(1, sizeof(*cursor));
    server->cursor = cursor;
    cursor->seat = seat;
    cursor->cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(cursor->cursor, server->output_layout);

    cursor->cursor_manager = wlr_xcursor_manager_create(NULL, 24);

    cursor->cursor_mode = LIARSWM_CURSOR_PASSTHROUGH;
    cursor->motion.notify = server_cursor_motion;
    wl_signal_add(&cursor->cursor->events.motion, &cursor->motion);

    cursor->motion_absolute.notify = server_cursor_motion_absolute;
    wl_signal_add(&cursor->cursor->events.motion_absolute, &cursor->motion_absolute);

    cursor->button.notify = server_cursor_button;
    wl_signal_add(&cursor->cursor->events.button, &cursor->button);

	cursor->axis.notify = server_cursor_axis;
	wl_signal_add(&cursor->cursor->events.axis, &cursor->axis);

	cursor->frame.notify = server_cursor_frame;
	wl_signal_add(&cursor->cursor->events.frame, &cursor->frame);

    seat->new_input.notify = server_new_input;
    wl_signal_add(&server->backend->events.new_input, &seat->new_input);

    const char* socket = wl_display_add_socket_auto(server->wl_display);
    if (!socket) {
        wlr_backend_destroy(server->backend);
        return 1;
    }

    if (!wlr_backend_start(server->backend)) {
        wlr_backend_destroy(server->backend);
        wl_display_destroy(server->wl_display);
        return 1;
    }
    wlr_log(WLR_DEBUG, "YOOO");
    wlr_cursor_set_xcursor(cursor->cursor, cursor->cursor_manager, "default");

    setenv("WAYLAND_DISPLAY", socket, true);
    
    if (fork() == 0) {
        execl("/usr/bin/kitty", "kitty", (void*)NULL);
    }

    wl_display_run(server->wl_display);

    wlr_log(WLR_DEBUG, "Liar server initiated");
    return 0;
}



