/*
 * Copyright (c) 2026 Mrinal Kalakrishnan
 *
 * SPDX-License-Identifier: MIT
 *
 * Device-side host-lease state (protocol v4, capability bit 0).
 *
 * A host that consumes raw touch frames ACQUIRES A LEASE on them by
 * writing a 4-byte command to the feature report (USB SET_REPORT(FEATURE)
 * or a GATT write to the BLE feature-report characteristic). While a lease
 * is held for the endpoint instance ZMK currently sends to, the module
 * suppresses the scroll-context wheel fallback it would otherwise
 * re-inject, and sets frame flags bit 2 so the host knows the wheel is
 * off. Wheel and synthesized scroll are thereby mutually exclusive by
 * construction: hosts synthesize scroll only on frames with bit 2 set.
 *
 * A lease is scoped to the endpoint instance the write arrived on
 * (transport + BLE profile) and is kept alive by the host renewing it
 * at no more than half its own timeout. It lapses on timeout expiry,
 * explicit release, USB detach, disconnect of the leasing BLE profile,
 * and any endpoint switch away from the leasing endpoint - a dead or
 * absent host must never leave the wheel fallback dead.
 */

#pragma once

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>

#include <zmk/endpoints_types.h>

/* Host command length, for the lease and tap confirm alike: the 4-byte
 * SET-feature-report body (over USB the control payload may additionally
 * carry the leading report-ID byte). */
#define ZMK_RAW_TOUCH_CMD_LEN 4

/* body[0]: command. 0x01 = host lease; 0x02 = tap confirm (see
 * zmk/raw_touch/tap.h, dispatched by the same handler); everything else
 * is rejected. */
#define ZMK_RAW_TOUCH_LEASE_CMD_HOST_LEASE 0x01

/* body[1]: operation. */
#define ZMK_RAW_TOUCH_LEASE_OP_RELEASE 0x00
#define ZMK_RAW_TOUCH_LEASE_OP_ACQUIRE 0x01 /* acquire or renew */

/* body[2]: timeout in seconds. 0 is rejected; nonzero values are clamped
 * to [ZMK_RAW_TOUCH_LEASE_TIMEOUT_MIN_S, ZMK_RAW_TOUCH_LEASE_TIMEOUT_MAX_S].
 * Ignored on release. */
#define ZMK_RAW_TOUCH_LEASE_TIMEOUT_MIN_S 5
#define ZMK_RAW_TOUCH_LEASE_TIMEOUT_MAX_S 120

/* body[3]: reserved, must be 0. */

#if IS_ENABLED(CONFIG_ZMK_RAW_TOUCH_USB) || IS_ENABLED(CONFIG_ZMK_RAW_TOUCH_BLE)

/**
 * @brief Handle a command written by the host to the feature report.
 *
 * The lease command is handled here; a tap-confirm command is forwarded
 * to zmk_raw_touch_tap_confirm() when it comes from the endpoint that
 * holds the lease, and otherwise ignored (0), since a confirmation from a
 * host that is no longer scrolling refers to a touch it no longer owns.
 *
 * @param source The endpoint instance the write arrived on.
 * @param body The command body (without any report-ID prefix).
 * @param len Length of @p body; must be ZMK_RAW_TOUCH_CMD_LEN.
 *
 * @retval 0 on success.
 * @retval -EMSGSIZE on a wrong length.
 * @retval -ENOTSUP on an unknown command byte.
 * @retval -EINVAL on a bad operation, timeout or reserved byte.
 */
int zmk_raw_touch_lease_handle_command(struct zmk_endpoint_instance source, const uint8_t *body,
                                      size_t len);

/**
 * @brief Whether a lease is held for the currently-selected endpoint.
 *
 * True iff the endpoint instance zmk_endpoints_selected() returns holds a
 * live lease. Cheap enough to call once per frame; this is what decides
 * both frame flags bit 2 and the wheel-fallback suppression, so the two
 * can never disagree.
 */
bool zmk_raw_touch_lease_held_for_selected(void);

/**
 * @brief Clear the lease of an endpoint whose host has gone away.
 *
 * For a transport to call when it loses the host behind @p endpoint, e.g.
 * on the disconnect of a BLE profile's connection. A no-op when no lease
 * is held there.
 *
 * @param endpoint The endpoint instance whose lease lapses.
 * @param reason Short description for the log.
 */
void zmk_raw_touch_lease_clear(struct zmk_endpoint_instance endpoint, const char *reason);

#else /* no host-facing transport: split peripheral, or both disabled */

static inline int zmk_raw_touch_lease_handle_command(struct zmk_endpoint_instance source,
                                                    const uint8_t *body, size_t len) {
    ARG_UNUSED(source);
    ARG_UNUSED(body);
    ARG_UNUSED(len);
    return -ENOTSUP;
}

static inline bool zmk_raw_touch_lease_held_for_selected(void) { return false; }

#endif
