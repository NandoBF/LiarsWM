#pragma once
#include <stdint.h>
#include <xkbcommon/xkbcommon.h>

void parse_key_combination(const char* s, uint32_t* out_modifiers,
                           xkb_keysym_t* out_keysym);
int parse_modifier(const char* s);
