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
 * touch.
 *
 * The latch storage lives here rather than in raw_touch.c so that the
 * marker keeps working (as a harmless no-op) in builds that enable the
 * processor without any pad.
 */

#define DT_DRV_COMPAT zmk_input_processor_raw_touch_scroll

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include <drivers/input_processor.h>

LOG_MODULE_DECLARE(zmk_raw_touch, CONFIG_ZMK_RAW_TOUCH_LOG_LEVEL);

#include <zmk/raw_touch/scroll.h>

struct rts_latch {
    const struct device *dev; /* NULL: unused slot */
    bool marked;
};

static struct rts_latch rts_latches[CONFIG_ZMK_INPUT_PROCESSOR_RAW_TOUCH_SCROLL_MAX_DEVICES];

/* A spinlock, not a mutex or a bare atomic:
 *
 * - Zephyr's input subsystem dispatches callbacks from its own thread with
 *   CONFIG_INPUT_MODE_THREAD, but from the caller's context - possibly an
 *   ISR - with CONFIG_INPUT_MODE_SYNCHRONOUS. A mutex may not be taken in
 *   an ISR; a spinlock may.
 * - Claiming a slot is a read-modify-write spanning two words (the device
 *   pointer and the flag) plus a scan of the table, so per-flag atomics
 *   would not make the insert safe on their own.
 *
 * The critical sections are a handful of pointer comparisons over a table
 * of CONFIG_ZMK_INPUT_PROCESSOR_RAW_TOUCH_SCROLL_MAX_DEVICES entries, with no logging or
 * other calls inside, so holding a spinlock across them is cheap.
 */
static struct k_spinlock rts_lock;

/* Call with rts_lock held. */
static struct rts_latch *rts_find(const struct device *dev, bool insert) {
    struct rts_latch *unused = NULL;

    for (size_t i = 0; i < ARRAY_SIZE(rts_latches); i++) {
        if (rts_latches[i].dev == dev) {
            return &rts_latches[i];
        }

        if (unused == NULL && rts_latches[i].dev == NULL) {
            unused = &rts_latches[i];
        }
    }

    if (insert && unused != NULL) {
        unused->dev = dev;
        return unused;
    }

    return NULL;
}

void zmk_raw_touch_scroll_mark(const struct device *dev) {
    if (dev == NULL) {
        /* NULL is the unused-slot sentinel, and the listener rejects
         * device-less events before reaching any processor anyway. */
        return;
    }

    /* Initialized here, not just in the block: K_SPINLOCK is a loop
     * construct, so the compiler cannot see that the body always runs. */
    bool full = false;

    K_SPINLOCK(&rts_lock) {
        struct rts_latch *latch = rts_find(dev, true);

        full = (latch == NULL);
        if (latch != NULL) {
            latch->marked = true;
        }
    }

    if (full) {
        /* Benign race on the flag: at worst the warning is logged twice. */
        static bool warned;

        if (!warned) {
            warned = true;
            LOG_WRN("More than %d input devices reached a raw touch scroll marker; raise "
                    "CONFIG_ZMK_INPUT_PROCESSOR_RAW_TOUCH_SCROLL_MAX_DEVICES",
                    CONFIG_ZMK_INPUT_PROCESSOR_RAW_TOUCH_SCROLL_MAX_DEVICES);
        }
    }
}

bool zmk_raw_touch_scroll_take(const struct device *dev) {
    bool marked = false;

    K_SPINLOCK(&rts_lock) {
        struct rts_latch *latch = rts_find(dev, false);

        if (latch != NULL) {
            marked = latch->marked;
            latch->marked = false;
        }
    }

    return marked;
}

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
