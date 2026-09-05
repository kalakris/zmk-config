/*
 * Copyright (c) 2026 Mrinal Kalakrishnan
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zmk/raw_touch/hid.h>

/**
 * @brief Send the current raw touch report over the active ZMK endpoint.
 *
 * Dispatches on zmk_endpoints_selected() exactly as ZMK's own reports do,
 * so the raw frames follow the user's USB/BLE output selection and their
 * active BLE profile. Defined in src/raw_touch_endpoints.c.
 *
 * @retval 0 on success, or a negative errno.
 */
int zmk_raw_touch_send_report(void);

#if IS_ENABLED(CONFIG_ZMK_RAW_TOUCH_USB)
/* src/raw_touch_usb_hid.c */
int zmk_raw_touch_usb_send_report(void);
#endif

#if IS_ENABLED(CONFIG_ZMK_RAW_TOUCH_BLE)
/**
 * @brief Queue a frame for notification on the raw touch HIDS instance.
 *
 * Never blocks - it runs inline on the input dispatch path. The frame is
 * copied into a bounded ring buffer and a work queue notifies it.
 *
 * Release frames (ZMK_RAW_TOUCH_FLAGS_TOUCHED clear) are durable, because
 * a lost one leaves the host holding a phantom finger-down:
 *
 *  - a full queue evicts the oldest MOTION frame, never a release;
 *  - a release that fails to notify (typically -ENOMEM/-ENOBUFS: no TX
 *    buffer free this connection event) is retried a few times, one
 *    connection interval apart, instead of being dropped;
 *  - frames are drained strictly in order and a retry is head-of-line, so
 *    a release never overtakes earlier motion of its pad, and the next
 *    touch never overtakes the release that closed the previous one.
 *
 * Motion frames are still dropped under pressure: they are ~10 ms apart
 * and carry absolute positions, so a gap costs nothing but the `seq`
 * counter noting it. Defined in src/raw_touch_hog.c.
 *
 * @retval 0 if the frame was queued.
 * @retval -ENODEV if the input report characteristic was not found.
 * @retval -ENOBUFS if the queue was full of undelivered release frames
 *         (a mis-sized CONFIG_ZMK_RAW_TOUCH_BLE_QUEUE_SIZE).
 */
int zmk_raw_touch_hog_send_report(struct zmk_raw_touch_report_body *body);
#endif
