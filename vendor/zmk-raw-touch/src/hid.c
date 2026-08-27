/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * The module's private HID report descriptor and report state.
 *
 * The descriptor is a single top-level vendor-defined application collection
 * carrying one Input report (a touch frame) and one Feature report (pad
 * capabilities), both on ZMK_RAW_TOUCH_REPORT_ID. It is served verbatim on
 * our own USB HID interface (src/usb_hid.c) and our own HID-over-GATT report
 * map characteristic (src/hog.c); ZMK's own descriptor is never touched.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/usb/class/hid.h>

#include <zephyr/logging/log.h>

/* The module's single LOG_MODULE_REGISTER. Every other file in the module
 * uses LOG_MODULE_DECLARE(zmk_raw_touch, CONFIG_ZMK_RAW_TOUCH_LOG_LEVEL) so
 * the module's log level is settable independently of CONFIG_ZMK_LOG_LEVEL. */
LOG_MODULE_REGISTER(zmk_raw_touch, CONFIG_ZMK_RAW_TOUCH_LOG_LEVEL);

#include <zmk/raw_touch/hid.h>

/* Zephyr's <zephyr/usb/class/hid.h> has HID_USAGE_PAGE() (1-byte payload)
 * but no 2-byte form, which a vendor-defined page such as 0xFF00 needs. */
#ifndef HID_USAGE_PAGE16
#define HID_USAGE_PAGE16(a, b) HID_ITEM(HID_ITEM_TAG_USAGE_PAGE, HID_ITEM_TYPE_GLOBAL, 2), a, b
#endif

/* HID main-item flags: Data (bit 0 clear), Variable (bit 1 set), Absolute
 * (bit 2 clear). Spelled out locally rather than pulled in as ZMK's
 * ZMK_HID_MAIN_VAL_* macros, which live in <zmk/hid.h> alongside a
 * file-scope definition of ZMK's own report descriptor. */
#define RAW_TOUCH_HID_DATA_VAR_ABS 0x02

/* Data usages within the vendor collection. Part of the wire protocol (the
 * host matches on the collection's usage page/usage, then on report ID), so
 * these are fixed rather than Kconfig-tunable. */
#define RAW_TOUCH_HID_USAGE_INPUT 0x02
#define RAW_TOUCH_HID_USAGE_FEATURE 0x03

BUILD_ASSERT(sizeof(struct zmk_raw_touch_report_body) == 7,
             "Raw touch input report body must stay 7 bytes; the host parses it by offset");
BUILD_ASSERT(sizeof(struct zmk_raw_touch_feature_body) == 8,
             "Raw touch feature report body must stay 8 bytes; the host parses it by offset");
BUILD_ASSERT(ZMK_RAW_TOUCH_REPORT_ID > 0 && ZMK_RAW_TOUCH_REPORT_ID <= 0xFF,
             "CONFIG_ZMK_RAW_TOUCH_REPORT_ID must be a non-zero single-byte report ID");

const uint8_t zmk_raw_touch_report_desc[] = {
    HID_USAGE_PAGE16((CONFIG_ZMK_RAW_TOUCH_USAGE_PAGE & 0xFF),
                     ((CONFIG_ZMK_RAW_TOUCH_USAGE_PAGE >> 8) & 0xFF)),
    HID_USAGE(CONFIG_ZMK_RAW_TOUCH_USAGE),
    HID_COLLECTION(HID_COLLECTION_APPLICATION),
    HID_REPORT_ID(ZMK_RAW_TOUCH_REPORT_ID),

    /* Input: one touch frame, 7 bytes.
     * pad_id, x (u16 LE), y (u16 LE), z, flags. */
    HID_USAGE(RAW_TOUCH_HID_USAGE_INPUT),
    HID_LOGICAL_MIN8(0x00),
    /* 16-bit form: logical values are signed, so an 8-bit maximum of 0xFF
     * would be read as -1. */
    HID_LOGICAL_MAX16(0xFF, 0x00),
    HID_REPORT_SIZE(0x08),
    HID_REPORT_COUNT(0x07),
    HID_INPUT(RAW_TOUCH_HID_DATA_VAR_ABS),

    /* Feature: self-describing pad capabilities, 8 bytes. Readable over USB
     * GET_REPORT and the BLE feature report characteristic.
     * protocol_version, pads_present, resolution, orientation,
     * x_max (u16 LE), y_max (u16 LE). */
    HID_USAGE(RAW_TOUCH_HID_USAGE_FEATURE),
    HID_LOGICAL_MIN8(0x00),
    HID_LOGICAL_MAX16(0xFF, 0x00),
    HID_REPORT_SIZE(0x08),
    HID_REPORT_COUNT(0x08),
    HID_FEATURE(RAW_TOUCH_HID_DATA_VAR_ABS),

    HID_END_COLLECTION,
};

const size_t zmk_raw_touch_report_desc_size = sizeof(zmk_raw_touch_report_desc);

static struct zmk_raw_touch_report touch_report = {
    .report_id = ZMK_RAW_TOUCH_REPORT_ID,
    .body = {.pad_id = 0, .x = 0, .y = 0, .z = 0, .flags = 0},
};

void zmk_raw_touch_hid_set(uint8_t pad_id, uint16_t x, uint16_t y, uint8_t z, uint8_t flags) {
    touch_report.body.pad_id = pad_id;
    touch_report.body.x = sys_cpu_to_le16(x);
    touch_report.body.y = sys_cpu_to_le16(y);
    touch_report.body.z = z;
    touch_report.body.flags = flags;
}

struct zmk_raw_touch_report *zmk_raw_touch_hid_get_report(void) { return &touch_report; }

static struct zmk_raw_touch_feature_report touch_feature_report = {
    .report_id = ZMK_RAW_TOUCH_REPORT_ID,
    .body = {.protocol_version = ZMK_RAW_TOUCH_PROTOCOL_VERSION},
};

void zmk_raw_touch_hid_set_feature(uint8_t pads_present, uint8_t resolution, uint8_t orientation,
                                   uint16_t x_max, uint16_t y_max) {
    touch_feature_report.body.pads_present = pads_present;
    touch_feature_report.body.resolution = resolution;
    touch_feature_report.body.orientation = orientation;
    touch_feature_report.body.x_max = sys_cpu_to_le16(x_max);
    touch_feature_report.body.y_max = sys_cpu_to_le16(y_max);
}

struct zmk_raw_touch_feature_report *zmk_raw_touch_hid_get_feature_report(void) {
    return &touch_feature_report;
}
