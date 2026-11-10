#pragma once
#include <stdint.h>

#define MACRO_HUB_KEY_COUNT 20

struct key_override {
    uint8_t modifier;
    uint8_t usage;
};

extern struct key_override key_overrides[MACRO_HUB_KEY_COUNT];
void key_overrides_save(void);
void key_overrides_load(void);
