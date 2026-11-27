#include "ipc/ipc.h"
#include "ipc/command.h"
#include "server.h"
#include "wlr/util/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <wayland-server-core.h>

static int ipc_handle_connection(int fd, uint32_t mask, void* data) {
    struct ipc_server* ipc = data;
    int client_fd = accept(fd, NULL, NULL);
    if (client_fd < 0)
        return 0;

    char buf[512];
    ssize_t n = read(client_fd, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';

        if (n > 0 && buf[n - 1] == '\n')
            buf[n - 1] = '\0';

        wlr_log(WLR_DEBUG, "IPC received: '%s'", buf);

        handle_command_input(ipc->server, buf, client_fd);
    }

    close(client_fd);
    return 0;
}

static const char* ipc_runtime_dir(void) {
    static char fallback[64];

    const char* runtime = getenv("XDG_RUNTIME_DIR");
    if (runtime && *runtime)
        return runtime;

    snprintf(fallback, sizeof(fallback), "/run/user/%u", (unsigned)getuid());
    struct stat st;
    if (stat(fallback, &st) == 0 && S_ISDIR(st.st_mode) &&
        access(fallback, W_OK | X_OK) == 0)
        return fallback;

    return "/tmp";
}

struct ipc_server* ipc_server_create(struct liars_server* server) {
    struct ipc_server* ipc = calloc(1, sizeof(*ipc));
    if (!ipc) {
        wlr_log(WLR_ERROR, "Failed to allocate IPC server");
        return NULL;
    }
    ipc->server = server;

    const char* runtime = ipc_runtime_dir();
    const char* display = getenv("WAYLAND_DISPLAY");
    int written =
        snprintf(ipc->socket_path, sizeof(ipc->socket_path), "%s/liars.%s.sock",
                 runtime, display ? display : "wayland-0");
    if (written < 0 || (size_t)written >= sizeof(ipc->socket_path) ||
        (size_t)written >= sizeof(((struct sockaddr_un*)0)->sun_path)) {
        wlr_log(WLR_ERROR, "IPC socket path is too long");
        free(ipc);
        return NULL;
    }

    ipc->socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (ipc->socket_fd < 0) {
        wlr_log_errno(WLR_ERROR, "Failed to create IPC socket");
        free(ipc);
        return NULL;
    }

    struct sockaddr_un addr = {.sun_family = AF_UNIX};
    strncpy(addr.sun_path, ipc->socket_path, sizeof(addr.sun_path) - 1);
    unlink(ipc->socket_path);

    if (bind(ipc->socket_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        wlr_log_errno(WLR_ERROR, "Failed to bind IPC Socket");
        close(ipc->socket_fd);
        free(ipc);
        return NULL;
    }
    if (listen(ipc->socket_fd, 5) < 0) {
        wlr_log_errno(WLR_ERROR, "Failed to listen on IPC Socket");
        close(ipc->socket_fd);
        unlink(ipc->socket_path);
        free(ipc);
        return NULL;
    }

    ipc->event_source =
        wl_event_loop_add_fd(server->event_loop, ipc->socket_fd,
                             WL_EVENT_READABLE, ipc_handle_connection, ipc);
    if (!ipc->event_source) {
        wlr_log(WLR_ERROR, "Failed to add IPC socket to event loop");
        close(ipc->socket_fd);
        unlink(ipc->socket_path);
        free(ipc);
        return NULL;
    }

    setenv("LIARSWM_SOCKET", ipc->socket_path, true);
    return ipc;
}

void ipc_server_destroy(struct ipc_server* ipc) {
    if (!ipc)
        return;

    if (ipc->event_source)
        wl_event_source_remove(ipc->event_source);
    close(ipc->socket_fd);
    unlink(ipc->socket_path);
    free(ipc);
}
