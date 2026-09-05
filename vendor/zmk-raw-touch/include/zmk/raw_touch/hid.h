/*
 * Copyright (c) 2026 Mrinal Kalakrishnan
 *
 * SPDX-License-Identifier: MIT
 *
 * Wire format for raw touch frames (protocol v3).
 *
 * The README's wire-format appendix is the normative description of this
 * layout; the structs here are its C spelling and must match it byte for
 * byte (src/raw_touch_hid.c BUILD_ASSERTs the sizes).
 *
 * The module owns a private HID report descriptor containing exactly one
 * top-level vendor-defined application collection, carried on its own USB
 * HID interface and its own HID-over-GATT service instance. Nothing here
 * touches ZMK's own descriptor, which is why no fork of ZMK is needed.
 */

#pragma once

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

/* Protocol identity, fixed rather than Kconfig-tunable: hosts match the
 * application collection by usage page/usage and then the report ID, so a
 * different value would be silently invisible to every host. */
#define ZMK_RAW_TOUCH_USAGE_PAGE 0xFF00
#define ZMK_RAW_TOUCH_USAGE 0x01
#define ZMK_RAW_TOUCH_REPORT_ID 0x04

/* Input report: one 11-byte frame, emitted per pad sample while touched
 * (~100 Hz), plus exactly one release frame (touched = 0, z = 0) on
 * lift-off. */

/* Touched. Clear marks a RELEASE frame - the one emitted at lift-off, and
 * the single synthetic one emitted when a host claim clears mid-touch.
 * Release frames are the only frames whose delivery matters: a lost one
 * leaves the host holding a phantom finger-down. The transports treat
 * them as durable - the BLE queue evicts motion frames rather than a
 * release and retries a release that fails to notify, in order (see
 * zmk_raw_touch_hog_send_report() in zmk/raw_touch/transport.h). Motion
 * frames may still be dropped under pressure; `seq` exposes that. */
#define ZMK_RAW_TOUCH_FLAGS_TOUCHED BIT(0)
#define ZMK_RAW_TOUCH_FLAGS_SCROLL_MODE BIT(1)
/* Host claimed: set iff the endpoint this frame is being sent to held a
 * live host claim when the frame was sampled, i.e. the scroll-context
 * wheel fallback is suppressed for it. Since frames are only emitted
 * while claimed, this is implied-set on ordinary frames; the one frame
 * carrying it clear is the single synthetic release emitted when a claim
 * clears mid-touch. Hosts synthesize scroll only when this and
 * SCROLL_MODE are both set, making wheel and synthesized scroll mutually
 * exclusive by construction (see zmk/raw_touch/gate.h). */
#define ZMK_RAW_TOUCH_FLAGS_HOST_CLAIMED BIT(2)

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
 *
 * Its body is 4 + 8 * N bytes, where N = ZMK_RAW_TOUCH_FEATURE_PAD_SLOTS
 * is the number of pads compiled into this firmware - 20 bytes on the
 * two-pad reference build (21 over USB, where the control transfer
 * carries the report ID as its first byte). Hosts MUST accept any body
 * length of that form and MUST NOT hard-code 20. Pads beyond the 8-slot
 * ceiling (pads_present is an 8-bit mask) still set their bit but get no
 * slot.
 *
 * Hosts MUST read and validate this before treating a vendor collection on
 * this usage page as the raw touch protocol - 0xFF00/0x01 is a commonly
 * squatted vendor pair. */

#define ZMK_RAW_TOUCH_PROTOCOL_VERSION 3

/* Capabilities bit 0: the host claim is supported - the host may claim
 * the stream by writing the feature report (see zmk/raw_touch/gate.h),
 * switching the pads from standard (firmware-driven) scrolling to
 * host-driven scrolling. Advertised whenever a host-facing transport is
 * built; hosts MUST check this bit before attempting a claim. */
#define ZMK_RAW_TOUCH_CAP_HOST_CLAIM BIT(0)

#define ZMK_RAW_TOUCH_ORIENT_ROTATE_90 BIT(0)
#define ZMK_RAW_TOUCH_ORIENT_X_INVERT BIT(1)
#define ZMK_RAW_TOUCH_ORIENT_Y_INVERT BIT(2)

/* One slot per pad compiled in, so the report is exactly as long as the
 * hardware needs and no longer. Derived from the devicetree rather than
 * from a Kconfig knob or a fixed cap: the pad instances are what
 * src/raw_touch.c enumerates, so the two can never disagree.
 *
 * Floor of 1 keeps struct zmk_raw_touch_feature_body well-formed (and
 * the HID descriptor's feature REPORT_COUNT nonzero) if this header is
 * ever pulled into a build with no pad node. Ceiling of 8 is the
 * pads_present bitmask's width.
 *
 * WARNING: changing the pad count changes the report body's length,
 * which changes the HID report descriptor - and macOS caches the HOGP
 * report map at pairing time. Adding or removing a pad therefore needs a
 * forget + re-pair on the host (see the README's Known issues; the
 * failure is deceptively partial - USB fine, BLE frames unparseable). */
#define ZMK_RAW_TOUCH_FEATURE_PAD_SLOTS CLAMP(DT_NUM_INST_STATUS_OKAY(zmk_raw_touch_pad), 1, 8)

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
    uint8_t capabilities;     /* ZMK_RAW_TOUCH_CAP_* */
    uint8_t reserved;         /* 0 */
    /* Present pads in ascending pad-id order, one slot per pad compiled
     * in. Hosts recover N from the report length as (len - 4) / 8 and
     * read min(N, popcount(pads_present)) slots; any slot they do not
     * account for that way is zeroed. */
    struct zmk_raw_touch_feature_pad_slot pads[ZMK_RAW_TOUCH_FEATURE_PAD_SLOTS];
} __packed;

struct zmk_raw_touch_feature_report {
    uint8_t report_id;
    struct zmk_raw_touch_feature_body body;
} __packed;

/* The module's private report descriptor, defined in src/raw_touch_hid.c. */
extern const uint8_t zmk_raw_touch_report_desc[];
extern const size_t zmk_raw_touch_report_desc_size;

/* Report state accessors (src/raw_touch_hid.c). The caller owns seq and timestamp:
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
