/*
 * Copyright (c) 2026 Mrinal Kalakrishnan
 *
 * SPDX-License-Identifier: MIT
 *
 * Raw touch frames (protocol v3): forwards absolute touch frames
 * (position + touch strength) from a trackpad running in absolute mode to
 * the host over a vendor-defined HID report (see zmk/raw_touch/hid.h).
 *
 * Reports are emitted only while the selected endpoint's host holds a
 * live claim (see raw_touch_gate.c) - i.e. only in RawTouch mode, never
 * in Standard mode: one report per pad sample while touched (~100 Hz),
 * plus exactly one release report (touched = 0, z = 0) on lift-off.
 * Unclaimed, nothing is emitted - nobody is listening, and
 * at ~100 Hz of 11-byte reports the wasted BLE airtime is real. If the
 * claim clears mid-touch, one final synthetic release report (touched =
 * 0, flags bit 2 clear) closes the host's gesture before the stream goes
 * quiet. Each frame carries a per-pad sequence number (drop detection)
 * and a device-side timestamp in 100 us units (host-side velocity that
 * BLE batching cannot
 * distort). A feature report on the same report ID describes the
 * protocol version, this build's module version, the pads present and,
 * per pad, its resolution, orientation, coordinate ranges, contact count
 * and the module version of the half that owns it; its body is
 * 4 + 8 * (pads compiled in) bytes - 20 on a two-pad build - and it is
 * readable over USB GET_REPORT and the BLE HOG feature-report
 * characteristic.
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
 * working in Standard mode. A live host claim suppresses only the
 * scroll-context deltas (see below).
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
#include <zmk/raw_touch/split_stamp.h>
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

    /* Whether `dev` is a zmk,input-split relay rather than a pad the
     * central can see directly, i.e. whether the half that owns this pad
     * is the peripheral. Read off the devicetree, so it cannot disagree
     * with the wiring. It decides only whose module version the pad's
     * feature slot reports. */
    bool relayed;
};

struct raw_touch_pad_data {
    /* Current frame, accumulated until the sync event. */
    uint16_t cur_x, cur_y;
    uint8_t cur_z;

    /* Scroll-context marks seen while accumulating the current frame. */
    bool frame_open;
    bool frame_scroll;

    /* Peripheral sample time for the frame that closes next, when the pad
     * is relayed from a split peripheral running the split-stamp processor
     * (see zmk/raw_touch/split_stamp.h). Latest wins; consumed per frame. */
    bool stamp_valid;
    uint16_t stamp;

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

    /* This pad's slot in the feature report, assigned by raw_touch_init(),
     * or -1 for a pad that got none (more pads than slots). Only a relayed
     * pad needs it, to republish its half's version below. */
    int8_t feature_slot;

    /* Packed module version last announced by the owning half of a relayed
     * pad; 0 until it announces. Mirrors what the slot already holds, so
     * the LOG_INF fires once per change rather than once per touch. */
    uint8_t peer_version;
};

static void raw_touch_emit_tap(const struct raw_touch_pad_config *cfg) {
    /* K_NO_WAIT: dropping a click beats deadlocking the input queue we
     * are dispatched from. Press and release are separate sync'd events,
     * so the listener sends a button-down report followed by a button-up
     * report through the pad's normal processor chain. */
    input_report_key(cfg->dev, INPUT_BTN_0, 1, true, K_NO_WAIT);
    input_report_key(cfg->dev, INPUT_BTN_0, 0, true, K_NO_WAIT);
}

/* Device-side sample time (HID Scan Time convention, 100 us units): hosts
 * derive finger velocity from this rather than from arrival time, which
 * BLE connection-interval batching distorts. Only computed when a report
 * actually goes out - the 64-bit divide is not free on a Cortex-M.
 *
 * This is the local pad's clock, read when the frame is processed. A pad
 * relayed from a split peripheral is processed here too, after the split
 * hop, so for it this reading would carry the link's delivery jitter; the
 * peripheral can send its own reading along with the frame instead (see
 * zmk/raw_touch/split_stamp.h), and raw_touch_process_frame() prefers
 * that when it arrived. */
static uint16_t raw_touch_timestamp(void) {
    return (uint16_t)(k_ticks_to_us_floor64(k_uptime_ticks()) / 100);
}

static void raw_touch_process_frame(const struct raw_touch_pad_config *cfg,
                                    struct raw_touch_pad_data *data, bool scroll_mode) {
    /* Take the peripheral stamp for this frame, if one arrived, before any
     * early return below: left pending, it would misdate a later frame. */
    bool have_stamp = data->stamp_valid;
    uint16_t stamp = data->stamp;
    data->stamp_valid = false;

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

    if (host_claimed) {
        uint8_t flags = (touched ? ZMK_RAW_TOUCH_FLAGS_TOUCHED : 0) |
                        (scroll_mode ? ZMK_RAW_TOUCH_FLAGS_SCROLL_MODE : 0) |
                        ZMK_RAW_TOUCH_FLAGS_HOST_CLAIMED;

        /* !touched implies cur_x/y/z are all zero (that is how touched is
         * derived above), so the release report's zeros need no special
         * case.
         *
         * The send result is deliberately not checked, here or below. Both
         * transports QUEUE the frame rather than handing it to hardware
         * synchronously, and both protect release frames on the way out -
         * a full queue evicts motion and never a release (see
         * zmk/raw_touch/transport.h). So there is nothing a per-frame
         * producer could usefully retry: the cases that still lose a frame
         * (the bus or the link going away) are exactly the cases where
         * re-sending it would go nowhere either, and the host's silence
         * watchdog covers them. */
        zmk_raw_touch_hid_set(cfg->pad_id, data->cur_x, data->cur_y, data->cur_z, flags,
                              data->seq++, have_stamp ? stamp : raw_touch_timestamp());
        zmk_raw_touch_send_report();
        data->stream_touched = touched;
    } else if (data->stream_touched) {
        /* The claim cleared mid-touch (timeout, host release, endpoint
         * switch - see raw_touch_gate.c) and the last report the host saw said
         * touched. Emit exactly one synthetic release report so the host
         * closes its gesture instead of holding a phantom finger-down
         * (runaway momentum), then go silent. Bit 2 is clear - its meaning
         * stays exact: "a host claim was engaged when this frame was
         * sampled" - which also tells the host the wheel fallback is live
         * again, so it must not add lift-off momentum on top. */
        zmk_raw_touch_hid_set(cfg->pad_id, 0, 0, 0,
                              scroll_mode ? ZMK_RAW_TOUCH_FLAGS_SCROLL_MODE : 0, data->seq++,
                              have_stamp ? stamp : raw_touch_timestamp());
        zmk_raw_touch_send_report();
        data->stream_touched = false;
    }
    /* Unclaimed with nothing owed: emit nothing. Everything below - tap
     * detection, relative-delta derivation, prev_x/prev_y tracking - keeps
     * running regardless, because it IS Standard mode (cursor + tap +
     * wheel fallback). */

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
     * provides standard wheel scrolling for hosts that never claim.
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

/* The module version of the half that owns a relayed pad, announced ahead
 * of each touch's first frame (see zmk/raw_touch/split_stamp.h) and
 * republished in that pad's feature report slot, so a host reading the
 * report sees both halves' builds. Ignored for a local pad, whose slot
 * already carries this half's version: nothing should be able to talk a
 * central out of its own number. */
static void raw_touch_set_peer_version(const struct raw_touch_pad_config *cfg,
                                       struct raw_touch_pad_data *data, uint8_t version) {
    if (!cfg->relayed || version == data->peer_version) {
        return;
    }

    data->peer_version = version;
    LOG_INF("Raw touch pad %d is relayed from a half running module version %d.%d", cfg->pad_id,
            version >> 4, version & 0x0F);

    if (data->feature_slot >= 0) {
        zmk_raw_touch_hid_set_feature_slot_version(data->feature_slot, version);
    }
}

static void raw_touch_input_event(const struct raw_touch_pad_config *cfg,
                                  struct raw_touch_pad_data *data, struct input_event *evt) {
    if (evt->type == ZMK_RAW_TOUCH_SPLIT_STAMP_TYPE) {
        switch (evt->code) {
        case ZMK_RAW_TOUCH_SPLIT_STAMP_CODE:
            /* The peripheral's sample time, sent just ahead of the frame's
             * sync (see zmk/raw_touch/split_stamp.h). Latest wins: if the
             * stamped frame's own sync were lost on the link, the next
             * frame's stamp supersedes this one instead of the stale one
             * misdating that frame. */
            data->stamp = (uint16_t)evt->value;
            data->stamp_valid = true;
            break;
        case ZMK_RAW_TOUCH_SPLIT_VERSION_CODE:
            raw_touch_set_peer_version(cfg, data, (uint8_t)evt->value);
            break;
        }
        return;
    }

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

    /* Fold the scroll-context latch into the frame being accumulated (see
     * zmk/raw_touch/scroll.h for why the latch is already correct on the
     * first frame of a touch, whichever order the input callbacks run in).
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
    /* feature_slot starts at "none": raw_touch_init() hands out the real                          \
     * ones, and nothing may write a slot it does not own before then. */                          \
    static struct raw_touch_pad_data rt_data_##n = {.feature_slot = -1};                           \
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
        .relayed = DT_NODE_HAS_COMPAT(DT_INST_PHANDLE(n, device), zmk_input_split),                \
    };                                                                                             \
    static void rt_input_cb_##n(struct input_event *evt) {                                         \
        raw_touch_input_event(&rt_config_##n, &rt_data_##n, evt);                                  \
    }                                                                                              \
    INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(DT_INST_PHANDLE(n, device)), rt_input_cb_##n);

DT_INST_FOREACH_STATUS_OKAY(RT_INST)

#define RT_CONFIG_REF(n) &rt_config_##n,
#define RT_DATA_REF(n) &rt_data_##n,

static const struct raw_touch_pad_config *const raw_touch_pads[] = {
    DT_INST_FOREACH_STATUS_OKAY(RT_CONFIG_REF)};

/* Parallel to raw_touch_pads: same pad, same index. Only raw_touch_init()
 * needs it, to hand each pad the feature slot it was given. */
static struct raw_touch_pad_data *const raw_touch_pad_datas[] = {
    DT_INST_FOREACH_STATUS_OKAY(RT_DATA_REF)};

static int raw_touch_init(void) {
    uint8_t pads_present = 0;

    for (size_t i = 0; i < ARRAY_SIZE(raw_touch_pads); i++) {
        uint8_t bit = BIT(raw_touch_pads[i]->pad_id);

        if (pads_present & bit) {
            LOG_ERR("Duplicate raw touch pad-id %d; frames from both pads will be indistinguishable",
                    raw_touch_pads[i]->pad_id);
        }
        pads_present |= bit;
    }

    zmk_raw_touch_hid_set_feature_header(pads_present);

    /* Fill the feature report's pad slots with the present pads in
     * ascending pad-id order, each with its own geometry and orientation.
     * pad-ids are unique (the binding requires it; duplicates are flagged
     * above), so each id fills at most one slot. There is one slot per pad
     * node (hid.h derives the count from the same devicetree instances),
     * so every pad is described unless more than 8 are configured - the
     * ceiling of the pads_present bitmask, warned about below. */
    int slot = 0;

    for (uint8_t id = 0; id < 8 && slot < ZMK_RAW_TOUCH_FEATURE_PAD_SLOTS; id++) {
        for (size_t i = 0; i < ARRAY_SIZE(raw_touch_pads); i++) {
            const struct raw_touch_pad_config *cfg = raw_touch_pads[i];

            if (cfg->pad_id != id) {
                continue;
            }

            /* max_contacts = 1: the module streams a single contact per pad
             * (contact_id 0); a Pinnacle reports one finger anyway.
             *
             * The version is the OWNING half's: this build for a local
             * pad, and 0 (unknown) for a relayed one until its half
             * announces its own - which it does per touch, so the slot is
             * updated rather than filled once here. */
            raw_touch_pad_datas[i]->feature_slot = (int8_t)slot;
            zmk_raw_touch_hid_set_feature_slot(
                slot++, cfg->resolution, RT_ORIENTATION(cfg), cfg->x_max, cfg->y_max, 1,
                cfg->relayed ? 0 : ZMK_RAW_TOUCH_MODULE_VERSION_PACKED);
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
