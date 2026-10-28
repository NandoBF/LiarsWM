#pragma once
#include "windows/layout.h"
#include <stdbool.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/box.h>

struct liars_server;
struct liars_seat;
struct workspace;

struct view {
    struct wl_list link;
    struct wl_list flink;
    const struct view_impl* impl;
    struct liars_server* server;

    struct wlr_surface* surface;
    struct wlr_scene_tree* scene_tree;

    struct workspace* workspace;
    bool mapped;

    struct wl_listener destroy;
    struct wl_listener map;
    struct wl_listener unmap;

    struct wl_listener commit;
    struct wl_listener request_move;
    struct wl_listener request_resize;
    struct wl_listener request_fullscreen;
    struct wl_listener request_maximize;
    struct wl_listener set_title;
    struct wl_listener set_app_id;

    struct wl_listener initial_commit;

    struct wlr_scene_rect* borders[4];
    bool is_fullscreen;
    bool is_floating;
};

struct view_impl {
    void (*get_geometry)(struct view* v, struct wlr_box* box);
    void (*set_size)(struct view* v, int width, int height);
    void (*set_activated)(struct view* v, bool activated);
    void (*close)(struct view* v);
    const char* (*get_title)(struct view* v);
    const char* (*get_app_id)(struct view* v);
    bool (*want_fullscreen)(struct view* v);
    struct wlr_surface* (*get_surface)(struct view* v);
    void (*set_fullscreen)(struct view* v, bool fullscreen);
};

struct monitor* view_get_monitor(struct view* view);
bool is_surface_focused(struct liars_seat* seat, struct wlr_surface* surface);
bool is_view_focused(struct view* view);
void focus_view(struct view* view);

void view_set_borders(struct view* v, int w, int h);
void view_map(struct view* view);
void view_destroy(struct view* view);
void view_borders_set_enabled(struct view* view, bool enabled);

void view_set_floating(struct view* view, bool floating);
void view_set_fullscreen(struct view* view, bool fullscreen);
