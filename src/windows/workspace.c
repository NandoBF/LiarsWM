#include "windows/workspace.h"
#include "server.h"
#include "view/view.h"
#include "windows/layout.h"
#include "wlr/util/log.h"
#include <stdlib.h>
#include <string.h>
#include <wayland-util.h>

static int is_column_name_valid(const char* name) {
    // only allow numbers and letters
    // TODO:
    for (int i = 0; name[i]; i++) {
        if (name[i] == ' ')
            return 0;
    }
    return 1;
}

bool column_is_level_valid(int level) {
    return (level < MAX_LEVELS && level >= 0);
}

bool is_workspace_empty(struct workspace* ws) {
    return wl_list_empty(&ws->views.tiles) &&
           wl_list_empty(&ws->views.floating) && !ws->views.fullscreen;
}

bool is_column_empty(struct column* col) {
    for (int i = 0; i < MAX_LEVELS; i++) {
        if (col->levels[i])
            return false;
    }
    return true;
}

void workspace_try_remove(struct workspace* ws) {
    if (is_workspace_empty(ws)) {
        ws->col->levels[ws->level] = NULL;
        free(ws);
    }
}
// The target column needs to have a free level for the workspace
// TODO: Option to not follow the workspace (just focus the old column and
// level)
void workspace_send_to_column(struct workspace* ws, struct column* target_col) {
    struct column* old = ws->col;
    int target_level = -1;

    if (!target_col->levels[ws->level])
        target_level = ws->level;
    else {
        for (int i = 0; target_level == -1 && i < MAX_LEVELS; i++) {
            if (!target_col->levels[i])
                target_level = i;
        }
    }

    if (target_level == -1)
        return; // no free level at target

    old->levels[ws->level] = NULL;
    target_col->levels[target_level] = ws;
    ws->col = target_col;
    ws->level = target_level;
    wlr_log(WLR_DEBUG, "Workspace moved from %s to %s:%d", old->name,
            target_col->name, ws->level);
}

struct workspace* workspace_create_at_level(struct column* col, int level) {
    if (!column_is_level_valid(level))
        return NULL;
    struct workspace* ws = calloc(1, sizeof(*ws));
    ws->col = col;
    ws->TREE = wlr_scene_tree_create(&col->server->scene->tree);

    // bottom-to-top creation order so they render correctly
    ws->views.layer_tiled = wlr_scene_tree_create(ws->TREE);
    ws->views.layer_floating = wlr_scene_tree_create(ws->TREE);
    ws->views.layer_fullscreen = wlr_scene_tree_create(ws->TREE);
    wl_list_init(&ws->views.tiles);
    wl_list_init(&ws->views.floating);
    ws->views.fullscreen = NULL;

    col->levels[level] = ws;
    ws->level = level;
    ws->layout = &tiling_layout;
    ws->needs_arrange = true;
    return ws;
}

// TODO: Collapse levels option?
struct workspace* column_get_or_create_level(struct column* col, int level) {
    if (!column_is_level_valid(level))
        return NULL;
    struct workspace* ws;
    ws = col->levels[level];

    if (!ws) {
        ws = workspace_create_at_level(col, level);
    }
    return ws;
}

// This function returns NULL if the passed column is not focused
struct workspace* column_get_offset_level(struct column* col, int offset) {
    struct workspace* ws = get_active_workspace(col->server);
    if (ws->col != col)
        return NULL;

    int new_level = ws->level + offset;
    return column_get_or_create_level(col, new_level);
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
    wl_list_insert(&server->columns, &col->link);
    col->levels = calloc(MAX_LEVELS, sizeof(struct workspace*));

    workspace_create_at_level(col, 0);
    return col;
}

void column_try_remove(struct liars_server* server, struct column* col) {
    if (!is_column_empty(col))
        return;

    wl_list_remove(&col->link);
    free(col);
    col = NULL;
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

    if (view->impl->want_floating && view->impl->want_floating(view)) {
        wlr_log(WLR_DEBUG, "Spawning dialog window as floating");
        view->is_floating = true;
        wl_list_insert(&ws->views.floating, &view->link);
        wlr_scene_node_reparent(&view->scene_tree->node,
                                ws->views.layer_floating);
        view_move_to_center(view);
        return;
    }

    wlr_scene_node_reparent(&view->scene_tree->node, ws->views.layer_tiled);
    wl_list_insert(&view->workspace->views.tiles, &view->link);
    workspace_arrange(ws);
}
