#pragma once
#include <stdint.h>

#define MACRO_HUB_KEY_COUNT 20

struct key_override {
    uint8_t modifier; /* HID modifier bitmask: Ctrl=0x01 Shift=0x02 Alt=0x04 GUI=0x08 */
    uint8_t usage;    /* HID keyboard usage ID (0x04=A … 0=unassigned) */
};

extern struct key_override key_overrides[MACRO_HUB_KEY_COUNT];

void key_overrides_save(void);
void key_overrides_load(void);
