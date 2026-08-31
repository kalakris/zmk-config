/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * Raw touch streaming (protocol v3): forwards absolute touch frames
 * (position + touch strength) from a trackpad running in absolute mode to
 * the host over a vendor-defined HID report (see zmk/raw_touch/hid.h).
 *
 * Reports are emitted only while the selected endpoint's host holds a
 * live claim (see gate.c) - i.e. only in host-driven ("RawTouch") mode,
 * never in standard mode: one report per pad sample while
 * touched (~100 Hz), plus exactly one release report (touched = 0, z = 0)
 * on lift-off. Unclaimed, the stream is silent - nobody is listening, and
 * at ~100 Hz of 11-byte reports the wasted BLE airtime is real. If the
 * claim clears mid-touch, one final synthetic release report (touched =
 * 0, flags bit 2 clear) closes the host's gesture before the stream goes
 * quiet. Each frame carries a per-pad sequence number (drop detection) and a device-side
 * timestamp in 100 us units (host-side velocity that BLE batching cannot
 * distort). A 20-byte feature report on the same report ID describes the
 * protocol version, the pads present and, per pad, its resolution,
 * orientation, coordinate ranges and contact count; it is readable over
 * USB GET_REPORT and the BLE HOG feature-report characteristic.
 *
 * One instance per zmk,raw-touch-pad devicetree node. Nothing here is
 * specific to any particular touchpad ASIC: the node names the input
 * device and declares the geometry, orientation and tap parameters that
 * the pad's own driver binding would otherwise have to supply.
 *
 * Scroll mode: frames carry the scroll-mode flag while a processor chain
 * containing the zmk,input-processor-raw-touch-scroll marker is the one
 * handling the pad's events (see the commentary in zmk/raw_touch/scroll.h).
 *
 * Dual mode: relative deltas derived from successive absolute positions
 * are ALWAYS re-injected as REL_X/REL_Y on the same input device,
 * regardless of scroll mode, so the existing input listener chain
 * (scaling, wheel-mapping overlays, temp mouse layer, buttons) keeps
 * working as a fallback. A host consuming the raw stream is expected to
 * suppress the pointer/wheel events it supersedes.
 *
 * Tap-to-click: when the pad node sets tap-click, a touch that lifts off
 * within tap-max-ms and never strays more than tap-max-movement raw
 * counts (Chebyshev distance) from its touch-down point - and never had a
 * scroll-mode frame - injects an INPUT_BTN_0 press + release into the
 * pad's normal input pipeline, so existing button processors apply.
 */

#define DT_DRV_COMPAT zmk_raw_touch_pad

#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/init.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk_raw_touch, CONFIG_ZMK_RAW_TOUCH_LOG_LEVEL);

#include <zmk/raw_touch/gate.h>
#include <zmk/raw_touch/hid.h>
#include <zmk/raw_touch/scroll.h>
#include <zmk/raw_touch/transport.h>

struct raw_touch_pad_config {
    /* The input device producing the absolute touch events, and the
     * device the derived relative deltas and taps are injected back on. */
    const struct device *dev;

    uint8_t pad_id;
    uint16_t x_max;
    uint16_t y_max;
    uint8_t resolution;

    /* A pad driver's own invert/rotate feed transforms typically only
     * apply to its relative mode, so mirror them here when deriving
     * pointer deltas from the raw absolute coordinates. The streamed
     * frames stay untransformed; the same property values go out verbatim
     * in the feature report's orientation byte so hosts can apply them. */
    bool rotate_90;
    bool x_invert;
    bool y_invert;

    bool tap_click;
    uint16_t tap_max_ms;
    uint16_t tap_max_movement;
};

struct raw_touch_pad_data {
    /* Current frame, accumulated until the sync event. */
    uint16_t cur_x, cur_y;
    uint8_t cur_z;

    /* Scroll-context marks seen while accumulating the current frame. */
    bool frame_open;
    bool frame_scroll;

    /* Previous frame state for release dedup and delta derivation. */
    bool prev_touched;
    bool have_prev_pos;
    uint16_t prev_x, prev_y;

    /* Streamed-frame sequence number, +1 per emitted report, wraps. */
    uint8_t seq;

    /* Whether the last emitted report said touched, i.e. the stream's
     * consumer currently believes a finger is down. Distinct from
     * prev_touched (the pad's physical state, which keeps tracking while
     * no report goes out): a mid-touch declaim must emit exactly one
     * synthetic release report so the host is not left holding a phantom
     * finger-down, and this is the state that says one is owed. */
    bool stream_touched;

    /* Tap detection state for the current touch. */
    int64_t touch_down_ts;
    uint16_t touch_down_x, touch_down_y;
    bool tap_candidate;
    bool tap_scroll_seen;
};

static void raw_touch_emit_tap(const struct raw_touch_pad_config *cfg) {
    /* K_NO_WAIT: dropping a click beats deadlocking the input queue we
     * are dispatched from. Press and release are separate sync'd events,
     * so the listener sends a button-down report followed by a button-up
     * report through the pad's normal processor chain. */
    input_report_key(cfg->dev, INPUT_BTN_0, 1, true, K_NO_WAIT);
    input_report_key(cfg->dev, INPUT_BTN_0, 0, true, K_NO_WAIT);
}

static void raw_touch_process_frame(const struct raw_touch_pad_config *cfg,
                                    struct raw_touch_pad_data *data, bool scroll_mode) {
    /* An absolute-mode pad marks lift-off with an all-zeros idle frame. */
    bool touched = !(data->cur_x == 0 && data->cur_y == 0 && data->cur_z == 0);

    if (!touched && !data->prev_touched) {
        /* Nothing to stream while idle; also swallows any extra idle
         * frames so exactly one release report goes out. */
        return;
    }

    /* Host claim, evaluated fresh on every frame: true iff the endpoint
     * this frame is about to go to (zmk_raw_touch_send_report() dispatches
     * on zmk_endpoints_selected() a few lines below) holds a live claim.
     * Deliberately NOT latched across frames - frame emission, the flag
     * and the wheel suppression below must all revert on the very next
     * frame after a claim clears or the endpoint switches away. */
    bool host_claimed = zmk_raw_touch_gate_engaged_for_selected();

    /* Device-side sample time (HID Scan Time convention, 100 us units):
     * hosts derive finger velocity from this rather than from arrival
     * time, which BLE connection-interval batching distorts. */
    uint16_t timestamp = (uint16_t)(k_ticks_to_us_floor64(k_uptime_ticks()) / 100);

    if (host_claimed) {
        uint8_t flags = (touched ? ZMK_RAW_TOUCH_FLAGS_TOUCHED : 0) |
                        (scroll_mode ? ZMK_RAW_TOUCH_FLAGS_SCROLL_MODE : 0) |
                        ZMK_RAW_TOUCH_FLAGS_HOST_CLAIMED;

        /* !touched implies cur_x/y/z are all zero (that is how touched is
         * derived above), so the release report's zeros need no special
         * case. */
        zmk_raw_touch_hid_set(cfg->pad_id, data->cur_x, data->cur_y, data->cur_z, flags,
                              data->seq++, timestamp);
        zmk_raw_touch_send_report();
        data->stream_touched = touched;
    } else if (data->stream_touched) {
        /* The claim cleared mid-touch (timeout, host release, endpoint
         * switch - see gate.c) and the last report the host saw said
         * touched. Emit exactly one synthetic release report so the host
         * closes its gesture instead of holding a phantom finger-down
         * (runaway momentum), then go silent. Bit 2 is clear - its meaning
         * stays exact: "a host claim was engaged when this frame was
         * sampled" - which also tells the host the wheel fallback is live
         * again, so it must not add lift-off momentum on top. */
        zmk_raw_touch_hid_set(cfg->pad_id, 0, 0, 0,
                              scroll_mode ? ZMK_RAW_TOUCH_FLAGS_SCROLL_MODE : 0, data->seq++,
                              timestamp);
        zmk_raw_touch_send_report();
        data->stream_touched = false;
    }
    /* Unclaimed with nothing owed: emit nothing. Everything below - tap
     * detection, relative-delta derivation, prev_x/prev_y tracking - keeps
     * running regardless, because it IS the driverless standard-mode
     * experience (cursor + tap + wheel fallback). */

    if (touched && !data->prev_touched) {
        /* Touch-down: start a tap candidacy. */
        data->touch_down_ts = k_uptime_get();
        data->touch_down_x = data->cur_x;
        data->touch_down_y = data->cur_y;
        data->tap_candidate = cfg->tap_click;
        data->tap_scroll_seen = false;
    }

    data->tap_scroll_seen = data->tap_scroll_seen || scroll_mode;

    if (touched && data->tap_candidate) {
        int travel_x = (int)data->cur_x - (int)data->touch_down_x;
        int travel_y = (int)data->cur_y - (int)data->touch_down_y;
        if (MAX(abs(travel_x), abs(travel_y)) > cfg->tap_max_movement) {
            data->tap_candidate = false;
        }
    }

    /* Dual mode: derive relative deltas for the normal pointer pipeline,
     * even in scroll mode - an existing wheel-mapping overlay then
     * provides standard wheel scrolling as a fallback for hosts without a
     * stream consumer.
     *
     * The host claim suppresses exactly that fallback: while the selected
     * endpoint holds a claim, scroll-context deltas are not injected at
     * all, so the wheel-mapping overlay downstream has nothing to emit
     * and the claiming host (which is synthesizing scroll from the
     * frames) never sees doubled scrolling. This is the narrowest
     * possible cut - pointer-context deltas (scroll_mode false), tap
     * clicks and every key path are untouched, and prev_x/prev_y keep
     * tracking below so the first delta after the claim clears is an
     * ordinary one-frame step, not a jump. Hosts that never claim are
     * unaffected: the wheel keeps working. */
    bool suppress_fallback = host_claimed && scroll_mode;

    if (touched && data->have_prev_pos && !suppress_fallback) {
        int32_t raw_dx = (int32_t)data->cur_x - (int32_t)data->prev_x;
        int32_t raw_dy = (int32_t)data->cur_y - (int32_t)data->prev_y;

        int32_t dx = cfg->rotate_90 ? raw_dy : raw_dx;
        int32_t dy = cfg->rotate_90 ? raw_dx : raw_dy;
        if (cfg->x_invert) {
            dx = -dx;
        }
        if (cfg->y_invert) {
            dy = -dy;
        }

        if (dx != 0 || dy != 0) {
            /* K_NO_WAIT: dropping a delta beats deadlocking the input
             * queue we are dispatched from. */
            input_report_rel(cfg->dev, INPUT_REL_X, dx, false, K_NO_WAIT);
            input_report_rel(cfg->dev, INPUT_REL_Y, dy, true, K_NO_WAIT);
        }
    }

    if (!touched && data->tap_candidate && !data->tap_scroll_seen &&
        (k_uptime_get() - data->touch_down_ts) <= cfg->tap_max_ms) {
        raw_touch_emit_tap(cfg);
    }

    if (touched) {
        data->prev_x = data->cur_x;
        data->prev_y = data->cur_y;
        data->have_prev_pos = true;
    } else {
        data->have_prev_pos = false;
        data->tap_candidate = false;
    }
    data->prev_touched = touched;
}

static void raw_touch_input_event(const struct raw_touch_pad_config *cfg,
                                  struct raw_touch_pad_data *data, struct input_event *evt) {
    if (evt->type != INPUT_EV_ABS) {
        /* Ignore everything else, including our own injected REL/KEY events. */
        return;
    }

    switch (evt->code) {
    case INPUT_ABS_X:
        data->cur_x = (uint16_t)evt->value;
        break;
    case INPUT_ABS_Y:
        data->cur_y = (uint16_t)evt->value;
        break;
    case INPUT_ABS_Z:
        data->cur_z = (uint8_t)evt->value;
        break;
    default:
        return;
    }

    /* Fold the scroll-context latch into the frame being accumulated.
     *
     * A frame is three input events (ABS_X, ABS_Y, then ABS_Z carrying the
     * sync) and the listener's processor chain runs on all of them, while
     * we act only on the sync. So whichever order the input subsystem
     * happens to invoke the listener's callback and ours in, the marker
     * has already been reached at least once for this frame by the time we
     * process the sync - the flag is correct from the very first frame of
     * a touch, including when the scroll layer was held before touch-down,
     * and it follows layer changes mid-touch.
     *
     * That is also strictly more faithful than reimplementing the
     * listener's chain walk would be: the marker runs if and only if the
     * chain containing it is the one actually handling the event, so layer
     * overlay ordering and process-next shadowing are honoured by
     * construction.
     *
     * The latch is discarded rather than accumulated on the first event of
     * a frame. A mark can be set *after* we handled the previous sync (if
     * the listener's callback runs after ours, or from the relative deltas
     * we injected), and carrying that forward would flag the first frame
     * of the next touch with the scroll state of the previous one. Since
     * the discard happens before the second event of the frame is even
     * dispatched, no mark belonging to this frame can be lost with it.
     */
    bool marked = zmk_raw_touch_scroll_take(cfg->dev);

    if (data->frame_open) {
        data->frame_scroll = data->frame_scroll || marked;
    } else {
        /* First event of a frame: `marked` belongs to the previous one. */
        data->frame_scroll = false;
        data->frame_open = true;
    }

    if (evt->sync) {
        data->frame_open = false;
        raw_touch_process_frame(cfg, data, data->frame_scroll);
    }
}

#define RT_ORIENTATION(cfg)                                                                        \
    (((cfg)->rotate_90 ? ZMK_RAW_TOUCH_ORIENT_ROTATE_90 : 0) |                                     \
     ((cfg)->x_invert ? ZMK_RAW_TOUCH_ORIENT_X_INVERT : 0) |                                       \
     ((cfg)->y_invert ? ZMK_RAW_TOUCH_ORIENT_Y_INVERT : 0))

#define RT_INST(n)                                                                                 \
    BUILD_ASSERT(DT_INST_PROP(n, pad_id) < 8, "raw touch pad-id must be less than 8");             \
    static struct raw_touch_pad_data rt_data_##n;                                                  \
    static const struct raw_touch_pad_config rt_config_##n = {                                     \
        .dev = DEVICE_DT_GET(DT_INST_PHANDLE(n, device)),                                          \
        .pad_id = DT_INST_PROP(n, pad_id),                                                         \
        .x_max = DT_INST_PROP(n, x_max),                                                           \
        .y_max = DT_INST_PROP(n, y_max),                                                           \
        .resolution = DT_INST_PROP(n, resolution),                                                 \
        .rotate_90 = DT_INST_PROP(n, rotate_90),                                                   \
        .x_invert = DT_INST_PROP(n, x_invert),                                                     \
        .y_invert = DT_INST_PROP(n, y_invert),                                                     \
        .tap_click = DT_INST_PROP(n, tap_click),                                                   \
        .tap_max_ms = DT_INST_PROP(n, tap_max_ms),                                                 \
        .tap_max_movement = DT_INST_PROP(n, tap_max_movement),                                     \
    };                                                                                             \
    static void rt_input_cb_##n(struct input_event *evt) {                                         \
        raw_touch_input_event(&rt_config_##n, &rt_data_##n, evt);                                  \
    }                                                                                              \
    INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(DT_INST_PHANDLE(n, device)), rt_input_cb_##n);

DT_INST_FOREACH_STATUS_OKAY(RT_INST)

#define RT_CONFIG_REF(n) &rt_config_##n,

static const struct raw_touch_pad_config *const raw_touch_pads[] = {
    DT_INST_FOREACH_STATUS_OKAY(RT_CONFIG_REF)};

static int raw_touch_init(void) {
    uint8_t pads_present = 0;

    for (size_t i = 0; i < ARRAY_SIZE(raw_touch_pads); i++) {
        pads_present |= BIT(raw_touch_pads[i]->pad_id);
    }

    zmk_raw_touch_hid_set_feature_header(pads_present);

    /* Fill the feature report's pad slots with the present pads in
     * ascending pad-id order, each with its own geometry and orientation.
     * pad-ids are unique (the binding requires it), so each id fills at
     * most one slot. */
    int slot = 0;

    for (uint8_t id = 0; id < 8 && slot < ZMK_RAW_TOUCH_FEATURE_PAD_SLOTS; id++) {
        for (size_t i = 0; i < ARRAY_SIZE(raw_touch_pads); i++) {
            const struct raw_touch_pad_config *cfg = raw_touch_pads[i];

            if (cfg->pad_id != id) {
                continue;
            }

            /* max_contacts = 1: the module streams a single contact per pad
             * (contact_id 0); a Pinnacle reports one finger anyway. */
            zmk_raw_touch_hid_set_feature_slot(slot++, cfg->resolution, RT_ORIENTATION(cfg),
                                               cfg->x_max, cfg->y_max, 1);
        }
    }

    if (ARRAY_SIZE(raw_touch_pads) > ZMK_RAW_TOUCH_FEATURE_PAD_SLOTS) {
        LOG_WRN("%d raw touch pads configured but the feature report has %d slots; "
                "pads with the highest pad-ids are not described",
                (int)ARRAY_SIZE(raw_touch_pads), ZMK_RAW_TOUCH_FEATURE_PAD_SLOTS);
    }

    return 0;
}

SYS_INIT(raw_touch_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
