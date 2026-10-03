#include "ipc.h"
#include "server.h"
#include "wlr/util/log.h"
#include "command.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <wayland-server-core.h>

static int ipc_handle_connection(int fd, uint32_t mask, void* data) {
    struct ipc_server* ipc = data;
    int client_fd = accept(fd, NULL, NULL);
    if (client_fd < 0) return 0;

    char buf[512];
    ssize_t n = read(client_fd, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';

        if (n > 0 && buf[n-1] == '\n') buf[n-1] = '\0';

        wlr_log(WLR_DEBUG, "IPC received: '%s'", buf);

        const char* result = handle_command_input(ipc->server, buf);
        const char* resp = result ? result : "ok\n";
        write(client_fd, resp, strlen(resp));
    }

    close(client_fd);
    return 0;
}


struct ipc_server* ipc_server_create(struct liars_server *server) {
    struct ipc_server* ipc = calloc(1, sizeof(*ipc));
    ipc->server = server;

    const char* runtime = getenv("XDG_RUNTIME_DIR");
    const char* display = getenv("WAYLAND_DISPLAY");
    snprintf(ipc->socket_path, sizeof(ipc->socket_path), "%s/liars.%s.sock", runtime, display);

    ipc->socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    strncpy(addr.sun_path, ipc->socket_path, sizeof(addr.sun_path) - 1);

    unlink(ipc->socket_path);

    if (bind(ipc->socket_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        wlr_log(WLR_ERROR, "Failed to bind IPC Socket");
        exit(1);
    }
    listen(ipc->socket_fd, 5);

    ipc->event_source = wl_event_loop_add_fd(server->event_loop, ipc->socket_fd, WL_EVENT_READABLE, ipc_handle_connection, ipc);

    setenv("LIARSWM_SOCKET", ipc->socket_path, true);
    return ipc;

}


void ipc_server_destroy(struct ipc_server* ipc){
    // TODO:
}
