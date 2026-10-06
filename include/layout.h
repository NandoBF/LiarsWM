#pragma once

#include <wayland-util.h>
struct workspace;

struct layout {
    const char* name;
    void (*arrange)(struct workspace* ws, int x, int y, int width, int height, int inner_gap);
};


extern const struct layout master_stack_layout;
extern const struct layout tiling_layout;
void workspace_arrange(struct workspace* ws);
