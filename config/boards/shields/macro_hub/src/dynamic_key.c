/*
 * stitchkeys,dynamic-key ZMK behavior.
 *
 * Usage in keymap:  &dkey <slot>
 * On press: looks up slot's (modifier, usage) from key_overrides[], sends HID.
 * On release: releases the same code.
 * key_overrides[] is populated from NVS by serial_handler on boot and updated
 * over serial at runtime.
 */

#define DT_DRV_COMPAT stitchkeys_dynamic_key

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zmk/hid.h>
#include <zmk/endpoints.h>
#include <dt-bindings/zmk/hid_usage_pages.h>
#include "key_overrides.h"

static int dkey_pressed(struct zmk_behavior_binding *binding,
                         struct zmk_behavior_binding_event event)
{
    uint8_t slot = (uint8_t)binding->param1;
    if (slot >= MACRO_HUB_KEY_COUNT) return -EINVAL;

    uint8_t mod   = key_overrides[slot].modifier;
    uint8_t usage = key_overrides[slot].usage;
    if (usage == 0) return 0; /* unassigned */

    if (mod) zmk_hid_register_mods(mod);
    zmk_hid_keyboard_press(ZMK_HID_USAGE(HID_USAGE_KEY, usage));
    return zmk_endpoints_send_report(HID_USAGE_KEY);
}

static int dkey_released(struct zmk_behavior_binding *binding,
                          struct zmk_behavior_binding_event event)
{
    uint8_t slot = (uint8_t)binding->param1;
    if (slot >= MACRO_HUB_KEY_COUNT) return -EINVAL;

    uint8_t mod   = key_overrides[slot].modifier;
    uint8_t usage = key_overrides[slot].usage;
    if (usage == 0) return 0;

    zmk_hid_keyboard_release(ZMK_HID_USAGE(HID_USAGE_KEY, usage));
    if (mod) zmk_hid_unregister_mods(mod);
    return zmk_endpoints_send_report(HID_USAGE_KEY);
}

static const struct behavior_driver_api dkey_api = {
    .binding_pressed  = dkey_pressed,
    .binding_released = dkey_released,
};

static int dkey_init(const struct device *dev)
{
    ARG_UNUSED(dev);
    key_overrides_load();
    return 0;
}

BEHAVIOR_DT_INST_DEFINE(0, dkey_init, NULL, NULL, NULL,
                        POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
                        &dkey_api);
