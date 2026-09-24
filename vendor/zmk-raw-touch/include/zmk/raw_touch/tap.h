/*
 * Copyright (c) 2026 Mrinal Kalakrishnan
 *
 * SPDX-License-Identifier: MIT
 *
 * Host-confirmed taps on a scrolling pad.
 *
 * A pad with tap-click-while-scrolling reports taps for touches that were
 * in scroll context. In Standard mode it emits them itself at lift-off.
 * In RawTouch mode it cannot tell a tap from a touch that merely caught a
 * coasting momentum tail - only the host, which runs the momentum, knows
 * that - so it parks the tap instead and emits it only when the host
 * confirms it with the feature-report command below. The keymap still
 * decides what the tap does: the confirmed tap is injected into the pad's
 * listener chain exactly like an unparked one.
 */

#pragma once

#include <stdint.h>

/* body[0] of a 4-byte feature SET command (same framing as the lease
 * command, see zmk/raw_touch/lease.h): confirm the parked tap on the pad
 * in body[1]; bytes 2-3 reserved, must be 0. Honored only from the
 * endpoint that holds the lease. Harmless when nothing is parked. */
#define ZMK_RAW_TOUCH_CMD_TAP_CONFIRM 0x02

/* How long a parked tap waits for its confirmation. One SET_REPORT after
 * lift-off is a few milliseconds over USB and a connection interval or two
 * over BLE; a confirmation later than this belongs to a touch the user has
 * moved on from. */
#define ZMK_RAW_TOUCH_TAP_CONFIRM_WINDOW_MS 250

/**
 * @brief Emit the tap parked on a pad, if one is waiting.
 *
 * @param pad_id The pad the host is confirming.
 * @retval 0 A tap was emitted, or none was parked / it had expired.
 * @retval -EINVAL No such pad.
 */
int zmk_raw_touch_tap_confirm(uint8_t pad_id);
