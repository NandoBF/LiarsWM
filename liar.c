#include <wayland-server-core.h>
#include "wlr/util/log.h"
#include "server.h"
#include "ipc/ipc.h"

#define DEFAULT_CONFIG_LOC "./config/nested.toml"

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
    init_liar_server(&server, WLR_DEBUG, config_file_path);
    server_run(&server);

    ipc_server_destroy(server.ipc_server); 
    return 0;
}
