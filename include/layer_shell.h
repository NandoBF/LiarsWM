#pragma once
#include <wayland-server-core.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_scene.h>


void server_new_layer_shell_surface(struct wl_listener* listener, void* data);
