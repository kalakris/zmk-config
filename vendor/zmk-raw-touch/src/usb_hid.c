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

/* No .set_report: the raw touch protocol has no host-to-device path, and
 * Zephyr's default handler already answers SET_REPORT with -ENOTSUP.
 * CONFIG_ENABLE_HID_INT_OUT_EP is deliberately left alone too -- it is a
 * global symbol that would add an interrupt OUT endpoint to ZMK's keyboard
 * interface as well. */
static const struct hid_ops ops = {
    .int_in_ready = in_ready_cb,
    .get_report = get_report_cb,
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

    /* Unlike ZMK core we check the take: on timeout the previous transfer is
     * still reading tx_report, so overwriting it would corrupt a frame that
     * is already on the wire. Touch frames are ephemeral and arrive at
     * ~100 Hz, so dropping this one is strictly better than that. */
    if (k_sem_take(&hid_sem, K_MSEC(30)) != 0) {
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
