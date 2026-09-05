/*
 * Copyright (c) 2026 Mrinal Kalakrishnan
 *
 * SPDX-License-Identifier: MIT
 *
 * The module's private HID report descriptor and report state.
 *
 * The descriptor is a single top-level vendor-defined application collection
 * carrying one Input report (a touch frame) and one Feature report (pad
 * capabilities), both on ZMK_RAW_TOUCH_REPORT_ID. It is served verbatim on
 * our own USB HID interface (src/raw_touch_usb_hid.c) and our own
 * HID-over-GATT report map characteristic (src/raw_touch_hog.c); ZMK's own
 * descriptor is never touched.
 */

#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/usb/class/hid.h>

#include <zephyr/logging/log.h>

/* The module's LOG_MODULE_REGISTER lives in src/raw_touch_log.c. */
LOG_MODULE_DECLARE(zmk_raw_touch, CONFIG_ZMK_RAW_TOUCH_LOG_LEVEL);

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

BUILD_ASSERT(sizeof(struct zmk_raw_touch_report_body) == 11,
             "Raw touch input report body must stay 11 bytes; the host parses it by offset");
BUILD_ASSERT(sizeof(struct zmk_raw_touch_feature_pad_slot) == 8,
             "Raw touch feature pad slot must stay 8 bytes; the host parses it by offset");
/* The feature body is a 4-byte header plus one 8-byte slot per pad, so
 * its length is 4 + 8 * N (20 on a two-pad build). Hosts derive N from
 * the report length; nothing here may pad the struct. */
BUILD_ASSERT(sizeof(struct zmk_raw_touch_feature_body) ==
                 4 + 8 * ZMK_RAW_TOUCH_FEATURE_PAD_SLOTS,
             "Raw touch feature report body must stay 4 + 8 * pads bytes; "
             "the host parses it by offset");
/* The descriptor's feature REPORT_COUNT below is a single-byte HID item
 * payload, so the body must fit in a byte. 8 pads = 68 bytes, so this
 * can only trip if the slot count ceiling in hid.h is raised. */
BUILD_ASSERT(sizeof(struct zmk_raw_touch_feature_body) <= 0xFF,
             "Raw touch feature report body no longer fits a one-byte HID REPORT_COUNT");

/* The descriptor declares the pads' REAL logical ranges on the x/y fields,
 * so generic HID tooling sees true geometry without parsing the feature
 * report. The values come straight from the zmk,raw-touch-pad node: this
 * file only compiles under CONFIG_ZMK_RAW_TOUCH, which depends on
 * DT_HAS_ZMK_RAW_TOUCH_PAD_ENABLED, so a status-okay node always exists
 * here.
 *
 * With several pads, DT_COMPAT_GET_ANY_STATUS_OKAY picks one of them: the
 * compile-time descriptor can only carry one geometry, so it describes that
 * any-instance pad and the per-pad truth lives in the feature report's
 * slots. Hosts consuming multi-pad streams must use the slots, not the
 * descriptor. */
#define RAW_TOUCH_DESC_PAD_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(zmk_raw_touch_pad)
#define RAW_TOUCH_DESC_X_MAX DT_PROP(RAW_TOUCH_DESC_PAD_NODE, x_max)
#define RAW_TOUCH_DESC_Y_MAX DT_PROP(RAW_TOUCH_DESC_PAD_NODE, y_max)

/* HID logical values are signed, so a 16-bit Logical Maximum item tops out
 * at 0x7FFF. (That is also why the 8-bit fields below use the 16-bit item
 * form for a maximum of 0xFF, and the timestamp the 32-bit form for
 * 0xFFFF.) */
BUILD_ASSERT(RAW_TOUCH_DESC_X_MAX > 0 && RAW_TOUCH_DESC_X_MAX <= 0x7FFF,
             "raw touch pad x-max must fit a 16-bit signed HID logical maximum");
BUILD_ASSERT(RAW_TOUCH_DESC_Y_MAX > 0 && RAW_TOUCH_DESC_Y_MAX <= 0x7FFF,
             "raw touch pad y-max must fit a 16-bit signed HID logical maximum");

const uint8_t zmk_raw_touch_report_desc[] = {
    HID_USAGE_PAGE16((ZMK_RAW_TOUCH_USAGE_PAGE & 0xFF), ((ZMK_RAW_TOUCH_USAGE_PAGE >> 8) & 0xFF)),
    HID_USAGE(ZMK_RAW_TOUCH_USAGE),
    HID_COLLECTION(HID_COLLECTION_APPLICATION),
    HID_REPORT_ID(ZMK_RAW_TOUCH_REPORT_ID),

    /* Input: one touch frame, 11 bytes, split into per-field items so each
     * carries its real logical range.
     * pad_id, contact_id, x (u16 LE), y (u16 LE), z, flags, seq,
     * timestamp (u16 LE). Logical Minimum is a global item, so the 0 set
     * here holds for every field below. */

    /* pad_id, contact_id: 8-bit, 0..255. */
    HID_USAGE(RAW_TOUCH_HID_USAGE_INPUT),
    HID_LOGICAL_MIN8(0x00),
    HID_LOGICAL_MAX16(0xFF, 0x00),
    HID_REPORT_SIZE(0x08),
    HID_REPORT_COUNT(0x02),
    HID_INPUT(RAW_TOUCH_HID_DATA_VAR_ABS),

    /* x: 16-bit, 0..x-max. */
    HID_USAGE(RAW_TOUCH_HID_USAGE_INPUT),
    HID_LOGICAL_MAX16((RAW_TOUCH_DESC_X_MAX & 0xFF), ((RAW_TOUCH_DESC_X_MAX >> 8) & 0xFF)),
    HID_REPORT_SIZE(0x10),
    HID_REPORT_COUNT(0x01),
    HID_INPUT(RAW_TOUCH_HID_DATA_VAR_ABS),

    /* y: 16-bit, 0..y-max. */
    HID_USAGE(RAW_TOUCH_HID_USAGE_INPUT),
    HID_LOGICAL_MAX16((RAW_TOUCH_DESC_Y_MAX & 0xFF), ((RAW_TOUCH_DESC_Y_MAX >> 8) & 0xFF)),
    HID_REPORT_SIZE(0x10),
    HID_REPORT_COUNT(0x01),
    HID_INPUT(RAW_TOUCH_HID_DATA_VAR_ABS),

    /* z, flags, seq: 8-bit, 0..255. */
    HID_USAGE(RAW_TOUCH_HID_USAGE_INPUT),
    HID_LOGICAL_MAX16(0xFF, 0x00),
    HID_REPORT_SIZE(0x08),
    HID_REPORT_COUNT(0x03),
    HID_INPUT(RAW_TOUCH_HID_DATA_VAR_ABS),

    /* timestamp: 16-bit, 0..65535. */
    HID_USAGE(RAW_TOUCH_HID_USAGE_INPUT),
    HID_LOGICAL_MAX32(0xFF, 0xFF, 0x00, 0x00),
    HID_REPORT_SIZE(0x10),
    HID_REPORT_COUNT(0x01),
    HID_INPUT(RAW_TOUCH_HID_DATA_VAR_ABS),

    /* Feature: self-describing pad capabilities, uniform 8-bit fields
     * (its u16 members are protocol-level structure the host parses by
     * offset; the descriptor does not model them). Readable over USB
     * GET_REPORT and the BLE feature report characteristic.
     * protocol_version, pads_present, capabilities, reserved, then one
     * 8-byte slot per pad compiled in: 4 + 8 * N bytes, 20 on the two-pad
     * reference build.
     *
     * The count is taken from sizeof() rather than written out, so the
     * descriptor and the struct cannot drift apart when the pad count
     * changes. Note that it changing at all is a report-map change, which
     * on macOS means the host must forget and re-pair. */
    HID_USAGE(RAW_TOUCH_HID_USAGE_FEATURE),
    HID_LOGICAL_MAX16(0xFF, 0x00),
    HID_REPORT_SIZE(0x08),
    HID_REPORT_COUNT(sizeof(struct zmk_raw_touch_feature_body)),
    HID_FEATURE(RAW_TOUCH_HID_DATA_VAR_ABS),

    HID_END_COLLECTION,
};

const size_t zmk_raw_touch_report_desc_size = sizeof(zmk_raw_touch_report_desc);

static struct zmk_raw_touch_report touch_report = {
    .report_id = ZMK_RAW_TOUCH_REPORT_ID,
    /* contact_id stays 0 for the life of the report: the module only
     * streams single-touch pads. */
    .body = {.pad_id = 0, .contact_id = 0, .x = 0, .y = 0, .z = 0, .flags = 0, .seq = 0,
             .timestamp = 0},
};

void zmk_raw_touch_hid_set(uint8_t pad_id, uint16_t x, uint16_t y, uint8_t z, uint8_t flags,
                           uint8_t seq, uint16_t timestamp) {
    touch_report.body.pad_id = pad_id;
    touch_report.body.x = sys_cpu_to_le16(x);
    touch_report.body.y = sys_cpu_to_le16(y);
    touch_report.body.z = z;
    touch_report.body.flags = flags;
    touch_report.body.seq = seq;
    touch_report.body.timestamp = sys_cpu_to_le16(timestamp);
}

struct zmk_raw_touch_report *zmk_raw_touch_hid_get_report(void) { return &touch_report; }

static struct zmk_raw_touch_feature_report touch_feature_report = {
    .report_id = ZMK_RAW_TOUCH_REPORT_ID,
    .body = {.protocol_version = ZMK_RAW_TOUCH_PROTOCOL_VERSION,
             /* The host claim needs a host-facing transport for its
              * writes; without one the capability must not be advertised.
              * (Moot in practice: with no transport there is no host to
              * read this report either.) */
             .capabilities = (IS_ENABLED(CONFIG_ZMK_RAW_TOUCH_USB) ||
                              IS_ENABLED(CONFIG_ZMK_RAW_TOUCH_BLE))
                                 ? ZMK_RAW_TOUCH_CAP_HOST_CLAIM
                                 : 0},
};

void zmk_raw_touch_hid_set_feature_header(uint8_t pads_present) {
    touch_feature_report.body.pads_present = pads_present;
}

void zmk_raw_touch_hid_set_feature_slot(int slot, uint8_t resolution, uint8_t orientation,
                                        uint16_t x_max, uint16_t y_max, uint8_t max_contacts) {
    if (slot < 0 || slot >= ZMK_RAW_TOUCH_FEATURE_PAD_SLOTS) {
        LOG_ERR("Raw touch feature slot %d out of range", slot);
        return;
    }

    struct zmk_raw_touch_feature_pad_slot *pad = &touch_feature_report.body.pads[slot];

    pad->resolution = resolution;
    pad->orientation = orientation;
    pad->x_max = sys_cpu_to_le16(x_max);
    pad->y_max = sys_cpu_to_le16(y_max);
    pad->max_contacts = max_contacts;
}

struct zmk_raw_touch_feature_report *zmk_raw_touch_hid_get_feature_report(void) {
    return &touch_feature_report;
}
