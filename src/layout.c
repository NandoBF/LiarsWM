#include "layout.h"
#include "server.h"
#include "view.h"
#include "workspace.h"
#include <wayland-util.h>


void workspace_arrange(struct workspace* ws) {
    if (!ws->layout || !ws->layout->arrange) return;

    struct monitor* monitor = ws->col->monitor;
    int width = monitor->output->width;
    int height = monitor->output->height;
    ws->layout->arrange(ws, width, height);
}

static void layout_master_stack(struct workspace* ws, int width, int height) {
    int n = wl_list_length(&ws->tiles);
    if (n == 0) return;

    int master_width = (n==1) ? width : width / 2;
    int i = 0;
    struct view* view;
    wl_list_for_each(view, &ws->tiles, link) {
        if (i == 0) {
            wlr_scene_node_set_position(&view->scene_tree->node, 0, 0);
            view->impl->set_size(view, master_width, height);
        } else {
            int stack_count = n - 1;
            int stack_height = height / stack_count;
            int y = (i - 1) * stack_height;
            wlr_scene_node_set_position(&view->scene_tree->node, master_width, y);
            view->impl->set_size(view, width - master_width, stack_height);
        }
        i++;
    }
}

const struct layout master_stack_layout = {
    .name = "master-stack",
    .arrange = layout_master_stack,
};
