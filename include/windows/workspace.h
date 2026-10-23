#pragma once
#include "server.h"
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-util.h>
#include <wlr/backend.h>

struct workspace_views {
    struct wlr_scene_tree* layer_tiled;
    struct wl_list tiles;

    struct wlr_scene_tree* layer_floating;
    struct wl_list floating;

    struct wlr_scene_tree* layer_fullscreen;
    struct view* fullscreen;
};

struct column {
    struct wl_list link;
    struct liars_server* server;

    struct monitor* monitor;
    char* name;
    struct wl_list levels;
    int last_level;
};

struct workspace {
    struct wl_list link;
    struct column* col;
    struct workspace_views views;
    int level;

    struct wlr_scene_tree* TREE; // root tree, dont place anything here
    const struct layout* layout;
    void* layout_data;

    bool needs_arrange; // FIX: I dont like this much
};

struct column* column_get_or_create(struct liars_server* server,
                                    struct monitor* monitor, const char* name);

int send_to_workspace(struct workspace* new, struct view* view);
void workspace_spawn(struct workspace* ws, struct view* view);
void workspace_set_fullscreen(struct view* view);
void workspace_unset_fullscreen(struct workspace* ws, struct view* view);
