/*
 * Copyright (c) 2026 Mrinal Kalakrishnan
 *
 * SPDX-License-Identifier: MIT
 *
 * The transmit ring both transports queue frames in
 * (src/raw_touch_hog.c and src/raw_touch_usb_hid.c).
 *
 * Internal to the module - deliberately under src/ rather than include/,
 * because nothing outside the two transports may depend on it.
 *
 * A hand-rolled ring under a spinlock rather than a K_MSGQ, because
 * RELEASE frames (ZMK_RAW_TOUCH_FLAGS_TOUCHED clear: the lift-off frame,
 * and the synthetic one emitted when a lease lapses mid-touch) need
 * guarantees a msgq cannot express. Losing a release leaves the host
 * holding a phantom finger-down - runaway momentum until its own silence
 * watchdog fires.
 *
 *  1. Never evicted. A msgq that is full can only be made room in by
 *     dropping its head, which is whatever frame happens to be oldest -
 *     quite possibly the other pad's release. Here a full queue evicts
 *     the oldest MOTION frame instead (stale position data nobody will
 *     miss: the next frame is ~10 ms behind it and carries an absolute
 *     position, so nothing is reconstructed from what is dropped). Only
 *     when every evictable entry is a release does an incoming frame get
 *     dropped instead.
 *  2. Ordering. One FIFO for all pads, drained strictly head-first. Per-pad
 *     ordering therefore holds for free - a release never overtakes earlier
 *     motion of the same pad, and the next touch's frames never overtake
 *     the release that closed the previous one. Eviction removes an
 *     interior entry and closes the gap, which preserves the relative
 *     order of everything else.
 *  3. Retryability. The head can be examined without being consumed
 *     (rt_txq_peek) so a transport can re-attempt a frame that failed for
 *     a transient reason and keep it at the head meanwhile.
 *
 * Slot 0 is never an eviction candidate. A transport that peeks the head,
 * drops the lock to transmit, and then drops the head by position would
 * otherwise discard the frame that moved into slot 0 behind an evicted
 * head.
 *
 * The generation counter closes the same window against the flush paths,
 * which run from other threads entirely (an endpoint switch, a disconnect,
 * a USB bus reset). Every operation that replaces what sits at the head
 * without going through rt_txq_drop_head() bumps it; rt_txq_peek() hands
 * the caller the current value and rt_txq_drop_head() refuses to drop if
 * it no longer matches. The transport then simply re-peeks: the frame it
 * had in hand is gone, and whatever is at the head now has not been sent.
 * Interior removals leave the head where it is and so do NOT bump the
 * generation - bumping there would cost a duplicate transmission for no
 * gain.
 *
 * `binding` scopes an entry to the destination it was sampled for. BLE
 * stores the active profile index, so a frame queued for one host is
 * discarded rather than delivered to another after a profile switch (its
 * `lease_held` bit belongs to the endpoint it was sampled for; see
 * zmk/raw_touch/lease.h). USB has one bus and one host, so it queues
 * everything with RT_TXQ_NO_BINDING.
 *
 * Every entry point takes the ring's own spinlock: the producer runs on
 * the input dispatch thread while the drains run on the module's BLE work
 * queue and in USB callback context, and a spinlock (which locks
 * interrupts) is the only cheap primitive valid in all three.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <zmk/raw_touch/hid.h>

/* Binding for a transport with nothing to bind to: USB has a single bus
 * and a single host. Never equal to a BLE profile index, which is >= 0. */
#define RT_TXQ_NO_BINDING ((int16_t)-1)

struct rt_txq_entry {
    struct zmk_raw_touch_report_body body;

    /* The destination this frame was sampled for; see the block comment. */
    int16_t binding;
};

struct rt_txq {
    struct rt_txq_entry *buf;
    uint8_t size;  /* entries in buf */
    uint8_t head;  /* slot holding the oldest queued frame */
    uint8_t count; /* frames queued */

    /* Bumped whenever the head is replaced by anything other than
     * rt_txq_drop_head(). Wrapping is harmless: only equality matters. */
    uint32_t generation;

    struct k_spinlock lock;
};

/* Define a ring and its storage. `queue_size` comes from Kconfig, whose
 * own `range 2 255` this re-asserts at the point of use - a size below 2
 * has no evictable slot at all (slot 0 is never a candidate), and head /
 * count are uint8_t. */
#define RT_TXQ_DEFINE(name, queue_size)                                                            \
    BUILD_ASSERT((queue_size) >= 2 && (queue_size) <= 255,                                         \
                 "raw touch transmit queue size must be in [2, 255]");                             \
    static struct rt_txq_entry name##_entries[queue_size];                                         \
    static struct rt_txq name = {.buf = name##_entries, .size = (queue_size)}

enum rt_txq_put_result {
    RT_TXQ_PUT_OK,      /* queued, nothing dropped */
    RT_TXQ_PUT_EVICTED, /* queued after evicting the oldest motion frame */
    RT_TXQ_PUT_FULL,    /* nothing evictable; the incoming frame was dropped */
};

static inline bool rt_txq_is_release(const struct zmk_raw_touch_report_body *frame) {
    return (frame->flags & ZMK_RAW_TOUCH_FLAGS_TOUCHED) == 0;
}

/* Ring slot holding the frame `offset` places behind the head. Callers
 * keep offset <= count <= size - 1, so one subtraction wraps. */
static inline uint8_t rt_txq_slot(const struct rt_txq *q, uint8_t offset) {
    uint16_t i = (uint16_t)q->head + offset;

    return (uint8_t)(i >= q->size ? i - q->size : i);
}

/* Remove the entry at `pos` places behind the head, closing the gap.
 * Caller holds q->lock. Returns true if the head itself was removed, which
 * is what the generation counter tracks. */
static inline bool rt_txq_remove_at(struct rt_txq *q, uint8_t pos) {
    if (pos == 0) {
        q->head = rt_txq_slot(q, 1);
        q->count--;
        return true;
    }

    for (uint8_t i = pos; i + 1 < q->count; i++) {
        q->buf[rt_txq_slot(q, i)] = q->buf[rt_txq_slot(q, i + 1)];
    }

    q->count--;
    return false;
}

static inline enum rt_txq_put_result
rt_txq_put(struct rt_txq *q, const struct zmk_raw_touch_report_body *body, int16_t binding) {
    enum rt_txq_put_result res = RT_TXQ_PUT_OK;
    k_spinlock_key_t key = k_spin_lock(&q->lock);

    if (q->count == q->size) {
        int victim = -1;

        for (uint8_t i = 1; i < q->count; i++) {
            if (!rt_txq_is_release(&q->buf[rt_txq_slot(q, i)].body)) {
                victim = i;
                break;
            }
        }

        if (victim < 0) {
            /* Every evictable entry is a release frame: the queue is
             * mis-sized for the number of pads. Drop the incoming frame
             * rather than a release that a host is waiting on. */
            k_spin_unlock(&q->lock, key);
            return RT_TXQ_PUT_FULL;
        }

        rt_txq_remove_at(q, (uint8_t)victim);
        res = RT_TXQ_PUT_EVICTED;
    }

    q->buf[rt_txq_slot(q, q->count)].body = *body;
    q->buf[rt_txq_slot(q, q->count)].binding = binding;
    q->count++;

    k_spin_unlock(&q->lock, key);

    return res;
}

/* Copy the head out without consuming it, along with the generation to
 * hand back to rt_txq_drop_head() once it has been transmitted. */
static inline bool rt_txq_peek(struct rt_txq *q, struct rt_txq_entry *out, uint32_t *generation) {
    bool have = false;
    k_spinlock_key_t key = k_spin_lock(&q->lock);

    if (q->count > 0) {
        *out = q->buf[q->head];
        *generation = q->generation;
        have = true;
    }

    k_spin_unlock(&q->lock, key);

    return have;
}

/* Drop the frame a previous rt_txq_peek() returned. Refuses (returning
 * false) if the head has been replaced since that peek - see the
 * generation discussion in the block comment. */
static inline bool rt_txq_drop_head(struct rt_txq *q, uint32_t generation) {
    bool dropped = false;
    k_spinlock_key_t key = k_spin_lock(&q->lock);

    if (q->count > 0 && q->generation == generation) {
        rt_txq_remove_at(q, 0);
        dropped = true;
    }

    k_spin_unlock(&q->lock, key);

    return dropped;
}

/* Peek and drop as one critical section, for a transport that hands the
 * frame to hardware synchronously and so has no retry window to protect.
 * Bumps the generation like every other head replacement, so mixing this
 * with peek/drop_head in one queue would still be safe. */
static inline bool rt_txq_pop(struct rt_txq *q, struct rt_txq_entry *out) {
    bool have = false;
    k_spinlock_key_t key = k_spin_lock(&q->lock);

    if (q->count > 0) {
        *out = q->buf[q->head];
        rt_txq_remove_at(q, 0);
        q->generation++;
        have = true;
    }

    k_spin_unlock(&q->lock, key);

    return have;
}

/* Discard everything. For the events that make every queued frame
 * meaningless at once: a USB bus reset or detach, an endpoint switch. */
static inline void rt_txq_flush(struct rt_txq *q) {
    k_spinlock_key_t key = k_spin_lock(&q->lock);

    q->head = 0;
    q->count = 0;
    q->generation++;

    k_spin_unlock(&q->lock, key);
}

/* Discard the entries queued for one destination, leaving the others in
 * order. For a disconnect, which invalidates one BLE profile's frames
 * while another profile's may still be live. */
static inline void rt_txq_discard_binding(struct rt_txq *q, int16_t binding) {
    k_spinlock_key_t key = k_spin_lock(&q->lock);

    for (uint8_t i = 0; i < q->count;) {
        if (q->buf[rt_txq_slot(q, i)].binding == binding) {
            if (rt_txq_remove_at(q, i)) {
                q->generation++;
            }
            /* Everything behind the removed entry shifted down into i. */
        } else {
            i++;
        }
    }

    k_spin_unlock(&q->lock, key);
}
