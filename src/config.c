#include "config.h"
#include "input/keybind.h"
#include "ipc/parser.h"
#include "tomlc17.h"
#include "wlr/util/log.h"
#include <stdlib.h>
#include <sys/stat.h>

// TODO: Refactor config struct access?

#define INNER_GAPS "general.appearance.inner_gaps"
#define OUTER_GAPS "general.appearance.outer_gaps"
#define BORDER_WIDTH "general.appearance.border_width"

static int read_int(toml_result_t result, const char* name) {
    toml_datum_t read = toml_seek(result.toptab, name);

    if (read.type == TOML_INT64) {
        return read.u.int64;
    }

    wlr_log(WLR_INFO, "Could not read int: %s. Falling back.", name);
    return 1;
}

void toggle_command_mode(struct liars_server* server,
                         const union keybind_arg* arg) {
    server->cmd.mode =
        server->cmd.mode == MODE_NORMAL ? MODE_COMMAND : MODE_COMMAND;
    wlr_log(WLR_DEBUG, "Switched mode.");
}

// Config loading functions

static void load_keybinds(struct config* c, toml_datum_t root) {
    toml_datum_t keybinds = toml_get(root, "keybind");

    if (keybinds.type != TOML_ARRAY) {
        wlr_log(WLR_ERROR, "Incorrect type for keybind toml entry");
        return;
    }

    for (int i = 0; i < keybinds.u.arr.size; i++) {
        toml_datum_t kb = keybinds.u.arr.elem[i];
        const char* keys = toml_get(kb, "keys").u.s;
        const char* command = toml_get(kb, "action").u.s;
        const char* args = toml_get(kb, "args").u.s;

        uint32_t modifier;
        xkb_keysym_t keysym;
        parse_key_combination(keys, &modifier, &keysym);

        if (modifier == 0) {
            wlr_log(WLR_INFO, "Got invalid key combination: %s", keys);
            continue;
        }
        struct liars_keybind* k = calloc(1, sizeof(*k));
        k->modifiers = modifier;
        k->keysym = keysym;
        k->action = command;
        k->args = args;
        c->keybinds[c->keybind_num] = k;
        c->keybind_num++;
        // wlr_log(WLR_INFO, "Got keys: %s (%d), action: %s, command: %s", keys,
        // modifier, args,command);
    }

    wlr_log(WLR_INFO, "Loaded %d keybinds", c->keybind_num);
}

struct config* load_config(const char* config_path) {
    struct config* c = calloc(1, sizeof(*c));
    // FIX:
    c->keybinds = calloc(20, sizeof(struct liars_keybind*));

    struct stat file_stat;
    int err = stat(config_path, &file_stat);
    if (err != 0) {
        wlr_log(WLR_DEBUG, "Stat error");
        return false;
    }
    c->last_modified_at = file_stat.st_mtim.tv_sec;

    toml_result_t result = toml_parse_file_ex(config_path);

    if (!result.ok) {
        wlr_log(WLR_ERROR, "Error loading config from: %s", config_path);
        return NULL;
    }

    c->inner_gap = read_int(result, INNER_GAPS);
    c->outer_gap = read_int(result, OUTER_GAPS);
    c->border_width = read_int(result, BORDER_WIDTH);

    load_keybinds(c, result.toptab);

    wlr_log(WLR_INFO, "Got inner gap: %d", c->inner_gap);

    // toml_free(result); FIX: For some reason keybinds dont work with this
    return c;
}

bool was_config_modified(struct config* config, const char* config_path) {
    struct stat file_stat;
    int err = stat(config_path, &file_stat);
    if (err != 0) {
        wlr_log(WLR_DEBUG, "Stat error");
        return false;
    }
    return file_stat.st_mtim.tv_sec > config->last_modified_at;
}
