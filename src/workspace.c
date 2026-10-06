#include "workspace.h"
#include "layout.h"
#include "server.h"
#include <stdlib.h>
#include <string.h>
#include <wayland-util.h>

static int is_column_name_valid(const char* name) {
    // only allow numbers and letters
    for (int i = 0; name[i]; i++) {
        if (name[i] == ' ')
            return 0;
    }
    return 1;
}

struct workspace* workspace_create(struct column* col) {
    struct workspace* ws = calloc(1, sizeof(*ws));
    ws->col = col;
    ws->level = col->last_level++;
    ws->tree = wlr_scene_tree_create(&col->server->scene->tree);
    wl_list_init(&ws->tiles);
    wl_list_init(&ws->floating);
    wl_list_insert(&col->levels, &ws->link);
    ws->layout = &tiling_layout;
    return ws;
}

struct column* column_get_or_create(struct liars_server* server,
                                    struct monitor* monitor, const char* name) {
    if (!is_column_name_valid(name))
        return NULL;

    struct column* col;
    wl_list_for_each(col, &server->columns, link) {
        if (strcmp(col->name, name) == 0)
            return col;
    }

    col = calloc(1, sizeof(*col));
    col->server = server;
    col->monitor = monitor;
    col->name = strdup(name);
    wl_list_init(&col->levels);
    col->last_level = 0;
    wl_list_insert(&server->columns, &col->link);

    workspace_create(col);
    return col;
}
