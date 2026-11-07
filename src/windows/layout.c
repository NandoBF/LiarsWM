#include "windows/layout.h"
#include "config.h"
#include "monitor.h"
#include "server.h"
#include "view/view.h"
#include "windows/workspace.h"
#include "wlr/util/log.h"
#include <wayland-util.h>

void workspace_arrange(struct workspace* ws) {
    if (!ws->layout || !ws->layout->arrange)
        return;

    struct monitor* monitor = ws->col->monitor;
    struct config* config = ws->col->server->config;
    int x = monitor->usable.x;
    int y = monitor->usable.y;
    int width = monitor->usable.width;
    int height = monitor->usable.height;

    int edge_offset = config->outer_gap - (config->inner_gap / 2);

    ws->layout->arrange(ws, x + edge_offset, y + edge_offset,
                        width - (2 * edge_offset), height - (2 * edge_offset),
                        config->inner_gap);
    ws->needs_arrange = false;
}

static void layout_master_stack(struct workspace* ws, int x, int y, int width,
                                int height, int inner_gap) {
    int n = wl_list_length(&ws->views.tiles);
    if (n == 0)
        return;

    int master_width = (n == 1) ? width : width / 2;
    int i = 0;
    struct view* view;
    wl_list_for_each(view, &ws->views.tiles, link) {
        int view_x, view_y, view_w, view_h;
        if (i == 0) {
            view_x = x;
            view_y = y;
            view_w = master_width;
            view_h = height;
        } else {
            int stack_count = n - 1;
            int stack_height = height / stack_count;
            int sy = (i - 1) * stack_height;
            view_x = x + master_width;
            view_y = y + sy;
            view_w = width - master_width;
            view_h = stack_height;
        }

        wlr_scene_node_set_position(&view->scene_tree->node,
                                    view_x + (inner_gap / 2),
                                    view_y + (inner_gap / 2));
        view->impl->set_size(view, view_w - inner_gap, view_h - inner_gap);

        i++;
    }
}

static void layout_tiling(struct workspace* ws, int x, int y, int width,
                          int height, int inner_gap) {
    wlr_log(WLR_DEBUG, "Tiling");
    int n = wl_list_length(&ws->views.tiles);
    if (n == 0)
        return;

    int current_x = x;
    int current_y = y;
    int current_w = width;
    int current_h = height;

    int i = 0;
    struct view* view;
    wl_list_for_each(view, &ws->views.tiles, link) {
        int w = current_w;
        int h = current_h;

        int split_horiz = (current_w > current_h);

        if (i < n - 1) {
            if (split_horiz) {
                w = current_w / 2;
                current_w -= w;
            } else {
                h = current_h / 2;
                current_h -= h;
            }
        }
        const int x = current_x + (inner_gap / 2);
        const int y = current_y + (inner_gap / 2);
        wlr_scene_node_set_position(&view->scene_tree->node, x, y);
        view->impl->set_size(view, w - inner_gap, h - inner_gap);

        if (i < n - 1) {
            if (split_horiz) {
                current_x += w;
            } else {
                current_y += h;
            }
        }

        i++;
    }
}

const struct layout master_stack_layout = {
    .name = "master-stack",
    .arrange = layout_master_stack,
};

const struct layout tiling_layout = {
    .name = "tiling",
    .arrange = layout_tiling,
};
