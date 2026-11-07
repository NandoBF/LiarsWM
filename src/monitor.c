#include "monitor.h"
#include "layer_shell.h"
#include "server.h"
#include "windows/layout.h"
#include "windows/workspace.h"

void monitor_arrange_layers(struct monitor* monitor) {
    struct wlr_box old_usable = monitor->usable;
    monitor->usable = monitor->box;

    struct layer_surface* ls;
    wl_list_for_each(ls, &monitor->layer_surfaces, link) {
        if (!ls->wlr_layer_surface->initialized)
            continue;
        wlr_scene_layer_surface_v1_configure(ls->scene, &monitor->box,
                                             &monitor->usable);
    }

    if (old_usable.x != monitor->usable.x ||
        old_usable.y != monitor->usable.y ||
        old_usable.width != monitor->usable.width ||
        old_usable.height != monitor->usable.height) {

        wlr_log(WLR_DEBUG, "Arranging!");
        struct workspace* ws = get_active_workspace(monitor->server);
        workspace_arrange(ws);

        // PERF: try a better way?
        struct column* col;
        wl_list_for_each(col, &monitor->server->columns, link) {
            for (int i = 0; i < MAX_LEVELS; i++) {
                if (col->levels[i] && col->levels[i] != ws)
                    col->levels[i]->needs_arrange = true;
            }
        }
    }
}
