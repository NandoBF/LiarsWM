#include <wayland-server-core.h>
#include "wlr/util/log.h"
#include "server.h"
#include "ipc/ipc.h"
#include "config.h"

#define DEFAULT_CONFIG_LOC "./config/nested.toml"

// TODO: Keyboard focus navigation
// TODO: Session lock?
// TODO: Fix primary selected (low priority I dont use it)
// TODO: Viewporter for HiDPI apps
// TODO: Multi monitor support (I cant test this)

int main(int argc, char* argv[]) {
    char* config_file_path = NULL;
    for (int i = 1; i < argc; i++) {
        printf("%s\n", argv[i]);
        if (!strcmp(argv[i], "-c")) {
            config_file_path = argv[++i]; 
        }
    }
    if (!config_file_path) 
        config_file_path = DEFAULT_CONFIG_LOC;


    struct liars_server server = {0};
    if (init_liar_server(&server, WLR_DEBUG, config_file_path) != 0) {
        return 1;
    }
    config_do_execs(&server, server.config);
    config_do_execs_once(&server, server.config);
    server_run(&server);

    server_cleanup(&server);
    printf("Graceful exit\n");
    return 0;
}
