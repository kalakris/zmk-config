/*
 * Copyright (c) 2026 Mrinal Kalakrishnan
 *
 * SPDX-License-Identifier: MIT
 *
 * Marker input processor for raw touch frames (see raw_touch.c).
 *
 * The processor itself is a pure pass-through: it never modifies or stops
 * events. Its only purpose is to mark a "scroll context": when an input
 * listener chain containing this processor handles an event from a
 * streaming pad, the pad's streamed frames carry the scroll-mode flag.
 *
 * See zmk/raw_touch/scroll.h for why marking from inside the chain is
 * both faithful to layer ordering and reliable from the first frame of a
 * touch. The latch itself lives in the pad's data (src/raw_touch.c); in a
 * build with no pad the marker is a harmless no-op.
 */

#define DT_DRV_COMPAT zmk_input_processor_raw_touch_scroll

#include <zephyr/kernel.h>
#include <zephyr/device.h>

#include <drivers/input_processor.h>

#include <zmk/raw_touch/scroll.h>

static int rts_handle_event(const struct device *dev, struct input_event *event, uint32_t param1,
                            uint32_t param2, struct zmk_input_processor_state *state) {
    ARG_UNUSED(dev);
    ARG_UNUSED(param1);
    ARG_UNUSED(param2);
    ARG_UNUSED(state);

    zmk_raw_touch_scroll_mark(event->dev);

    return ZMK_INPUT_PROC_CONTINUE;
}

static const struct zmk_input_processor_driver_api rts_driver_api = {
    .handle_event = rts_handle_event,
};

static int rts_init(const struct device *dev) {
    ARG_UNUSED(dev);

    return 0;
}

#define RTS_INST(n)                                                                                \
    DEVICE_DT_INST_DEFINE(n, &rts_init, NULL, NULL, NULL, POST_KERNEL,                             \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &rts_driver_api);

DT_INST_FOREACH_STATUS_OKAY(RTS_INST)
