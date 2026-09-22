/*
 * Copyright (c) 2026 Mrinal Kalakrishnan
 *
 * SPDX-License-Identifier: MIT
 *
 * What a split peripheral tells the central about a relayed pad: the
 * sample time of every frame, and its own module version.
 *
 * A pad on a split peripheral reaches the module through ZMK's
 * `zmk,input-split` relay, and the frame handler on the central stamps
 * each frame when it processes it - after the split hop. On a polled
 * wired link that puts up to one poll cycle of link jitter into the
 * frame's `timestamp` field (measured on the Go60: bimodal 0.3 / 22.8 ms
 * inter-frame spacing at stock timings, ~5 ms worst with the README's
 * cadence tweak), which a host cannot tell apart from finger motion when
 * it derives velocity from the timestamps.
 *
 * The fix is one extra input event per frame, sent by the peripheral
 * through the same relay just ahead of the frame's sync event: a
 * vendor-typed event whose value is the peripheral's uptime in the wire
 * timestamp's units. Nothing in ZMK core acts on the type (the central's
 * input listener dispatches only REL/ABS/KEY), and every processor in this
 * module passes it through; the pad's frame handler latches it and uses
 * it in place of its own clock for the frame that closes next.
 *
 * The stamp is in the PERIPHERAL's clock domain - the halves' clocks are
 * not synchronised. That matches what the wire protocol already asks of
 * hosts: one reconstructed timeline per pad, never a comparison of
 * timestamps across pads.
 */

#pragma once

#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>

#include <zmk/raw_touch/version.h>

/** Event type of both messages: the first vendor-specific type Zephyr
 * reserves. The code tells them apart. */
#define ZMK_RAW_TOUCH_SPLIT_STAMP_TYPE INPUT_EV_VENDOR_START

/** Event code of the stamp ('T' 'S'); distinctive, so a stray vendor event
 * from something else is not mistaken for one. */
#define ZMK_RAW_TOUCH_SPLIT_STAMP_CODE 0x5453

/** Event code of the version announcement ('T' 'V'), whose value is the
 * peripheral's ZMK_RAW_TOUCH_MODULE_VERSION_PACKED. The central publishes
 * it in the relayed pad's feature-report slot, so a host reading that
 * report learns which build each half is running, not just the central's.
 *
 * Sent ahead of the first frame of every touch rather than once when the
 * split link comes up: ZMK raises its peripheral connected event only on
 * the BLE split (src/split/bluetooth/peripheral.c), so a wired split - the
 * reference board - would never announce at all. Per touch also re-teaches
 * a central that rebooted while the peripheral kept running, and one extra
 * 4-byte event per touch is nothing beside the per-frame stamp. The cost
 * is that the version reads 0 until the user's first touch. */
#define ZMK_RAW_TOUCH_SPLIT_VERSION_CODE 0x5456

/** Now, in the wire timestamp's units (100 us, HID Scan Time convention).
 * The full 32-bit count travels in the event's value; the frame handler
 * truncates it to the report's 16 bits. */
static inline uint32_t zmk_raw_touch_split_stamp_now(void) {
    return (uint32_t)(k_ticks_to_us_floor64(k_uptime_ticks()) / 100);
}
