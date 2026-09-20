/*
 * Copyright (c) 2026 Mrinal Kalakrishnan
 *
 * SPDX-License-Identifier: MIT
 *
 * Peripheral-side sample-time stamp for a relayed pad. See
 * zmk/raw_touch/split_stamp.h for the why.
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

struct rtss_config {
    /* The relay's `reg`: the central dispatches relayed events to its
     * matching `zmk,input-split` node by this address, so the stamp must
     * carry the same one as the frame it belongs to. */
    uint8_t reg;
};

static int rtss_handle_event(const struct device *dev, struct input_event *event, uint32_t param1,
                             uint32_t param2, struct zmk_input_processor_state *state) {
    ARG_UNUSED(param1);
    ARG_UNUSED(param2);
    ARG_UNUSED(state);

    /* Only an absolute frame's sync gets a stamp. A sync on anything else
     * (there is none from an absolute-mode pad, but a chain is user
     * wiring) would leave a stamp pending on the central with no frame to
     * bind it to. */
    if (!event->sync || event->type != INPUT_EV_ABS) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    const struct rtss_config *cfg = dev->config;

    struct zmk_split_transport_peripheral_event ev = {
        .type = ZMK_SPLIT_TRANSPORT_PERIPHERAL_EVENT_TYPE_INPUT_EVENT,
        .data = {.input_event = {
                     .reg = cfg->reg,
                     .type = ZMK_RAW_TOUCH_SPLIT_STAMP_TYPE,
                     .code = ZMK_RAW_TOUCH_SPLIT_STAMP_CODE,
                     .value = (int32_t)zmk_raw_touch_split_stamp_now(),
                     .sync = 0,
                 }}};

    /* Not fatal: the central stamps the frame with its own clock when no
     * stamp arrives (the transport already warns when its queue is full). */
    int ret = zmk_split_peripheral_report_event(&ev);
    if (ret < 0) {
        LOG_DBG("Stamp not sent: %d", ret);
    }

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
    static const struct rtss_config rtss_config_##n = {                                            \
        .reg = DT_REG_ADDR(DT_INST_PHANDLE(n, input_split)),                                       \
    };                                                                                             \
    DEVICE_DT_INST_DEFINE(n, &rtss_init, NULL, NULL, &rtss_config_##n, POST_KERNEL,                \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &rtss_driver_api);

DT_INST_FOREACH_STATUS_OKAY(RTSS_INST)
