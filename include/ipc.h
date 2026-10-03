#pragma once
#include <wayland-server-core.h>

struct liars_server;

struct ipc_server {
    int socket_fd;
    struct wl_event_source* event_source;
    char socket_path[255];
    struct liars_server* server;
};

struct ipc_server* ipc_server_create(struct liars_server* server);
void ipc_server_destroy(struct ipc_server* ipc);
