/*
 * Copyright (c) 2026 Mrinal Kalakrishnan
 *
 * SPDX-License-Identifier: MIT
 *
 * Idle-sync filter: drops sync events that accumulated no host-visible
 * change.
 *
 * An input listener sends a mouse HID report on every sync event, even one
 * that carries no motion, no scroll and no button transition. Such a
 * report is a no-op to the host - zero relative deltas and an unchanged
 * button bitmap - but it still costs a USB interrupt-in transfer or a BLE
 * notification. A pad streaming absolute frames at ~100 Hz produces one
 * such sync per frame, because absolute events contribute nothing to a
 * mouse report, and a downscaling wheel overlay (e.g. 1:8) adds more by
 * emitting REL_WHEEL with value 0 on most frames. On BLE these compete
 * with the raw touch frame notifications for connection events,
 * which is felt as stuttering scroll.
 *
 * ZMK core fixes this inside the listener, by skipping a sync whose
 * accumulated x/y/wheel values are all zero and which requested no button
 * transition. A module cannot patch the listener, so the same decision is
 * made one step earlier, from the events themselves: stopping a sync event
 * before it reaches the listener is exactly equivalent to the listener
 * declining to send that report.
 *
 * Place this processor LAST in the chain, after every scaler, mapper and
 * temp-layer processor, so it sees final values and does not starve them
 * of events.
 */

#define DT_DRV_COMPAT zmk_input_processor_raw_touch_idle_filter

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>

#include <drivers/input_processor.h>

struct rtif_data {
    /* Whether anything the host would notice has been accumulated since
     * the last sync: a nonzero relative value, or a button transition. */
    bool nonzero_since_sync;
};

static int rtif_handle_event(const struct device *dev, struct input_event *event, uint32_t param1,
                             uint32_t param2, struct zmk_input_processor_state *state) {
    ARG_UNUSED(param1);
    ARG_UNUSED(param2);
    ARG_UNUSED(state);

    struct rtif_data *data = (struct rtif_data *)dev->data;

    switch (event->type) {
    case INPUT_EV_KEY:
        /* A button transition always changes host-visible state, so it
         * always gets a report - and so does the rest of its sync group. */
        data->nonzero_since_sync = true;
        break;

    case INPUT_EV_REL:
        if (event->value != 0) {
            data->nonzero_since_sync = true;
        } else if (!event->sync) {
            /* Value-less and carrying no sync: it can neither contribute
             * to a report nor cause one to be sent. Nothing downstream can
             * be stranded by dropping it. */
            return ZMK_INPUT_PROC_STOP;
        }
        break;

    default:
        /* Absolute (and anything else) contributes nothing to a mouse
         * report, so it never sets the flag. Non-sync ones still pass
         * through: the listener tracks absolute pointing state from them,
         * and this processor is only meant to suppress reports. */
        break;
    }

    if (event->sync) {
        /* THE TRAP: never stop a sync-carrying event once a nonzero value
         * has been accumulated earlier in the same sync group. The
         * listener flushes its accumulators only on a sync it actually
         * receives, so stopping this one would strand that value and leak
         * it into the next report. Hence the check on the flag as it was
         * before this group is closed out, not on this event's own value.
         *
         * The reset is unconditional for the same reason it is safe: the
         * listener has just flushed (or had nothing to flush), so the next
         * group starts empty. Leaving the flag set on a value-carrying
         * sync would wedge it permanently on for a device whose frames are
         * all absolute - one tap-to-click button event would be enough. */
        bool had = data->nonzero_since_sync;

        data->nonzero_since_sync = false;

        return had ? ZMK_INPUT_PROC_CONTINUE : ZMK_INPUT_PROC_STOP;
    }

    return ZMK_INPUT_PROC_CONTINUE;
}

static const struct zmk_input_processor_driver_api rtif_driver_api = {
    .handle_event = rtif_handle_event,
};

static int rtif_init(const struct device *dev) {
    ARG_UNUSED(dev);

    return 0;
}

#define RTIF_INST(n)                                                                               \
    static struct rtif_data rtif_data_##n;                                                         \
    DEVICE_DT_INST_DEFINE(n, &rtif_init, NULL, &rtif_data_##n, NULL, POST_KERNEL,                  \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &rtif_driver_api);

DT_INST_FOREACH_STATUS_OKAY(RTIF_INST)
