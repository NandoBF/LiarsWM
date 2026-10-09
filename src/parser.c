#include "parser.h"
#include "wlr/util/log.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wlr/types/wlr_keyboard.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>

struct key_token {
    const char* token;
    const uint32_t key;
};

static const struct key_token keys[] = {{"Ctrl", WLR_MODIFIER_CTRL},
                                        {"Shift", WLR_MODIFIER_SHIFT},
                                        {"Alt", WLR_MODIFIER_ALT},
                                        {"Super", WLR_MODIFIER_LOGO}};

int get_key_token_idx(const char* token) {
    for (int i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        if (!strcmp(token, keys[i].token))
            return i;
    }
    return -1;
}

void parse_key_combination(const char* s, uint32_t* out_modifiers,
                           xkb_keysym_t* out_keysym) {
    *out_modifiers = 0;
    *out_keysym = 0;

    char str[strlen(s) + 1];
    strcpy(str, s);

    char* curr = strtok(str, "+");
    while (curr != NULL) {
        int idx = get_key_token_idx(curr);
        if (idx >= 0) {
            *out_modifiers |= keys[idx].key;
        } else {
            *out_keysym =
                xkb_keysym_from_name(curr, XKB_KEYSYM_CASE_INSENSITIVE);
        }
        curr = strtok(NULL, "+");
    }
}
