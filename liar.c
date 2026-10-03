#include <string.h>
#include <wayland-server-core.h>
#include "wlr/util/log.h"
#include "server.h"
#include "ipc.h"


int main(int argc, char* argv[]) {
    struct liars_server server = {0};
    init_liar_server(&server, WLR_DEBUG);
    server_run(&server);

    ipc_server_destroy(server.ipc_server); 
    return 0;
}
