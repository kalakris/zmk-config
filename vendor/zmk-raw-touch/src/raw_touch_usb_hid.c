/*
 * Copyright (c) 2020 The ZMK Contributors
 * Copyright (c) 2026 Mrinal Kalakrishnan
 *
 * SPDX-License-Identifier: MIT
 *
 * Derived from ZMK's app/src/usb_hid.c (MIT).
 *
 * A second USB HID interface (HID_1) carrying only the raw touch report.
 *
 * ZMK core owns HID_0. We register HID_1 with our own report descriptor and
 * our own hid_ops, which is why the raw frames need no changes to ZMK's
 * descriptor or endpoint. Both interfaces sit behind the same USB device, so
 * the host sees one composite device exposing two HID collections.
 */

#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>

#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/class/usb_hid.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk_raw_touch, CONFIG_ZMK_RAW_TOUCH_LOG_LEVEL);

#include <zmk/event_manager.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/usb.h>

#include <zmk/raw_touch/gate.h>
#include <zmk/raw_touch/hid.h>
#include <zmk/raw_touch/transport.h>

#include "raw_touch_txq.h"

/* Zephyr instantiates exactly CONFIG_USB_HID_DEVICE_COUNT HID devices, named
 * HID_0 .. HID_(N-1). With the default of 1 there is no HID_1, and the only
 * symptom at runtime is device_get_binding() returning NULL: the keyboard
 * keeps working and the vendor interface silently never appears. Fail the
 * build instead. The module's Kconfig raises the default to 2, but an
 * application .conf that pins the symbol lower would otherwise win quietly. */
BUILD_ASSERT(CONFIG_USB_HID_DEVICE_COUNT >= 2,
             "CONFIG_ZMK_RAW_TOUCH_USB needs a second USB HID interface. "
             "Set CONFIG_USB_HID_DEVICE_COUNT=2 (or more) in your .conf.");

/* The full v3 input report (report ID + 11-byte body = 12 bytes) still fits
 * one interrupt IN packet at the default CONFIG_HID_INTERRUPT_EP_MPS of 16,
 * so a frame never straddles a USB (micro)frame boundary. Keep it that way:
 * a multi-packet report would also change the in_ready/hid_sem pacing
 * assumptions below. */
BUILD_ASSERT(sizeof(struct zmk_raw_touch_report) <= CONFIG_HID_INTERRUPT_EP_MPS,
             "Raw touch input report no longer fits a single interrupt IN packet");

/* HID_0 is ZMK's keyboard/consumer/mouse interface; ours is the next one. */
#define ZMK_RAW_TOUCH_USB_HID_DEV "HID_1"

static const struct device *hid_dev;

/* Exactly one in-flight interrupt IN transfer at a time. Taken by whoever
 * arms a transfer and given back only when there is nothing left to send:
 * see the transmit queue below. */
static K_SEM_DEFINE(hid_sem, 1, 1);

/* ---------------------------------------------------------------------
 * Transmit queue
 *
 * The interrupt IN endpoint carries one transfer at a time, and a frame
 * produced while the previous transfer is still in flight used to be
 * dropped on the spot. That is acceptable for motion frames and is not
 * acceptable for a RELEASE frame (ZMK_RAW_TOUCH_FLAGS_TOUCHED clear:
 * lift-off, or the synthetic frame from a mid-touch declaim), which the
 * producer never re-sends: losing one leaves the host holding a phantom
 * finger-down until its own ~150 ms silence watchdog fires, which costs
 * the gesture its lift-off momentum and can join a quick re-touch onto
 * it. It does not take a broken link to hit - two pads share this
 * interface, and a peripheral pad's frames arrive from the split link in
 * bursts.
 *
 * So frames queue instead, in the ring shared with the BLE transport
 * (raw_touch_txq.h) and under the same policy: a full queue evicts the
 * oldest MOTION frame, never a release, and drops the incoming frame only
 * when there is nothing but releases to evict. The queue is drained by
 * transfer completion - in_ready_cb arms the next transfer rather than
 * giving the semaphore back - so it empties at exactly the rate the host
 * polls the endpoint, and a release cannot be lost while the bus is
 * healthy.
 *
 * The pacing assumption the BUILD_ASSERT above rests on is unchanged:
 * still one transfer, of one packet, in flight at a time. The queue only
 * decides what the NEXT transfer carries; it never arms a second one.
 *
 * Entries carry RT_TXQ_NO_BINDING. Unlike a BLE profile, USB has one bus
 * and one host, and the events that invalidate queued frames (bus reset,
 * detach) invalidate all of them at once, so the queue is flushed whole
 * rather than per destination.
 * ------------------------------------------------------------------ */

RT_TXQ_DEFINE(usb_txq, CONFIG_ZMK_RAW_TOUCH_USB_QUEUE_SIZE);

/*
 * The DMA source for hid_int_ep_write(). Static, never a stack buffer: on
 * nRF, usb_dc_ep_write() hands the pointer to nrfx_usbd_ep_transfer() and the
 * DMA reads it *after* the call returns. A stack buffer produces a report
 * with a plausible header and whatever the stack holds by then -- typically
 * RAM pointers -- in the tail.
 *
 * Reuse of this buffer is gated by hid_sem, given back only from
 * in_ready_cb once the queue is empty (transfer complete, nothing more to
 * send), on a failed write, or -- via usb_conn_state_listener below --
 * once the bus leaves the HID state and the controller has abandoned the
 * transfer, so at most one in-flight transfer ever reads it.
 */
static struct zmk_raw_touch_report tx_report;

/* Arm a transfer with the frame at the head of the queue, or give the
 * semaphore back if there is nothing left to send.
 *
 * The caller must hold hid_sem, and that is precisely what makes writing
 * tx_report safe here: holding it means no transfer that could still be
 * reading the buffer is outstanding. */
static int usb_txq_send_next(void) {
    struct rt_txq_entry entry;

    if (!rt_txq_pop(&usb_txq, &entry)) {
        k_sem_give(&hid_sem);
        return 0;
    }

    tx_report.report_id = ZMK_RAW_TOUCH_REPORT_ID;
    tx_report.body = entry.body;

    int err = hid_int_ep_write(hid_dev, (const uint8_t *)&tx_report, sizeof(tx_report), NULL);
    if (err) {
        /* Nothing was armed, so no completion will come to drain the rest
         * of the queue: hand the semaphore back and let the next frame (or
         * the bus-state listener) restart it. */
        k_sem_give(&hid_sem);
    }

    return err;
}

/* Transfer complete: the endpoint is free and nothing is reading
 * tx_report any more, so the next queued frame goes out immediately with
 * the semaphore held across the handover.
 *
 * The legacy USB device stack calls this from its own callback context.
 * On nRF -- the tested platform -- that is the driver's work-queue thread,
 * never an ISR, and nothing on this path blocks, so arming the next
 * transfer from here is legal. A controller whose class callbacks are
 * dispatched from an ISR would need this deferred to a work item instead:
 * the ring is ISR-safe (spinlock), but hid_int_ep_write() is not
 * documented to be. */
static void in_ready_cb(const struct device *dev) {
    ARG_UNUSED(dev);

    int err = usb_txq_send_next();

    if (err) {
        LOG_DBG("Failed to arm the next queued raw touch frame: %d", err);
    }
}

/* Bus-state recovery for hid_sem and the queue.
 *
 * An interrupt IN transfer that is in flight when the cable is pulled (or
 * the bus resets) is simply abandoned by the controller: in_ready_cb never
 * fires, so the semaphore taken for it is never returned. The bus-down
 * branch in zmk_raw_touch_usb_send_report() cannot heal this on its own,
 * because it only runs when something sends while the bus is down -- and
 * the moment USB detaches, ZMK switches the selected endpoint to BLE, so
 * the USB send path goes quiet until after replug, by which point
 * zmk_usb_get_status() is healthy again and that branch is unreachable.
 * Net effect without this listener: one frame in flight at unplug time
 * wedges the vendor interface permanently (feature GETs and keys fine,
 * zero input frames) until the keyboard is power-cycled.
 *
 * So re-arm the semaphore whenever the USB connection state leaves
 * ZMK_USB_CONN_HID (detach, bus reset, error -- every transition that
 * kills an in-flight transfer; the same condition raw_touch_gate.c uses to drop
 * the USB claim). Safe against a genuinely in-flight transfer by
 * construction: leaving the HID state means the controller has abandoned
 * any armed IN transfer, so nothing reads tx_report any more, and a late
 * in_ready_cb -- were a driver ever to deliver one for an aborted
 * transfer -- finds an empty queue and just saturates the semaphore at
 * its limit of 1.
 *
 * The queue is flushed in the same breath, and before the semaphore is
 * given: frames addressed to a bus that has gone away are garbage, and
 * re-arming first would let a late in_ready_cb push one of them at
 * whatever comes back. This is the one case where a queued release IS
 * lost -- deliberately, since there is no longer a host to receive it,
 * and the host's silence watchdog is what closes that gesture.
 *
 * Deliberately NOT added: a time-based force-reclaim in the send path
 * ("sem held > 100 ms, take it anyway"). A pending interrupt IN transfer
 * has no deadline in the legacy stack: with the interface configured but
 * the host not polling the endpoint (no open handle, host-side
 * scheduling), the transfer stays armed indefinitely with the endpoint
 * still set up to DMA from tx_report, and a timed reclaim would overwrite
 * that buffer while the host can still collect it -- corrupting a frame
 * on the wire. The checked non-blocking take in the send path below
 * leaves the frame in the queue instead of overwriting a buffer an
 * in-flight transfer may still be reading. A stalled-but-configured host
 * needs no reclaim anyway: the moment it polls again, the pending
 * transfer completes and in_ready_cb drains what accumulated. */
static int usb_conn_state_listener(const zmk_event_t *eh) {
    const struct zmk_usb_conn_state_changed *ev = as_zmk_usb_conn_state_changed(eh);

    if (ev != NULL && ev->conn_state != ZMK_USB_CONN_HID) {
        rt_txq_flush(&usb_txq);
        k_sem_give(&hid_sem);
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(zmk_raw_touch_usb_hid, usb_conn_state_listener);
ZMK_SUBSCRIPTION(zmk_raw_touch_usb_hid, zmk_usb_conn_state_changed);

#define HID_GET_REPORT_TYPE_MASK 0xff00
#define HID_GET_REPORT_ID_MASK 0x00ff

#define HID_REPORT_TYPE_INPUT 0x100
#define HID_REPORT_TYPE_FEATURE 0x300

static int get_report_cb(const struct device *dev, struct usb_setup_packet *setup, int32_t *len,
                         uint8_t **data) {
    switch (setup->wValue & HID_GET_REPORT_TYPE_MASK) {
    case HID_REPORT_TYPE_FEATURE:
        switch (setup->wValue & HID_GET_REPORT_ID_MASK) {
        case ZMK_RAW_TOUCH_REPORT_ID: {
            /* Over the control pipe the report ID is part of the payload, so
             * hand back the whole struct rather than just the body. */
            struct zmk_raw_touch_feature_report *report = zmk_raw_touch_hid_get_feature_report();
            *data = (uint8_t *)report;
            *len = sizeof(*report);
            break;
        }
        default:
            LOG_DBG("Unsupported feature report ID %d requested",
                    setup->wValue & HID_GET_REPORT_ID_MASK);
            return -ENOTSUP;
        }
        break;
    case HID_REPORT_TYPE_INPUT:
        switch (setup->wValue & HID_GET_REPORT_ID_MASK) {
        case ZMK_RAW_TOUCH_REPORT_ID: {
            struct zmk_raw_touch_report *report = zmk_raw_touch_hid_get_report();
            *data = (uint8_t *)report;
            *len = sizeof(*report);
            break;
        }
        default:
            LOG_DBG("Unsupported input report ID %d requested",
                    setup->wValue & HID_GET_REPORT_ID_MASK);
            return -ENOTSUP;
        }
        break;
    default:
        /* 7.2.1 of the HID v1.11 spec is unclear about requests for reports
         * that do not exist; return -ENOTSUP like the Zephyr subsys does. */
        LOG_DBG("Unsupported report type 0x%x requested",
                setup->wValue & HID_GET_REPORT_TYPE_MASK);
        return -ENOTSUP;
    }

    return 0;
}

/* SET_REPORT(FEATURE) carries the protocol's single host-to-device path:
 * the host claim (see src/raw_touch_gate.c). Everything else is still rejected
 * with -ENOTSUP, exactly as Zephyr's default handler would.
 *
 * Per HID 1.11 a control-pipe report on a device using report IDs is
 * ID-prefixed, matching what get_report_cb returns; but host stacks are
 * not uniform about the prefix on Set_Report, so a bare 4-byte body is
 * accepted too. The two forms cannot collide: the command byte 0x01 is
 * fixed and distinct from the report ID 0x04.
 *
 * CONFIG_ENABLE_HID_INT_OUT_EP stays untouched -- it is a global symbol
 * that would add an interrupt OUT endpoint to ZMK's keyboard interface as
 * well, and the control pipe is plenty for a ~0.03 Hz claim refresh. */
static int set_report_cb(const struct device *dev, struct usb_setup_packet *setup, int32_t *len,
                         uint8_t **data) {
    if ((setup->wValue & HID_GET_REPORT_TYPE_MASK) != HID_REPORT_TYPE_FEATURE) {
        LOG_DBG("Unsupported report type 0x%x written", setup->wValue & HID_GET_REPORT_TYPE_MASK);
        return -ENOTSUP;
    }

    if ((setup->wValue & HID_GET_REPORT_ID_MASK) != ZMK_RAW_TOUCH_REPORT_ID) {
        LOG_DBG("Unsupported feature report ID %d written", setup->wValue & HID_GET_REPORT_ID_MASK);
        return -ENOTSUP;
    }

    const uint8_t *body = *data;
    size_t body_len = *len;

    if (body_len == ZMK_RAW_TOUCH_GATE_CMD_LEN + 1 && body[0] == ZMK_RAW_TOUCH_REPORT_ID) {
        body++;
        body_len--;
    }

    struct zmk_endpoint_instance source = {.transport = ZMK_TRANSPORT_USB};

    return zmk_raw_touch_gate_handle_command(source, body, body_len);
}

static const struct hid_ops ops = {
    .int_in_ready = in_ready_cb,
    .get_report = get_report_cb,
    .set_report = set_report_cb,
};

int zmk_raw_touch_usb_send_report(void) {
    if (hid_dev == NULL) {
        return -ENODEV;
    }

    switch (zmk_usb_get_status()) {
    case USB_DC_SUSPEND:
        return usb_wakeup_request();
    case USB_DC_ERROR:
    case USB_DC_RESET:
    case USB_DC_DISCONNECTED:
    case USB_DC_UNKNOWN:
        /* Defense in depth: the authoritative flush and re-arm live in
         * usb_conn_state_listener(), but the event behind it is raised from
         * a work item, so a send racing the status change can get here
         * first. Doing both again is harmless -- k_sem_give saturates at
         * the limit of 1, and an already-empty queue flushes to itself --
         * and with the bus in one of these states no transfer is running,
         * so nothing can be reading tx_report. */
        rt_txq_flush(&usb_txq);
        k_sem_give(&hid_sem);
        return -ENODEV;
    default:
        break;
    }

    /* Queue first, then try to take the endpoint - never the other way
     * round. Taking first and enqueueing only on failure loses the frame
     * whenever the in-flight transfer completes in between: in_ready_cb
     * would find the queue still empty, give the semaphore back, and the
     * frame would sit there with nothing left to drain it. For a release
     * frame there is no next frame to nudge it out, which is exactly the
     * loss this queue exists to prevent. */
    enum rt_txq_put_result res =
        rt_txq_put(&usb_txq, &zmk_raw_touch_hid_get_report()->body, RT_TXQ_NO_BINDING);

    switch (res) {
    case RT_TXQ_PUT_EVICTED:
        LOG_DBG("Raw touch USB queue full; evicted the oldest motion frame");
        break;
    case RT_TXQ_PUT_FULL:
        LOG_WRN("Raw touch USB queue (%d) full of undelivered release frames; dropped an "
                "incoming frame. Raise CONFIG_ZMK_RAW_TOUCH_USB_QUEUE_SIZE.",
                CONFIG_ZMK_RAW_TOUCH_USB_QUEUE_SIZE);
        return -ENOBUFS;
    case RT_TXQ_PUT_OK:
        break;
    }

    /* The take is checked: a busy semaphore means the previous transfer is
     * still reading tx_report, so overwriting it would corrupt a frame that
     * is already on the wire. The frame just queued goes out from
     * in_ready_cb when that transfer completes. K_NO_WAIT, not a timeout:
     * this runs inline on the input dispatch path, so waiting here
     * head-of-line blocks pointer deltas and taps whenever USB degrades in
     * a way the status switch above does not catch. */
    if (k_sem_take(&hid_sem, K_NO_WAIT) != 0) {
        return 0;
    }

    /* Endpoint free: send the head, which is this frame unless in_ready_cb
     * had already emptied the queue past it. Always from the head, so
     * frames stay in the order they were sampled. */
    return usb_txq_send_next();
}

static int raw_touch_usb_hid_init(void) {
    hid_dev = device_get_binding(ZMK_RAW_TOUCH_USB_HID_DEV);
    if (hid_dev == NULL) {
        LOG_ERR("Unable to locate %s; raw touch frames will not be sent over USB. "
                "Is CONFIG_USB_HID_DEVICE_COUNT >= 2?",
                ZMK_RAW_TOUCH_USB_HID_DEV);
        return -EINVAL;
    }

    usb_hid_register_device(hid_dev, zmk_raw_touch_report_desc, zmk_raw_touch_report_desc_size,
                            &ops);

    /* No usb_hid_set_proto_code(): this interface has no boot protocol, so
     * bInterfaceProtocol stays 0 (None) and the host's boot/report protocol
     * selection on ZMK's keyboard interface does not affect it. */
    usb_hid_init(hid_dev);

    return 0;
}

SYS_INIT(raw_touch_usb_hid_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
