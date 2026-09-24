/*
 * Copyright (c) 2026 Mrinal Kalakrishnan
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zmk/raw_touch/hid.h>

/**
 * @brief Send a raw touch frame over the active ZMK endpoint.
 *
 * Dispatches on zmk_endpoints_selected() exactly as ZMK's own reports do,
 * so the raw frames follow the user's USB/BLE output selection and their
 * active BLE profile. Both transports copy @p body into their queue before
 * returning. Defined in src/raw_touch_endpoints.c.
 *
 * @retval 0 on success, or a negative errno.
 */
int zmk_raw_touch_send_report(const struct zmk_raw_touch_report_body *body);

/**
 * @brief Identify the endpoint zmk_raw_touch_send_report() sends to now.
 *
 * ZMK's zmk_endpoint_instance_to_index() of zmk_endpoints_selected(), so
 * equal values mean the same host (transport and BLE profile). -1 on a
 * build with no host-facing transport, which never sends. Defined in
 * src/raw_touch_endpoints.c.
 */
int zmk_raw_touch_selected_endpoint(void);

#if IS_ENABLED(CONFIG_ZMK_RAW_TOUCH_USB)
/**
 * @brief Queue a frame for the raw touch USB HID interface.
 *
 * Never blocks - it runs inline on the input dispatch path. The frame is
 * copied into a bounded ring; if the interrupt IN endpoint is idle it is
 * written straight away, and otherwise the completion of the transfer
 * already in flight takes it. A release frame is therefore not lost to a
 * busy endpoint - which two pads sharing the interface, or a peripheral
 * pad's split-link bursts, make an ordinary occurrence rather than a
 * failure. As on BLE, a full queue evicts the oldest MOTION frame and
 * never a release.
 *
 * While the bus is suspended the frame is queued and a remote wakeup is
 * requested; the queue drains once the host resumes the bus.
 *
 * A bus reset or detach flushes the queue: those frames belong to a bus
 * that is gone, and the host's silence watchdog is what closes that
 * gesture. Defined in src/raw_touch_usb_hid.c.
 *
 * @retval 0 if the frame was queued, or queued and written.
 * @retval -ENODEV if the interface is missing or the bus is not in the
 *         HID state.
 * @retval -ENOBUFS if the queue was full of undelivered release frames
 *         (a mis-sized CONFIG_ZMK_RAW_TOUCH_USB_QUEUE_SIZE).
 * @retval other negative errno from hid_int_ep_write().
 */
int zmk_raw_touch_usb_send_report(const struct zmk_raw_touch_report_body *body);
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
 * Each entry is bound to the BLE profile that was active when its frame
 * was sampled. A frame is delivered only to that profile: after a profile
 * switch or the disconnect of the bound profile, queued frames - a
 * retrying release included - are discarded rather than sent to whoever
 * is connected now, so the protocol's endpoint scoping holds for queued
 * frames too.
 *
 * Motion frames are still dropped under pressure: they are ~10 ms apart
 * and carry absolute positions, so a gap costs nothing but the `seq`
 * counter noting it. Defined in src/raw_touch_hog.c.
 *
 * @retval 0 if the frame was queued.
 * @retval -ENODEV if the input report characteristic was not found.
 * @retval -ENOTCONN if the active profile's host is not connected.
 * @retval -ENOBUFS if the queue was full of undelivered release frames
 *         (a mis-sized CONFIG_ZMK_RAW_TOUCH_BLE_QUEUE_SIZE).
 */
int zmk_raw_touch_hog_send_report(const struct zmk_raw_touch_report_body *body);
#endif
