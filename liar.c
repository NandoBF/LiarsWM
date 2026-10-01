#include <wayland-server-core.h>
#include "wlr/util/log.h"
#include "workspace.h"
#include "server.h"


int main(int argc, char* argv[]) {
    struct liars_server server = {0};
    init_liar_server(&server, WLR_DEBUG);
    server_run(&server);

    return 0;
}
