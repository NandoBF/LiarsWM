#pragma once
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-util.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>

struct liars_server;
struct view;

// Shared by XDG toplevel popups and layer surface popups. parent_view is NULL
// for popups whose parent chain ends in a layer surface.
struct xdg_popup {
    struct liars_server* server;
    struct view* parent_view;
    struct wlr_scene_tree* root_tree;
    struct wlr_scene_tree* scene_tree;
    struct wlr_xdg_popup* xdg_popup;

    struct wl_listener commit;
    struct wl_listener destroy;
    struct wl_listener reposition;
};

void xdg_popup_create(struct wlr_xdg_popup* xdg_popup,
                      struct wlr_scene_tree* parent_tree,
                      struct wlr_scene_tree* root_tree,
                      struct view* parent_view, struct liars_server* server);
void server_new_xdg_toplevel(struct wl_listener* listener, void* data);
void server_new_xdg_popup(struct wl_listener* listener, void* data);
