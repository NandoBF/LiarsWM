#pragma once
#include "server.h"
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-util.h>
#include <wlr/backend.h>

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
    int level;

    struct wlr_scene_tree* tree;
    struct wl_list tiles;
    struct wl_list floating;
    const struct layout* layout;
    void* layout_data;

    bool needs_arrange; // FIX: I dont like this much
};

struct column* column_get_or_create(struct liars_server* server,
                                    struct monitor* monitor, const char* name);

int send_to_workspace(struct workspace* new, struct view* view);
