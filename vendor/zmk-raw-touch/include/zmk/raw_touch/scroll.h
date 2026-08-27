/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * Scroll-context detection.
 *
 * ZMK core's input listener is not extensible from a module, so scroll
 * context is detected by the marker input processor itself: when the
 * marker is reached by an event from a pad's input device, it latches a
 * flag for that device. The pad's frame handler reads and clears the flag
 * once per frame.
 *
 * This is reliable from the first frame of a touch because a frame is
 * three input events (ABS_X, ABS_Y, then ABS_Z carrying the sync) and the
 * listener's processor chain runs on all of them, whereas the pad's frame
 * handler acts only on the sync. So the marker has already been reached at
 * least once this frame whichever order the two input callbacks run in.
 *
 * It is also strictly more faithful than reimplementing the listener's
 * chain walk: the marker runs if and only if the chain containing it is
 * the one actually handling the event, so layer-overlay ordering and
 * process-next shadowing are honoured by construction.
 */

#pragma once

#include <stdbool.h>
#include <zephyr/device.h>

#if IS_ENABLED(CONFIG_ZMK_INPUT_PROCESSOR_RAW_TOUCH_SCROLL)

/** Latch that a marker processor was reached by an event from @p dev. */
void zmk_raw_touch_scroll_mark(const struct device *dev);

/** Read and clear the latch for @p dev. */
bool zmk_raw_touch_scroll_take(const struct device *dev);

#else

static inline void zmk_raw_touch_scroll_mark(const struct device *dev) { ARG_UNUSED(dev); }
static inline bool zmk_raw_touch_scroll_take(const struct device *dev) {
    ARG_UNUSED(dev);
    return false;
}

#endif
