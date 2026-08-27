/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * Endpoint dispatch for the raw touch report.
 *
 * Mirrors ZMK core's zmk_endpoints_send_*_report(): the raw stream follows
 * whichever transport the user has selected, and on BLE whichever profile is
 * active, exactly as the keyboard and mouse reports do.
 */

#include <errno.h>

#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk_raw_touch, CONFIG_ZMK_RAW_TOUCH_LOG_LEVEL);

#include <zmk/raw_touch/hid.h>
#include <zmk/raw_touch/transport.h>

#define ZMK_RAW_TOUCH_HAS_TRANSPORT                                                                \
    (IS_ENABLED(CONFIG_ZMK_RAW_TOUCH_USB) || IS_ENABLED(CONFIG_ZMK_RAW_TOUCH_BLE))

#if !ZMK_RAW_TOUCH_HAS_TRANSPORT

/* Both transports are central-only, so on a split peripheral this file is all
 * that is left of the send path -- and it must not reference ZMK's endpoints
 * API at all. ZMK compiles its own src/endpoints.c under
 * `(NOT CONFIG_ZMK_SPLIT) OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL`, so on a
 * peripheral zmk_endpoints_selected() does not exist and calling it would
 * fail to link.
 *
 * The module core still runs there on purpose: it re-injects the relative
 * deltas that ordinary pointing needs, and the split link forwards those to
 * the central. Only the raw stream itself is unavailable, so the frame
 * handler's send is a no-op. */
int zmk_raw_touch_send_report(void) { return -ENOTSUP; }

#else

#include <zmk/endpoints.h>
#include <zmk/endpoints_types.h>

int zmk_raw_touch_send_report(void) {
    /* Note the plural: zmk_endpoints_selected() is stock ZMK's spelling.
     * zmk_endpoint_selected() exists only in a downstream fork, and stock ZMK
     * has no ZMK_TRANSPORT_NONE, so the switch below is exhaustive. */
    struct zmk_endpoint_instance current_instance = zmk_endpoints_selected();

    switch (current_instance.transport) {
    case ZMK_TRANSPORT_USB: {
#if IS_ENABLED(CONFIG_ZMK_RAW_TOUCH_USB)
        int err = zmk_raw_touch_usb_send_report();
        if (err) {
            /* Deliberately not LOG_ERR: frames stream at ~100 Hz while a
             * finger is down, so a host that has gone away would otherwise
             * produce a log flood at the default log level. */
            LOG_DBG("Failed to send raw touch report over USB: %d", err);
        }
        return err;
#else
        LOG_DBG("Raw touch over USB is not enabled");
        return -ENOTSUP;
#endif /* IS_ENABLED(CONFIG_ZMK_RAW_TOUCH_USB) */
    }

    case ZMK_TRANSPORT_BLE: {
#if IS_ENABLED(CONFIG_ZMK_RAW_TOUCH_BLE)
        /* The report ID is carried by the report reference descriptor on
         * BLE, not by the payload, so only the body is sent. */
        struct zmk_raw_touch_report *report = zmk_raw_touch_hid_get_report();
        int err = zmk_raw_touch_hog_send_report(&report->body);
        if (err) {
            LOG_DBG("Failed to send raw touch report over HOG: %d", err);
        }
        return err;
#else
        LOG_DBG("Raw touch over BLE is not enabled");
        return -ENOTSUP;
#endif /* IS_ENABLED(CONFIG_ZMK_RAW_TOUCH_BLE) */
    }
    }

    LOG_ERR("Unhandled endpoint transport %d", current_instance.transport);
    return -ENOTSUP;
}

#endif /* ZMK_RAW_TOUCH_HAS_TRANSPORT */
