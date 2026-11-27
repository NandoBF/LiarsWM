#include "config.h"
#include "input/keybind.h"
#include "ipc/command.h"
#include "ipc/parser.h"
#include "tomlc17.h"
#include "wlr/util/log.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// TODO: Refactor config struct access?
// PERF: Lookup tables for actions?

#define INNER_GAPS "general.appearance.inner_gaps"
#define OUTER_GAPS "general.appearance.outer_gaps"
#define BORDER_WIDTH "general.appearance.border_width"
#define INTERACTIVE_MOD "general.binds.interactive_modifier"

static int read_int(toml_result_t result, const char* name) {
    toml_datum_t read = toml_seek(result.toptab, name);

    if (read.type == TOML_INT64) {
        return read.u.int64;
    }

    wlr_log(WLR_INFO, "Could not read int: %s. Falling back.", name);
    return 1;
}

static const char* read_str(toml_result_t result, const char* name) {
    toml_datum_t read = toml_seek(result.toptab, name);

    if (read.type == TOML_STRING) {
        return read.u.s;
    }

    wlr_log(WLR_INFO, "Could not read string: %s. Falling back.", name);
    return "";
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
    if (keybinds.u.arr.size <= 0) {
        wlr_log(WLR_ERROR, "Invalid sized keybinds array");
        return;
    }

    c->keybinds = calloc(keybinds.u.arr.size, sizeof(*c->keybinds));

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
        struct liars_keybind* k = &c->keybinds[c->keybind_num];
        k->modifiers = modifier;
        k->keysym = keysym;
        if (command) {
            k->action = strdup(command);
        }
        if (args) {
            k->args = strdup(args);
        }
        c->keybind_num++;
    }

    wlr_log(WLR_INFO, "Loaded %d keybinds", c->keybind_num);
}

static void load_execs(struct config* c, toml_datum_t root) {
    toml_datum_t execs = toml_get(root, "exec");

    if (execs.type != TOML_ARRAY) {
        wlr_log(WLR_DEBUG, "Incorrect type for exec toml entry");
        return;
    }
    if (execs.u.arr.size <= 0) {
        wlr_log(WLR_ERROR, "Invalid sized exec array: %d", execs.u.arr.size);
        return;
    }

    c->execs = calloc(execs.u.arr.size, sizeof(*c->execs));
    for (int i = 0; i < execs.u.arr.size; i++) {
        toml_datum_t e = execs.u.arr.elem[i];
        const char* command = toml_get(e, "command").u.s;
        if (!command) {
            wlr_log(WLR_ERROR, "Invalid exec when loading config");
            continue;
        }
        c->execs[c->execs_count] = strdup(command);
        c->execs_count++;
    }
    wlr_log(WLR_INFO, "Loaded %d execs", c->execs_count);
}

static void load_auto_execs(struct config* c, toml_datum_t root) {
    toml_datum_t execs_once = toml_get(root, "exec-once");

    if (execs_once.type != TOML_ARRAY) {
        wlr_log(WLR_DEBUG, "Incorrect type for exec-once toml entry");
        return;
    }
    if (execs_once.u.arr.size <= 0) {
        wlr_log(WLR_ERROR, "Invalid sized exec-once array: %d",
                execs_once.u.arr.size);
        return;
    }

    c->execs_once = calloc(execs_once.u.arr.size, sizeof(*c->execs_once));
    for (int i = 0; i < execs_once.u.arr.size; i++) {
        toml_datum_t e = execs_once.u.arr.elem[i];
        const char* command = toml_get(e, "command").u.s;
        if (!command) {
            wlr_log(WLR_ERROR, "Invalid exec when loading config");
            continue;
        }
        c->execs_once[c->execs_once_count] = strdup(command);
        c->execs_once_count++;
    }
    wlr_log(WLR_INFO, "Loaded %d exec-once's", c->execs_once_count);
}

void config_destroy(struct config* config) {
    free(config->file_path);

    for (int i = 0; i < config->keybind_num; i++) {
        if (config->keybinds[i].action)
            free(config->keybinds[i].action);
        if (config->keybinds[i].args)
            free(config->keybinds[i].args);
    }
    for (int i = 0; i < config->execs_count; i++) {
        free(config->execs[i]);
    }
    for (int i = 0; i < config->execs_once_count; i++) {
        free(config->execs_once[i]);
    }
    if (config->execs)
        free(config->execs);
    if (config->execs_once)
        free(config->execs_once);

    free(config->keybinds);

    free(config);
}

void config_do_execs(struct liars_server* server, struct config* c) {
    if (!c)
        return;

    for (int i = 0; i < c->execs_count; i++) {
        command_spawn(server, c->execs[i], -1);
    }
}

void config_do_execs_once(struct liars_server* server, struct config* c) {
    if (!c)
        return;

    for (int i = 0; i < c->execs_once_count; i++) {
        command_spawn(server, c->execs_once[i], -1);
    }
}

struct config* load_config(const char* config_path) {
    struct stat file_stat;
    int err = stat(config_path, &file_stat);
    if (err != 0) {
        wlr_log(WLR_DEBUG, "Stat error");
        return NULL;
    }

    toml_result_t result = toml_parse_file_ex(config_path);

    if (!result.ok) {
        wlr_log(WLR_ERROR, "Error loading config from: %s", config_path);
        toml_free(result);
        return NULL;
    }

    struct config* c = calloc(1, sizeof(*c));
    c->file_path = strdup(config_path);
    c->last_modified_at = file_stat.st_mtim.tv_sec;

    c->inner_gap = read_int(result, INNER_GAPS);
    c->outer_gap = read_int(result, OUTER_GAPS);
    c->border_width = read_int(result, BORDER_WIDTH);
    c->interactive_modifier = parse_modifier(read_str(result, INTERACTIVE_MOD));
    if (c->interactive_modifier == -1) {
        c->interactive_modifier = WLR_MODIFIER_CTRL;
        wlr_log(WLR_ERROR,
                "Invalid interactive modifier, falling back to CTRL");
    }

    load_keybinds(c, result.toptab);
    load_execs(c, result.toptab);
    load_auto_execs(c, result.toptab);

    wlr_log(WLR_INFO, "Got inner gap: %d", c->inner_gap);

    toml_free(result);
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
