/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * Wire format for the raw touch stream (protocol v3).
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

/* Input report: one 11-byte frame, emitted per pad sample while touched
 * (~100 Hz), plus exactly one release frame (touched = 0, z = 0) on
 * lift-off. */

#define ZMK_RAW_TOUCH_FLAGS_TOUCHED BIT(0)
#define ZMK_RAW_TOUCH_FLAGS_SCROLL_MODE BIT(1)
/* Gate engaged: set iff the endpoint this frame is being sent to holds a
 * live mode-gate claim, i.e. the scroll-context wheel fallback is
 * suppressed for it. Hosts synthesize scroll only when this and
 * SCROLL_MODE are both set, making wheel and synthesized scroll mutually
 * exclusive by construction (see zmk/raw_touch/gate.h). */
#define ZMK_RAW_TOUCH_FLAGS_MODE_GATE BIT(2)

struct zmk_raw_touch_report_body {
    uint8_t pad_id;
    uint8_t contact_id; /* 0 on single-touch pads */
    uint16_t x;         /* little-endian, raw pad counts */
    uint16_t y;         /* little-endian, raw pad counts */
    uint8_t z;          /* touch strength */
    uint8_t flags;      /* ZMK_RAW_TOUCH_FLAGS_* */
    uint8_t seq;        /* per-pad counter, +1 per emitted report, wraps */
    uint16_t timestamp; /* little-endian, 100 us units, wraps at 6.5536 s */
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

#define ZMK_RAW_TOUCH_PROTOCOL_VERSION 3

/* The usage pair is the protocol's identity: hosts match the application
 * collection by it, so it is fixed, not tunable (unlike the report ID,
 * which keeps its Kconfig justification). */
#define ZMK_RAW_TOUCH_USAGE_PAGE 0xFF00
#define ZMK_RAW_TOUCH_USAGE 0x01

/* Capabilities bit 0: the mode gate is supported - the host may claim the
 * stream by writing the feature report (see zmk/raw_touch/gate.h).
 * Advertised whenever a host-facing transport is built; hosts MUST check
 * this bit before attempting a claim. */
#define ZMK_RAW_TOUCH_CAP_MODE_GATE BIT(0)

#define ZMK_RAW_TOUCH_ORIENT_ROTATE_90 BIT(0)
#define ZMK_RAW_TOUCH_ORIENT_X_INVERT BIT(1)
#define ZMK_RAW_TOUCH_ORIENT_Y_INVERT BIT(2)

#define ZMK_RAW_TOUCH_FEATURE_PAD_SLOTS 2

struct zmk_raw_touch_feature_pad_slot {
    uint8_t resolution;   /* counts/mm, 0 = unknown */
    uint8_t orientation;  /* ZMK_RAW_TOUCH_ORIENT_* */
    uint16_t x_max;       /* little-endian */
    uint16_t y_max;       /* little-endian */
    uint8_t max_contacts; /* 1 on a Pinnacle */
    uint8_t reserved;     /* 0 */
} __packed;

struct zmk_raw_touch_feature_body {
    uint8_t protocol_version; /* ZMK_RAW_TOUCH_PROTOCOL_VERSION */
    uint8_t pads_present;     /* bit N set if pad-id N exists */
    uint8_t capabilities;     /* ZMK_RAW_TOUCH_CAP_*; all bits 0 today */
    uint8_t reserved;         /* 0 */
    /* Present pads in ascending pad-id order; unused trailing slots stay
     * zeroed. */
    struct zmk_raw_touch_feature_pad_slot pads[ZMK_RAW_TOUCH_FEATURE_PAD_SLOTS];
} __packed;

struct zmk_raw_touch_feature_report {
    uint8_t report_id;
    struct zmk_raw_touch_feature_body body;
} __packed;

/* The module's private report descriptor, defined in src/hid.c. */
extern const uint8_t zmk_raw_touch_report_desc[];
extern const size_t zmk_raw_touch_report_desc_size;

/* Report state accessors (src/hid.c). The caller owns seq and timestamp:
 * seq is per-pad state (src/raw_touch.c) and timestamp is the device-side
 * sample time in 100 us units, so neither can be stamped here without
 * losing meaning. contact_id is fixed at 0 while the module only streams
 * single-touch pads. */
void zmk_raw_touch_hid_set(uint8_t pad_id, uint16_t x, uint16_t y, uint8_t z, uint8_t flags,
                           uint8_t seq, uint16_t timestamp);
struct zmk_raw_touch_report *zmk_raw_touch_hid_get_report(void);

void zmk_raw_touch_hid_set_feature_header(uint8_t pads_present);
void zmk_raw_touch_hid_set_feature_slot(int slot, uint8_t resolution, uint8_t orientation,
                                        uint16_t x_max, uint16_t y_max, uint8_t max_contacts);
struct zmk_raw_touch_feature_report *zmk_raw_touch_hid_get_feature_report(void);
