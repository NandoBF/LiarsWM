#pragma once

#include <wayland-util.h>
struct workspace;

struct layout {
    const char* name;
    void (*arrange)(struct workspace* ws, int width, int height);
};


extern const struct layout master_stack_layout;
void workspace_arrange(struct workspace* ws);
