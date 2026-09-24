/*
 * Copyright (c) 2026 Mrinal Kalakrishnan
 *
 * SPDX-License-Identifier: MIT
 *
 * Wire format for raw touch frames (protocol v4).
 *
 * docs/protocol.md is the normative description of this
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

#include <zmk/raw_touch/version.h>

/* Protocol identity, fixed rather than Kconfig-tunable: hosts match the
 * application collection by usage page/usage and then the report ID, so a
 * different value would be silently invisible to every host. */
#define ZMK_RAW_TOUCH_USAGE_PAGE 0xFF00
#define ZMK_RAW_TOUCH_USAGE 0x01
#define ZMK_RAW_TOUCH_REPORT_ID 0x04

/* Input report: one 11-byte frame, emitted per pad sample while touched
 * and a host lease is held (~100 Hz), plus exactly one release frame
 * (touched = 0, z = 0) on lift-off. */

/* Touched. Clear marks a RELEASE frame - the one emitted at lift-off, and
 * the single synthetic one emitted when a host lease lapses mid-touch.
 * Release frames are the only frames whose delivery matters: a lost one
 * leaves the host holding a phantom finger-down. Both transports treat
 * them as durable - each queues frames in a ring that evicts motion
 * frames rather than a release, in order, and BLE additionally retries a
 * release that fails to notify (see zmk_raw_touch_hog_send_report() and
 * zmk_raw_touch_usb_send_report() in zmk/raw_touch/transport.h). Motion
 * frames may still be dropped under pressure; `seq` exposes that. */
#define ZMK_RAW_TOUCH_FLAGS_TOUCHED BIT(0)
#define ZMK_RAW_TOUCH_FLAGS_SCROLL_MODE BIT(1)
/* Lease held: set iff the endpoint this frame is being sent to held a
 * live host lease when the frame was sampled, i.e. the scroll-context
 * wheel fallback is suppressed for it. Since frames are only emitted
 * while a lease is held, this is implied-set on ordinary frames; the one
 * frame carrying it clear is the single synthetic release emitted when a
 * lease lapses mid-touch. Hosts synthesize scroll only when this and
 * SCROLL_MODE are both set, making wheel and synthesized scroll mutually
 * exclusive by construction (see zmk/raw_touch/lease.h). */
#define ZMK_RAW_TOUCH_FLAGS_LEASE_HELD BIT(2)

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
 * Its body is 16 + 8 * N bytes, where N = ZMK_RAW_TOUCH_FEATURE_PAD_SLOTS
 * is the number of pads compiled into this firmware - 32 bytes on the
 * two-pad reference build (33 over USB, where the control transfer
 * carries the report ID as its first byte). Hosts MUST accept any body
 * length of that form and MUST NOT hard-code 32. Pads beyond the 8-slot
 * ceiling (pads_present is an 8-bit mask) still set their bit but get no
 * slot.
 *
 * Hosts MUST read and validate this before treating a vendor collection on
 * this usage page as the raw touch protocol - 0xFF00/0x01 is a commonly
 * squatted vendor pair. Validation means all three of: the magic below,
 * protocol_version, and a body length of the 16 + 8 * N shape. */

#define ZMK_RAW_TOUCH_PROTOCOL_VERSION 4

/* Identification magic: the ASCII bytes "RAWT", at a fixed offset in the
 * feature report body. This is what makes an unrelated device on the
 * generic 0xFF00/0x01 vendor pair distinguishable from a raw touch
 * keyboard, so a host MUST reject a body whose magic does not match and
 * MUST NOT write a lease command to such a device. Identification, not
 * authentication: it carries no secret and proves nothing about who is
 * talking. */
#define ZMK_RAW_TOUCH_MAGIC_0 'R'
#define ZMK_RAW_TOUCH_MAGIC_1 'A'
#define ZMK_RAW_TOUCH_MAGIC_2 'W'
#define ZMK_RAW_TOUCH_MAGIC_3 'T'
#define ZMK_RAW_TOUCH_MAGIC_INIT                                                                   \
    { ZMK_RAW_TOUCH_MAGIC_0, ZMK_RAW_TOUCH_MAGIC_1, ZMK_RAW_TOUCH_MAGIC_2, ZMK_RAW_TOUCH_MAGIC_3 }

/* Device id: the SoC's own hardware identifier, from Zephyr's
 * hwinfo_get_device_id() (the FICR DEVICEID pair on an nRF52), byte order
 * exactly as hwinfo returns it. Eight bytes because that is what the
 * supported SoCs report; a shorter answer is zero-padded on the right, a
 * longer one truncated, and all-zero means "this build could not tell"
 * (no HWINFO driver for the SoC).
 *
 * It is a stable per-chip identifier, so a host can recognize the SAME
 * keyboard across transports - USB and Bluetooth expose no common
 * identifier otherwise. Readable by any host that can read the feature
 * report, which on BLE means a bonded one: an identifier, NOT a secret,
 * and nothing may be authorized on the strength of it. Hosts MUST NOT
 * reject a device over this field's value, including all-zero. */
#define ZMK_RAW_TOUCH_DEVICE_ID_LEN 8

/* Capabilities bit 0: host lease supported - the host may acquire a lease
 * on the stream by writing the feature report (see zmk/raw_touch/lease.h),
 * switching the pads from standard (firmware-driven) scrolling to
 * host-driven scrolling. Advertised whenever a host-facing transport is
 * built; hosts MUST check this bit before acquiring a lease. */
#define ZMK_RAW_TOUCH_CAP_HOST_LEASE BIT(0)

/* Capabilities bit 1: tap confirm supported - the firmware accepts the
 * ZMK_RAW_TOUCH_CMD_TAP_CONFIRM feature command (see zmk/raw_touch/tap.h).
 * Advertised with the lease; a host MUST check it before writing one. */
#define ZMK_RAW_TOUCH_CAP_TAP_CONFIRM BIT(1)

/* Pad slot byte +1. Bits 0-2 are the pad's mounting; bit 3 says the pad
 * parks its scroll-context taps for host confirmation while a lease is
 * held (tap-click together with tap-click-while-scrolling on the node). */
#define ZMK_RAW_TOUCH_ORIENT_ROTATE_90 BIT(0)
#define ZMK_RAW_TOUCH_ORIENT_X_INVERT BIT(1)
#define ZMK_RAW_TOUCH_ORIENT_Y_INVERT BIT(2)
#define ZMK_RAW_TOUCH_PAD_PARKS_SCROLL_TAPS BIT(3)

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
 * forget + re-pair on the host (see the README's Troubleshooting; the
 * failure is deceptively partial - USB fine, BLE frames unparseable). */
#define ZMK_RAW_TOUCH_FEATURE_PAD_SLOTS CLAMP(DT_NUM_INST_STATUS_OKAY(zmk_raw_touch_pad), 1, 8)

struct zmk_raw_touch_feature_pad_slot {
    uint8_t resolution;   /* counts/mm, 0 = unknown */
    uint8_t orientation;  /* ZMK_RAW_TOUCH_ORIENT_* | ZMK_RAW_TOUCH_PAD_* flags */
    uint16_t x_max;       /* little-endian */
    uint16_t y_max;       /* little-endian */
    uint8_t max_contacts; /* 1 on a Pinnacle */
    /* Packed module version (zmk/raw_touch/version.h) of the half that
     * OWNS this pad - this one for a local pad, the peripheral's own for a
     * relayed pad, which announces it over the split link (0 until it
     * has; see zmk/raw_touch/split_stamp.h). */
    uint8_t module_version;
} __packed;

struct zmk_raw_touch_feature_body {
    uint8_t protocol_version; /* ZMK_RAW_TOUCH_PROTOCOL_VERSION */
    uint8_t pads_present;     /* bit N set if pad-id N exists */
    uint8_t capabilities;     /* ZMK_RAW_TOUCH_CAP_* */
    /* Packed module version of the half answering this report, i.e. the
     * central. Diagnostic only: protocol_version above is what a host
     * keys its parsing off. */
    uint8_t module_version;
    /* ZMK_RAW_TOUCH_MAGIC_*, the ASCII bytes "RAWT". Fixed for the life
     * of the protocol; protocol_version stays at byte 0 ahead of it so a
     * host can read the version before it knows the layout. */
    uint8_t magic[4];
    /* ZMK_RAW_TOUCH_DEVICE_ID_LEN bytes of hwinfo device id; all-zero
     * when this build could not read one. Diagnostic and identifying,
     * never a compatibility or admission check. */
    uint8_t device_id[ZMK_RAW_TOUCH_DEVICE_ID_LEN];
    /* Present pads in ascending pad-id order, one slot per pad compiled
     * in. Hosts recover N from the report length as (len - 16) / 8 and
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
/* Copies up to ZMK_RAW_TOUCH_DEVICE_ID_LEN bytes of hwinfo device id into
 * the feature report, zero-padding a shorter id and ignoring the tail of a
 * longer one. Called once at init; the field is constant thereafter. */
void zmk_raw_touch_hid_set_feature_device_id(const uint8_t *device_id, size_t len);
void zmk_raw_touch_hid_set_feature_slot(int slot, uint8_t resolution, uint8_t orientation,
                                        uint16_t x_max, uint16_t y_max, uint8_t max_contacts,
                                        uint8_t module_version);
/* A relayed pad's owning half announces its version over the split link,
 * which happens long after init has filled the slots - so that one byte
 * alone can be set again later. */
void zmk_raw_touch_hid_set_feature_slot_version(int slot, uint8_t module_version);
struct zmk_raw_touch_feature_report *zmk_raw_touch_hid_get_feature_report(void);
