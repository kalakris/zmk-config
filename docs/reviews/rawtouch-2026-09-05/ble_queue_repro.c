/*
 * Copyright (c) 2020 The ZMK Contributors
 * Copyright (c) 2026 Mrinal Kalakrishnan
 *
 * SPDX-License-Identifier: MIT
 *
 * Derived from ZMK's app/src/hog.c (MIT), which in turn derives from
 * Zephyr's samples/bluetooth/peripheral_hids (Apache-2.0, Copyright (c)
 * 2018 Nordic Semiconductor ASA).
 *
 * A second HID-over-GATT service instance carrying only the raw touch
 * report.
 *
 * ZMK core defines its own BT_GATT_SERVICE_DEFINE(..., BT_UUID_HIDS, ...);
 * this is an additional, independent one. HOGP allows a peripheral to expose
 * more than one HID Service, and a host that walks the GATT database will
 * find both. Nothing in ZMK's hog.c is patched.
 *
 * Host support for two HIDS instances on one peripheral is not universal --
 * see the note on CONFIG_ZMK_RAW_TOUCH_BLE in the module Kconfig.
 */


/* Review harness: scheduler/transport stubs below are not Zephyr. */

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <assert.h>
#define CONFIG_ZMK_RAW_TOUCH_BLE_QUEUE_SIZE 8
#define BUILD_ASSERT(x,s) _Static_assert(x,s)
#define ZMK_RAW_TOUCH_FLAGS_TOUCHED 1
#define K_MSEC(x) x
#define K_NO_WAIT 0
#define ARG_UNUSED(x) ((void)(x))
#define LOG_WRN(...) ((void)0)
#define LOG_DBG(...) ((void)0)
#define BT_SECURITY_L2 2
struct zmk_raw_touch_report_body {uint8_t pad_id, contact_id; uint16_t x,y; uint8_t z,flags,seq; uint16_t timestamp;};
struct k_spinlock {int x;};
typedef int k_spinlock_key_t;
static k_spinlock_key_t k_spin_lock(struct k_spinlock *s){return 0;}
static void k_spin_unlock(struct k_spinlock *s,k_spinlock_key_t key){}
struct k_work {int x;};
#define K_WORK_DELAYABLE_DEFINE(name,fn) struct k_work name
static int raw_touch_hog_work_q;
static int k_work_reschedule_for_queue(void *q,void *w,int delay){return 0;}
static int k_work_schedule_for_queue(void *q,void *w,int delay){return 0;}
struct bt_conn {int id;};
static struct bt_conn a={1},b={2};
static struct bt_conn *selected=&a;
static struct bt_conn *zmk_ble_active_profile_conn(void){return selected;}
static void bt_conn_unref(struct bt_conn *c){}
static void bt_conn_set_security(struct bt_conn *c,int n){}
static size_t touch_input_attr_idx=1;
static struct {int attrs[2];} raw_touch_hog_svc;
struct bt_gatt_notify_params {void *attr; void *data; size_t len;};
static int fail_notify=1, delivered_to=0;
static int bt_gatt_notify_cb(struct bt_conn *conn,struct bt_gatt_notify_params *params){
    if(fail_notify)return -ENOMEM;
    delivered_to=conn->id;
    printf("queued for host 1, delivered to host %d, flags=%u\n",conn->id,((struct zmk_raw_touch_report_body*)params->data)->flags);
    return 0;
}
#define RT_TXQ_SIZE CONFIG_ZMK_RAW_TOUCH_BLE_QUEUE_SIZE

BUILD_ASSERT(RT_TXQ_SIZE >= 2 && RT_TXQ_SIZE <= 255,
             "CONFIG_ZMK_RAW_TOUCH_BLE_QUEUE_SIZE must be in [2, 255]");

/* Bounded retry for a release frame stuck behind a transient notify
 * error. RT_TXQ_RELEASE_ATTEMPTS - 1 re-arms of the drain, one BLE
 * connection interval apart, so the worst case (~24 ms) stays far inside
 * the host's ~150 ms silence watchdog. */
#define RT_TXQ_RELEASE_ATTEMPTS 4
#define RT_TXQ_RETRY_DELAY K_MSEC(8)

static struct {
    struct zmk_raw_touch_report_body buf[RT_TXQ_SIZE];
    uint8_t head;  /* slot holding the oldest queued frame */
    uint8_t count; /* frames queued */
} txq;

static struct k_spinlock txq_lock;

static inline bool rt_frame_is_release(const struct zmk_raw_touch_report_body *frame) {
    return (frame->flags & ZMK_RAW_TOUCH_FLAGS_TOUCHED) == 0;
}

/* Ring slot holding the frame `offset` places behind the head. Callers
 * keep offset <= count <= RT_TXQ_SIZE - 1, so one subtraction wraps. */
static inline uint8_t txq_slot(uint8_t offset) {
    uint16_t i = (uint16_t)txq.head + offset;

    return (uint8_t)(i >= RT_TXQ_SIZE ? i - RT_TXQ_SIZE : i);
}

/* Remove the entry at `pos` places behind the head, closing the gap.
 * Caller holds txq_lock. */
static void txq_remove_at(uint8_t pos) {
    if (pos == 0) {
        txq.head = txq_slot(1);
    } else {
        for (uint8_t i = pos; i + 1 < txq.count; i++) {
            txq.buf[txq_slot(i)] = txq.buf[txq_slot(i + 1)];
        }
    }

    txq.count--;
}

enum txq_put_result {
    TXQ_PUT_OK,      /* queued, nothing dropped */
    TXQ_PUT_EVICTED, /* queued after evicting the oldest motion frame */
    TXQ_PUT_FULL,    /* nothing evictable; the incoming frame was dropped */
};

static enum txq_put_result txq_put(const struct zmk_raw_touch_report_body *body) {
    enum txq_put_result res = TXQ_PUT_OK;
    k_spinlock_key_t key = k_spin_lock(&txq_lock);

    if (txq.count == RT_TXQ_SIZE) {
        int victim = -1;

        for (uint8_t i = 1; i < txq.count; i++) {
            if (!rt_frame_is_release(&txq.buf[txq_slot(i)])) {
                victim = i;
                break;
            }
        }

        if (victim < 0) {
            /* Every evictable entry is a release frame: the queue is
             * mis-sized for the number of pads. Drop the incoming frame
             * rather than a release that a host is waiting on. */
            k_spin_unlock(&txq_lock, key);
            return TXQ_PUT_FULL;
        }

        txq_remove_at((uint8_t)victim);
        res = TXQ_PUT_EVICTED;
    }

    txq.buf[txq_slot(txq.count)] = *body;
    txq.count++;

    k_spin_unlock(&txq_lock, key);

    return res;
}

static bool txq_peek(struct zmk_raw_touch_report_body *out) {
    bool have = false;
    k_spinlock_key_t key = k_spin_lock(&txq_lock);

    if (txq.count > 0) {
        *out = txq.buf[txq.head];
        have = true;
    }

    k_spin_unlock(&txq_lock, key);

    return have;
}

static void txq_drop_head(void) {
    k_spinlock_key_t key = k_spin_lock(&txq_lock);

    if (txq.count > 0) {
        txq_remove_at(0);
    }

    k_spin_unlock(&txq_lock, key);
}

static void raw_touch_hog_drain(struct k_work *work);

static K_WORK_DELAYABLE_DEFINE(raw_touch_hog_work, raw_touch_hog_drain);

/* Notify attempts already spent on the release frame currently at the
 * head. Only ever touched from the drain handler, which a work queue can
 * never run concurrently with itself, so it needs no lock. The head only
 * ever changes via txq_drop_head() (eviction deliberately skips slot 0),
 * and every call site below resets this alongside it. */
static uint8_t release_attempts;

static void raw_touch_hog_drain(struct k_work *work) {
    ARG_UNUSED(work);

    struct zmk_raw_touch_report_body report;

    /* One connection lookup per drain, not per frame: the queue holds
     * several frames exactly when BLE batching is in play. The reference
     * zmk_ble_active_profile_conn() hands back is released at the end.
     * With no connection the queue simply waits - the next frame (or the
     * next retry) re-submits this handler. */
    struct bt_conn *conn = zmk_ble_active_profile_conn();
    if (conn == NULL) {
        return;
    }

    while (txq_peek(&report)) {
        struct bt_gatt_notify_params notify_params = {
            .attr = &raw_touch_hog_svc.attrs[touch_input_attr_idx],
            .data = &report,
            .len = sizeof(report),
        };

        int err = bt_gatt_notify_cb(conn, &notify_params);

        if (err == -EPERM) {
            bt_conn_set_security(conn, BT_SECURITY_L2);
        }

        if (err == 0) {
            txq_drop_head();
            release_attempts = 0;
            continue;
        }

        if (rt_frame_is_release(&report) && release_attempts + 1 < RT_TXQ_RELEASE_ATTEMPTS) {
            /* Leave it at the head and come back: no TX buffer now does
             * not mean none next connection event. Deliberately
             * head-of-line - sending the frames behind it first would
             * put a later touch's motion ahead of this touch's release. */
            release_attempts++;
            k_work_reschedule_for_queue(&raw_touch_hog_work_q, &raw_touch_hog_work,
                                        RT_TXQ_RETRY_DELAY);
            break;
        }

        if (rt_frame_is_release(&report)) {
            /* The link, not the buffer pool: nothing to do but let the
             * host's silence watchdog close the gesture. */
            LOG_WRN("Dropped raw touch release frame for pad %u after %d notify failures (%d)",
                    report.pad_id, RT_TXQ_RELEASE_ATTEMPTS, err);
        } else {
            LOG_DBG("Error notifying %d", err);
        }

        txq_drop_head();
        release_attempts = 0;
    }

    bt_conn_unref(conn);
}

int zmk_raw_touch_hog_send_report(struct zmk_raw_touch_report_body *body) {
    if (touch_input_attr_idx == 0) {
        return -ENODEV;
    }

    /* Never blocks: this runs inline on the input dispatch path, where
     * waiting would head-of-line block pointer deltas and taps. */
    enum txq_put_result res = txq_put(body);

    /* K_NO_WAIT via k_work_schedule (not reschedule): if a release-frame
     * retry is already armed, leave its delay alone - the queue cannot be
     * drained past that frame anyway. Zephyr's k_work_schedule submits
     * when the item is idle *or running*, so a frame arriving mid-drain
     * still gets a wakeup. */
    k_work_schedule_for_queue(&raw_touch_hog_work_q, &raw_touch_hog_work, K_NO_WAIT);

    switch (res) {
    case TXQ_PUT_EVICTED:
        LOG_DBG("Raw touch BLE queue full; evicted the oldest motion frame");
        break;
    case TXQ_PUT_FULL:
        LOG_WRN("Raw touch BLE queue (%d) full of undelivered release frames; dropped an "
                "incoming %s frame for pad %u. Raise CONFIG_ZMK_RAW_TOUCH_BLE_QUEUE_SIZE.",
                RT_TXQ_SIZE, rt_frame_is_release(body) ? "release" : "motion", body->pad_id);
        return -ENOBUFS;
    case TXQ_PUT_OK:
        break;
    }

    return 0;
}


int main(void){
    struct zmk_raw_touch_report_body release={.pad_id=0,.flags=6};
    assert(zmk_raw_touch_hog_send_report(&release)==0);
    raw_touch_hog_drain(NULL); /* host A: transient failure, delayed retry */
    assert(txq.count==1);
    selected=&b; /* endpoint/profile switch before delayed retry */
    fail_notify=0;
    raw_touch_hog_drain(NULL);
    assert(delivered_to==2);
}
