#include "monitor.h"
#include "layer_shell.h"
#include "server.h"
#include "windows/layout.h"
#include "windows/workspace.h"
#include <stdlib.h>
#include <wayland-util.h>

static void output_destroy(struct wl_listener* listener, void* data) {
    struct monitor* monitor = wl_container_of(listener, monitor, destroy);

    wl_list_remove(&monitor->frame.link);
    wl_list_remove(&monitor->request_state.link);
    wl_list_remove(&monitor->destroy.link);
    wl_list_remove(&monitor->link);
    free(monitor);
}

// Called when the backend requests a new state for the output.
static void output_request_state(struct wl_listener* listener, void* data) {
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

struct monitor* monitor_create(struct liars_server* server,
                               struct wlr_output* output) {
    struct monitor* monitor = calloc(1, sizeof(*monitor));
    monitor->output = output;
    monitor->server = server;
    wl_list_init(&monitor->layer_surfaces);

    wlr_output_init_render(monitor->output, server->allocator,
                           server->renderer);

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
    wl_signal_add(&monitor->output->events.request_state,
                  &monitor->request_state);

    monitor->destroy.notify = output_destroy;
    wl_signal_add(&monitor->output->events.destroy, &monitor->destroy);

    struct wlr_output_layout_output* l_output =
        wlr_output_layout_add_auto(server->output_layout, monitor->output);
    struct wlr_scene_output* scene_output =
        wlr_scene_output_create(server->scene, monitor->output);
    wlr_scene_output_layout_add_output(server->scene_layout, l_output,
                                       scene_output);
    monitor->scene_output = scene_output;

    wlr_output_layout_get_box(server->output_layout, monitor->output,
                              &monitor->box);
    wlr_log(WLR_INFO, "Monitor %s at (%d, %d) with %dx%d",
            monitor->output->name, monitor->box.x, monitor->box.y,
            monitor->box.width, monitor->box.height);
    monitor->usable = monitor->box;

    return monitor;
}

void monitor_arrange_layers(struct monitor* monitor) {
    struct wlr_box old_usable = monitor->usable;
    monitor->usable = monitor->box;

    struct layer_surface* ls;
    wl_list_for_each(ls, &monitor->layer_surfaces, link) {
        if (!ls->wlr_layer_surface->initialized)
            continue;
        wlr_scene_layer_surface_v1_configure(ls->scene, &monitor->box,
                                             &monitor->usable);
    }

    if (old_usable.x != monitor->usable.x ||
        old_usable.y != monitor->usable.y ||
        old_usable.width != monitor->usable.width ||
        old_usable.height != monitor->usable.height) {

        wlr_log(WLR_DEBUG, "Arranging!");
        struct workspace* ws = get_active_workspace(monitor->server);
        workspace_arrange(ws);

        // PERF: try a better way?
        struct column* col;
        wl_list_for_each(col, &monitor->server->columns, link) {
            for (int i = 0; i < MAX_LEVELS; i++) {
                if (col->levels[i] && col->levels[i] != ws)
                    col->levels[i]->needs_arrange = true;
            }
        }
    }
}
