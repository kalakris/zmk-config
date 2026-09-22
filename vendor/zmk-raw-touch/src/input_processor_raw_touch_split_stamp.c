/*
 * Copyright (c) 2026 Mrinal Kalakrishnan
 *
 * SPDX-License-Identifier: MIT
 *
 * Peripheral-side sample-time stamp for a relayed pad, plus this half's
 * module version. See zmk/raw_touch/split_stamp.h for the why of both.
 *
 * Built only on a split PERIPHERAL. Chain it on the peripheral's
 * `zmk,input-split` relay node: the relay runs its input-processors on
 * every event before forwarding it, so an event this processor reports
 * from inside that call reaches the wire ahead of the event being
 * forwarded. That ordering is what lets the central bind the stamp to the
 * right frame: it arrives between the frame's last non-sync event and its
 * sync.
 *
 * The processor never modifies or stops the events it sees.
 */

#define DT_DRV_COMPAT zmk_input_processor_raw_touch_split_stamp

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/logging/log.h>

#include <drivers/input_processor.h>
#include <zmk/split/peripheral.h>
#include <zmk/split/transport/types.h>

LOG_MODULE_DECLARE(zmk_raw_touch, CONFIG_ZMK_RAW_TOUCH_LOG_LEVEL);

#include <zmk/raw_touch/split_stamp.h>

/* A gap this long in a pad's frames means the next one opens a new touch.
 * Frames arrive ~10 ms apart while a finger is down and stop entirely
 * after the release frame, so any threshold well inside a second
 * distinguishes the two without having to interpret the frame's contents
 * (this processor only ever looks at the sync event). */
#define RTSS_TOUCH_GAP_MS 500

struct rtss_data {
    /* Uptime of the last frame stamped, and whether there has been one. */
    int64_t last_frame_ms;
    bool stamped_any;
};

static void rtss_report(uint8_t reg, uint16_t code, int32_t value) {
    struct zmk_split_transport_peripheral_event ev = {
        .type = ZMK_SPLIT_TRANSPORT_PERIPHERAL_EVENT_TYPE_INPUT_EVENT,
        .data = {.input_event = {
                     .reg = reg,
                     .type = ZMK_RAW_TOUCH_SPLIT_STAMP_TYPE,
                     .code = code,
                     .value = value,
                     .sync = 0,
                 }}};

    /* Not fatal for either message: the central stamps the frame with its
     * own clock when no stamp arrives, and keeps the version it already
     * had (the transport already warns when its queue is full). */
    int ret = zmk_split_peripheral_report_event(&ev);
    if (ret < 0) {
        LOG_DBG("Split message 0x%04x not sent: %d", (unsigned int)code, ret);
    }
}

/* param1 is the relay's `reg`: the central dispatches relayed events to its
 * matching `zmk,input-split` node by this address, so the stamp must carry
 * the same one as the frame it belongs to. A cell parameter rather than a
 * phandle property on the processor node, because relay -> processor ->
 * relay would be a devicetree dependency cycle (gen_defines.py rejects it). */
static int rtss_handle_event(const struct device *dev, struct input_event *event, uint32_t param1,
                             uint32_t param2, struct zmk_input_processor_state *state) {
    ARG_UNUSED(param2);
    ARG_UNUSED(state);

    /* Only an absolute frame's sync gets a stamp. A sync on anything else
     * (there is none from an absolute-mode pad, but a chain is user
     * wiring) would leave a stamp pending on the central with no frame to
     * bind it to. */
    if (!event->sync || event->type != INPUT_EV_ABS) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    struct rtss_data *data = (struct rtss_data *)dev->data;
    int64_t now = k_uptime_get();
    bool touch_start = !data->stamped_any || (now - data->last_frame_ms) >= RTSS_TOUCH_GAP_MS;

    data->last_frame_ms = now;
    data->stamped_any = true;

    if (touch_start) {
        /* Ahead of the stamp, so the central has the version before the
         * frame it belongs to; see zmk/raw_touch/split_stamp.h for why
         * this is per touch and not once at connect. */
        rtss_report((uint8_t)param1, ZMK_RAW_TOUCH_SPLIT_VERSION_CODE,
                    ZMK_RAW_TOUCH_MODULE_VERSION_PACKED);
    }

    rtss_report((uint8_t)param1, ZMK_RAW_TOUCH_SPLIT_STAMP_CODE,
                (int32_t)zmk_raw_touch_split_stamp_now());

    return ZMK_INPUT_PROC_CONTINUE;
}

static const struct zmk_input_processor_driver_api rtss_driver_api = {
    .handle_event = rtss_handle_event,
};

static int rtss_init(const struct device *dev) {
    ARG_UNUSED(dev);

    return 0;
}

#define RTSS_INST(n)                                                                               \
    static struct rtss_data rtss_data_##n;                                                         \
    DEVICE_DT_INST_DEFINE(n, &rtss_init, NULL, &rtss_data_##n, NULL, POST_KERNEL,                  \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &rtss_driver_api);

DT_INST_FOREACH_STATUS_OKAY(RTSS_INST)
