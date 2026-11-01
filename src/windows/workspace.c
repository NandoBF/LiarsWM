#include "windows/workspace.h"
#include "server.h"
#include "view/view.h"
#include "windows/layout.h"
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
    ws->TREE = wlr_scene_tree_create(&col->server->scene->tree);

    // bottom-to-top creation order so they render correctly
    ws->views.layer_tiled = wlr_scene_tree_create(ws->TREE);
    ws->views.layer_floating = wlr_scene_tree_create(ws->TREE);
    ws->views.layer_fullscreen = wlr_scene_tree_create(ws->TREE);
    wl_list_init(&ws->views.tiles);
    wl_list_init(&ws->views.floating);
    ws->views.fullscreen = NULL;

    wl_list_insert(&col->levels, &ws->link);
    ws->layout = &tiling_layout;
    ws->needs_arrange = true;
    return ws;
}

int send_to_workspace(struct workspace* new, struct view* view) {
    if (view->workspace == new)
        return 0;

    struct workspace* old = view->workspace;

    // remove any fullscreen view so its not annoying
    if (new->views.fullscreen != NULL) {
        view_set_fullscreen(new->views.fullscreen, false);
    }

    wl_list_remove(&view->link);
    if (view->is_floating) {
        wl_list_insert(&new->views.floating, &view->link);
        wlr_scene_node_reparent(&view->scene_tree->node,
                                new->views.layer_floating);
    } else if (view->is_fullscreen) {
        new->views.fullscreen = view;
        wlr_scene_node_reparent(&view->scene_tree->node,
                                new->views.layer_fullscreen);
    } else {
        wl_list_insert(&new->views.tiles, &view->link);
        wlr_scene_node_reparent(&view->scene_tree->node,
                                new->views.layer_tiled);
    }

    view->workspace = new;

    if (old && old->layout) {
        workspace_arrange(old);
    }
    if (new->layout) {
        workspace_arrange(new);
    }
    focus_view_under_cursor(view->server);

    return 1;
}

// PERF: I hate strings
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
    col->name_size = strlen(col->name);
    wl_list_init(&col->levels);
    col->last_level = 0;
    wl_list_insert(&server->columns, &col->link);

    workspace_create(col);
    return col;
}

void workspace_set_fullscreen(struct view* view) {
    wlr_scene_node_raise_to_top(&view->scene_tree->node);
    wlr_scene_node_set_position(&view->scene_tree->node, 0, 0);
    wlr_scene_node_reparent(&view->scene_tree->node,
                            view->workspace->views.layer_fullscreen);
    view->workspace->views.fullscreen = view;
    // workspace_arrange(view->workspace);
}

// perhaps remove would be a better name, but if youre coming from set, unset is
// better
void workspace_unset_fullscreen(struct workspace* ws, struct view* view) {
    ws->views.fullscreen = NULL;
    wlr_scene_node_reparent(&view->scene_tree->node,
                            view->workspace->views.layer_tiled);
    workspace_arrange(ws);
}

void workspace_spawn(struct workspace* ws, struct view* view) {
    if (ws->views.fullscreen) {
        view_set_fullscreen(ws->views.fullscreen, false);
    }

    view->workspace = ws;
    wlr_scene_node_reparent(&view->scene_tree->node, ws->views.layer_tiled);
    wl_list_insert(&view->workspace->views.tiles, &view->link);
    workspace_arrange(ws);
}
