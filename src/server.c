#include "server.h"
#include "config.h"
#include "input/input.h"
#include "ipc/ipc.h"
#include "layer_shell.h"
#include "view/x11_view.h"
#include "view/xdg_view.h"
#include "windows/decoration.h"
#include "windows/layout.h"
#include "windows/workspace.h"
#include "wlr/types/wlr_xdg_decoration_v1.h"
#include <bits/time.h>
#include <fcntl.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-util.h>
#include <wlr/util/edges.h>
#include <wlr/util/log.h>
#include <wlr/xwayland.h>
#include <xkbcommon/xkbcommon.h>

// NOTE: Can directly save the focused view if needed later
struct view* get_focused_view(struct liars_server* server) {
    struct wlr_surface* focused_surface = seat_focused_surface(server->seat);
    if (!focused_surface)
        return NULL;

    struct workspace* ws = get_active_workspace(server);
    struct view* v;
    wl_list_for_each(v, &ws->views.tiles, link) {
        if (v->surface == focused_surface)
            return v;
    }
    wl_list_for_each(v, &ws->views.floating, link) {
        if (v->surface == focused_surface)
            return v;
    }
    return NULL;
}

void server_run(struct liars_server* server) {
    wl_display_run(server->wl_display);
}

struct workspace* get_active_workspace(struct liars_server* server) {
    return server->active_workspace;
}

void switch_to_workspace(struct liars_server* server, struct workspace* new) {
    struct workspace* old = server->active_workspace;
    if (old == new)
        return;

    if (old) {
        workspace_try_remove(old);
        wlr_scene_node_set_enabled(&old->TREE->node, false);
        column_try_remove(server, old->col);
    }
    if (new->needs_arrange) {
        workspace_arrange(new);
    }
    wlr_scene_node_set_enabled(&new->TREE->node, true);
    server->active_workspace = new;
    focus_view_under_cursor(server);
    wlr_log(WLR_DEBUG, "Switched to column %s level %d", new->col->name,
            new->level);
}

// TODO: Last focused level in col? This is why I even left this function
void switch_to_column(struct liars_server* server, const char* name) {
    struct monitor* monitor;
    struct column* col;
    wl_list_for_each(monitor, &server->monitors, link) {
        col = column_get_or_create(server, monitor, name);
        if (col) {
            struct workspace* new = column_get_or_create_level(col, 0);

            switch_to_workspace(server, new);
            return;
        }
    }
}

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

static void server_new_output(struct wl_listener* listener, void* data) {
    struct liars_server* server = wl_container_of(listener, server, new_output);
    struct monitor* monitor = calloc(1, sizeof(*monitor));
    if (monitor == NULL) {
        wlr_log(WLR_ERROR, "Monitor Calloc return null");
        return;
    }
    monitor->output = data;
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

    wl_list_insert(&server->monitors, &monitor->link);

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

    // FIX: This will focus column 1 if a second monitor is disconnected
    if (wl_list_length(&server->monitors) == 1) {
        struct column* col = column_get_or_create(server, monitor, "1");
        if (!col) {
            wlr_log(WLR_ERROR, "[FATAL] Could not create initial column!");
            exit(1); // TODO: DIE
        }

        server->active_workspace = column_get_or_create_level(col, 0);
        wlr_scene_node_set_enabled(&server->active_workspace->TREE->node, true);
    }
}

struct view* desktop_view_at(struct liars_server* server, double lx, double ly,
                             struct wlr_surface** surface, double* sx,
                             double* sy) {

    double _sx, _sy;
    struct wlr_scene_node* node =
        wlr_scene_node_at(&server->scene->tree.node, lx, ly, &_sx, &_sy);

    if (sx)
        *sx = _sx;
    if (sy)
        *sy = _sy;

    if (node == NULL || node->type != WLR_SCENE_NODE_BUFFER) {
        return NULL;
    }

    struct wlr_scene_buffer* scene_buffer = wlr_scene_buffer_from_node(node);
    struct wlr_scene_surface* scene_surface =
        wlr_scene_surface_try_from_buffer(scene_buffer);
    if (!scene_surface)
        return NULL;

    if (surface)
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

struct view* get_view_under_cursor(struct liars_server* server) {
    if (!server->cursor || !server->cursor->cursor)
        return NULL;
    return desktop_view_at(server, server->cursor->cursor->x,
                           server->cursor->cursor->y, NULL, NULL, NULL);
}

void focus_view_under_cursor(struct liars_server* server) {
    struct view* new_focus = get_view_under_cursor(server);
    if (new_focus) {
        focus_view(new_focus);
    } else {
        struct workspace* ws = server->active_workspace;
        if (ws) {
            struct view* fallback = NULL;
            if (ws->views.fullscreen) {
                fallback = ws->views.fullscreen;
            }
            if (!wl_list_empty(&ws->views.floating)) {
                fallback =
                    wl_container_of(ws->views.floating.next, fallback, link);
            } else if (!wl_list_empty(&ws->views.tiles)) {
                fallback =
                    wl_container_of(ws->views.tiles.next, fallback, link);
            }

            if (fallback) {
                focus_view(fallback);
            } else if (server->seat && server->seat->seat) {
                wlr_seat_keyboard_clear_focus(server->seat->seat);
            }
        }
    }
}

int init_liar_server(struct liars_server* server,
                     enum wlr_log_importance log_level) {

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

    server->allocator =
        wlr_allocator_autocreate(server->backend, server->renderer);
    if (server->allocator == NULL) {
        wlr_log(WLR_ERROR, "Could not start allocator");
        return 3;
    }

    struct wlr_compositor* wlr_compositor =
        wlr_compositor_create(server->wl_display, 5, server->renderer);
    wlr_subcompositor_create(server->wl_display);
    wlr_data_device_manager_create(server->wl_display);

    wl_list_init(&server->monitors);
    wl_list_init(&server->columns);

    server->output_layout = wlr_output_layout_create(server->wl_display);

    server->new_output.notify = server_new_output;
    wl_signal_add(&server->backend->events.new_output, &server->new_output);
    server->scene = wlr_scene_create();
    server->scene_layout =
        wlr_scene_attach_output_layout(server->scene, server->output_layout);

    server->xdg_shell = wlr_xdg_shell_create(server->wl_display, 3);
    server->new_xdg_toplevel.notify = server_new_xdg_toplevel;
    wl_signal_add(&server->xdg_shell->events.new_toplevel,
                  &server->new_xdg_toplevel);

    server->new_xdg_popup.notify = server_new_xdg_popup;
    wl_signal_add(&server->xdg_shell->events.new_popup, &server->new_xdg_popup);

    server->layer_shell = wlr_layer_shell_v1_create(server->wl_display, 4);
    server->new_layer_shell_surface.notify = server_new_layer_shell_surface;
    wl_signal_add(&server->layer_shell->events.new_surface,
                  &server->new_layer_shell_surface);

    // x11
    server->xwayland =
        wlr_xwayland_create(server->wl_display, wlr_compositor, true);
    if (server->xwayland) {
        server->new_xwayland_surface.notify = server_new_xwayland_surface;
        wl_signal_add(&server->xwayland->events.new_surface,
                      &server->new_xwayland_surface);

        setenv("DISPLAY", server->xwayland->display_name, true);
    }

    server->decoration_manager =
        wlr_xdg_decoration_manager_v1_create(server->wl_display);
    server->new_view_decoration.notify = new_view_decoration;
    wl_signal_add(&server->decoration_manager->events.new_toplevel_decoration,
                  &server->new_view_decoration);

    server->xdg_output_manager = wlr_xdg_output_manager_v1_create(
        server->wl_display, server->output_layout);

    // Seat
    struct liars_seat* seat = create_seat(server);
    server->seat = seat;
    wlr_xwayland_set_seat(server->xwayland, seat->seat);

    struct liars_cursor* cursor = create_cursor(seat, server->output_layout);
    server->cursor = cursor;

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

    struct config* config = load_config("./config/nested.toml");
    server->config = config;

    wlr_cursor_set_xcursor(cursor->cursor, cursor->cursor_manager, "default");

    setenv("WAYLAND_DISPLAY", socket, true);

    server->ipc_server = ipc_server_create(server);

    wlr_log(WLR_DEBUG, "Liar server initiated");
    return 0;
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
