/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * A second USB HID interface (HID_1) carrying only the raw touch report.
 *
 * ZMK core owns HID_0. We register HID_1 with our own report descriptor and
 * our own hid_ops, which is why the raw stream needs no changes to ZMK's
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

#include <zmk/usb.h>

#include <zmk/raw_touch/gate.h>
#include <zmk/raw_touch/hid.h>
#include <zmk/raw_touch/transport.h>

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

static K_SEM_DEFINE(hid_sem, 1, 1);

static void in_ready_cb(const struct device *dev) { k_sem_give(&hid_sem); }

#define HID_GET_REPORT_TYPE_MASK 0xff00
#define HID_GET_REPORT_ID_MASK 0x00ff

#define HID_REPORT_TYPE_INPUT 0x100
#define HID_REPORT_TYPE_OUTPUT 0x200
#define HID_REPORT_TYPE_FEATURE 0x300

/*
 * The DMA source for hid_int_ep_write(). Static, never a stack buffer: on
 * nRF, usb_dc_ep_write() hands the pointer to nrfx_usbd_ep_transfer() and the
 * DMA reads it *after* the call returns. A stack buffer produces a report
 * with a plausible header and whatever the stack holds by then -- typically
 * RAM pointers -- in the tail.
 *
 * Reuse of this buffer is gated by hid_sem, which is only given back from
 * in_ready_cb (transfer complete) or on a failed write, so at most one
 * in-flight transfer ever reads it.
 */
static struct zmk_raw_touch_report tx_report;

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
 * the mode-gate claim (see src/gate.c). Everything else is still rejected
 * with -ENOTSUP, exactly as Zephyr's default handler would.
 *
 * Per HID 1.11 a control-pipe report on a device using report IDs is
 * ID-prefixed, matching what get_report_cb returns; but host stacks are
 * not uniform about the prefix on Set_Report, so a bare 4-byte body is
 * accepted too. The two forms cannot collide: the command byte 0x01 is
 * fixed and distinct from the report ID (0x04 by default; hid.c
 * BUILD_ASSERTs the ID is nonzero, and a Kconfig override of 0x01 would
 * be rejected below anyway because a 5-byte payload is only parsed
 * ID-prefixed).
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
        /* Re-arm the semaphore while the bus is down. A transfer aborted by
         * cable pull or bus reset never fires int_in_ready, so the give below
         * is the only way hid_sem ever comes back -- without it, the first
         * frame in flight at unplug time wedges the stream permanently
         * (every later send times out and drops). Safe here and only here:
         * with the bus in one of these states no transfer is running, so
         * nothing can be reading tx_report. k_sem_give saturates at the
         * limit of 1, so repeated calls are harmless. */
        k_sem_give(&hid_sem);
        return -ENODEV;
    default:
        break;
    }

    /* Unlike ZMK core we check the take: a busy semaphore means the previous
     * transfer is still reading tx_report, so overwriting it would corrupt a
     * frame that is already on the wire. K_NO_WAIT, not a timeout: this runs
     * inline on the input dispatch path, so waiting here head-of-line blocks
     * pointer deltas and taps whenever USB degrades in a way the status
     * switch above does not catch. Touch frames are ephemeral and arrive at
     * ~100 Hz, so dropping this one - as the BLE path does - is strictly
     * better than either. */
    if (k_sem_take(&hid_sem, K_NO_WAIT) != 0) {
        LOG_DBG("Raw touch USB endpoint busy, dropping frame");
        return -EAGAIN;
    }

    tx_report = *zmk_raw_touch_hid_get_report();

    int err = hid_int_ep_write(hid_dev, (const uint8_t *)&tx_report, sizeof(tx_report), NULL);
    if (err) {
        k_sem_give(&hid_sem);
    }

    return err;
}

static int zmk_raw_touch_usb_hid_init(void) {
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
     * bInterfaceProtocol stays 0 (None). Unlike the ZMK-fork prototype -- in
     * which the raw report shared ZMK's keyboard interface and so had to be
     * suppressed whenever the host selected boot protocol -- the raw stream
     * here is unaffected by the keyboard interface's protocol. */
    usb_hid_init(hid_dev);

    return 0;
}

SYS_INIT(zmk_raw_touch_usb_hid_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
