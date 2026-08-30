/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * Device-side mode gate (protocol v3, capability bit 0).
 *
 * A host that consumes the raw touch stream CLAIMS it by writing a
 * 4-byte command to the feature report (USB SET_REPORT(FEATURE) or a
 * GATT write to the BLE feature-report characteristic). While a claim is
 * held for the endpoint instance ZMK currently sends to, the module
 * suppresses the scroll-context wheel fallback it would otherwise
 * re-inject, and sets frame flags bit 2 so the host knows the wheel is
 * off. Wheel and synthesized scroll are thereby mutually exclusive by
 * construction: hosts synthesize scroll only on frames with bit 2 set.
 *
 * A claim is scoped to the endpoint instance the write arrived on
 * (transport + BLE profile) and is kept alive by the host re-writing it
 * at no more than half its own timeout. It clears on timeout expiry,
 * explicit release, USB detach, disconnect of the claiming BLE profile,
 * and any endpoint switch away from the claiming endpoint - a dead or
 * absent host must never leave the wheel fallback dead.
 */

#pragma once

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>

#include <zmk/endpoints_types.h>

/* Claim command: the 4-byte SET-feature-report body (over USB the control
 * payload may additionally carry the leading report-ID byte). */
#define ZMK_RAW_TOUCH_GATE_CMD_LEN 4

/* body[0]: command. 0x01 = gate claim; everything else is rejected. */
#define ZMK_RAW_TOUCH_GATE_CMD_CLAIM 0x01

/* body[1]: operation. */
#define ZMK_RAW_TOUCH_GATE_OP_RELEASE 0x00
#define ZMK_RAW_TOUCH_GATE_OP_CLAIM 0x01 /* claim or refresh */

/* body[2]: timeout in seconds. 0 is rejected; nonzero values are clamped
 * to [ZMK_RAW_TOUCH_GATE_TIMEOUT_MIN_S, ZMK_RAW_TOUCH_GATE_TIMEOUT_MAX_S].
 * Ignored on release. */
#define ZMK_RAW_TOUCH_GATE_TIMEOUT_MIN_S 5
#define ZMK_RAW_TOUCH_GATE_TIMEOUT_MAX_S 120

/* body[3]: reserved, must be 0. */

#if IS_ENABLED(CONFIG_ZMK_RAW_TOUCH_USB) || IS_ENABLED(CONFIG_ZMK_RAW_TOUCH_BLE)

/**
 * @brief Handle a gate command written by the host.
 *
 * @param source The endpoint instance the write arrived on.
 * @param body The command body (without any report-ID prefix).
 * @param len Length of @p body; must be ZMK_RAW_TOUCH_GATE_CMD_LEN.
 *
 * @retval 0 on success.
 * @retval -EMSGSIZE on a wrong length.
 * @retval -ENOTSUP on an unknown command byte.
 * @retval -EINVAL on a bad operation, timeout or reserved byte.
 */
int zmk_raw_touch_gate_handle_command(struct zmk_endpoint_instance source, const uint8_t *body,
                                      size_t len);

/**
 * @brief Whether the gate is engaged for the currently-selected endpoint.
 *
 * True iff the endpoint instance zmk_endpoints_selected() returns holds a
 * live claim. Cheap enough to call once per frame; this is what decides
 * both frame flags bit 2 and the wheel-fallback suppression, so the two
 * can never disagree.
 */
bool zmk_raw_touch_gate_engaged_for_selected(void);

#else /* no host-facing transport: split peripheral, or both disabled */

static inline int zmk_raw_touch_gate_handle_command(struct zmk_endpoint_instance source,
                                                    const uint8_t *body, size_t len) {
    ARG_UNUSED(source);
    ARG_UNUSED(body);
    ARG_UNUSED(len);
    return -ENOTSUP;
}

static inline bool zmk_raw_touch_gate_engaged_for_selected(void) { return false; }

#endif
