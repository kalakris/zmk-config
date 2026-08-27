/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * Wire format for the raw touch stream (protocol v2).
 *
 * The module owns a private HID report descriptor containing exactly one
 * top-level vendor-defined application collection, carried on its own USB
 * HID interface and its own HID-over-GATT service instance. Nothing here
 * touches ZMK's own descriptor, which is why no fork of ZMK is needed.
 */

#pragma once

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#define ZMK_RAW_TOUCH_REPORT_ID CONFIG_ZMK_RAW_TOUCH_REPORT_ID

/* Input report: one frame, emitted per pad sample while touched (~100 Hz),
 * plus exactly one release frame (touched = 0, z = 0) on lift-off. */

#define ZMK_RAW_TOUCH_FLAGS_TOUCHED BIT(0)
#define ZMK_RAW_TOUCH_FLAGS_SCROLL_MODE BIT(1)

struct zmk_raw_touch_report_body {
    uint8_t pad_id;
    uint16_t x; /* little-endian */
    uint16_t y; /* little-endian */
    uint8_t z;
    uint8_t flags;
} __packed;

struct zmk_raw_touch_report {
    uint8_t report_id;
    struct zmk_raw_touch_report_body body;
} __packed;

/* Feature report: self-describing pad capabilities, on the same report ID.
 * Readable over USB GET_REPORT and the BLE feature report characteristic.
 * Hosts MUST read and validate this before treating a vendor collection on
 * this usage page as the raw touch protocol - 0xFF00/0x01 is a commonly
 * squatted vendor pair. */

#define ZMK_RAW_TOUCH_PROTOCOL_VERSION 2

#define ZMK_RAW_TOUCH_ORIENT_ROTATE_90 BIT(0)
#define ZMK_RAW_TOUCH_ORIENT_X_INVERT BIT(1)
#define ZMK_RAW_TOUCH_ORIENT_Y_INVERT BIT(2)

struct zmk_raw_touch_feature_body {
    uint8_t protocol_version;
    uint8_t pads_present; /* bit N set if pad-id N exists */
    uint8_t resolution;   /* counts/mm, 0 = unknown */
    uint8_t orientation;  /* ZMK_RAW_TOUCH_ORIENT_* */
    uint16_t x_max;       /* little-endian */
    uint16_t y_max;       /* little-endian */
} __packed;

struct zmk_raw_touch_feature_report {
    uint8_t report_id;
    struct zmk_raw_touch_feature_body body;
} __packed;

/* The module's private report descriptor, defined in src/hid.c. */
extern const uint8_t zmk_raw_touch_report_desc[];
extern const size_t zmk_raw_touch_report_desc_size;

/* Report state accessors (src/hid.c). */
void zmk_raw_touch_hid_set(uint8_t pad_id, uint16_t x, uint16_t y, uint8_t z, uint8_t flags);
struct zmk_raw_touch_report *zmk_raw_touch_hid_get_report(void);

void zmk_raw_touch_hid_set_feature(uint8_t pads_present, uint8_t resolution, uint8_t orientation,
                                   uint16_t x_max, uint16_t y_max);
struct zmk_raw_touch_feature_report *zmk_raw_touch_hid_get_feature_report(void);
