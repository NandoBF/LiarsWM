#pragma once

#include "ipc/command.h"
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-util.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>

struct liars_server {
    struct config* config;

    struct wl_display* wl_display;
    struct wl_event_loop* event_loop;
    struct wlr_backend* backend;
    struct wlr_renderer* renderer;
    struct wlr_allocator* allocator;
    struct wlr_scene* scene;
    struct wlr_scene_output_layout* scene_layout;

    struct wlr_xdg_shell* xdg_shell;
    struct wl_listener new_xdg_toplevel;
    struct wl_listener new_xdg_popup;

    struct liars_seat* seat;
    struct liars_cursor* cursor;

    struct wl_list monitors;
    struct wlr_output_layout* output_layout;
    struct wl_listener new_output;
    struct wl_listener output_layout_changed;

    struct wl_list columns;
    struct workspace* active_workspace;

    struct command_state cmd;
    struct ipc_server* ipc_server;

    struct wlr_xwayland* xwayland;
    struct wl_listener new_xwayland_surface;

    struct wlr_layer_shell_v1* layer_shell;
    struct wl_listener new_layer_shell_surface;

    struct wlr_xdg_decoration_manager_v1* decoration_manager;
    struct wl_listener new_view_decoration;

    struct wlr_xdg_output_manager_v1* xdg_output_manager;

    struct wlr_xdg_activation_v1* xdg_activation;
    struct wl_listener activation_request;
    struct wl_listener activation_new_token;
};

struct view* desktop_view_at(struct liars_server* server, double lx, double ly,
                             struct wlr_surface** surface, double* sx,
                             double* sy);

void switch_to_workspace(struct liars_server* server, struct workspace* new);
void switch_to_column(struct liars_server* server, const char* name);
struct view* get_focused_view(struct liars_server* server);
struct view* get_view_under_cursor(struct liars_server* server);
void focus_view_under_cursor(struct liars_server* server);

void server_run(struct liars_server* server);
int init_liar_server(struct liars_server* server,
                     enum wlr_log_importance log_level, char* config_file_path);
struct workspace* get_active_workspace(struct liars_server* server);
