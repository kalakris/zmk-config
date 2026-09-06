/*
 * Copyright (c) 2026 Mrinal Kalakrishnan
 *
 * SPDX-License-Identifier: MIT
 *
 * Host-claim state: per-endpoint-instance claims with timeout expiry.
 *
 * One claim slot per selectable endpoint instance, indexed by ZMK core's
 * own zmk_endpoint_instance_to_index(). A slot, not a single global
 * claim, because a global boolean is a latent multi-host bug: host A
 * claiming over USB must never mute (or steal) the fallback that host B
 * on a BLE profile relies on. Each endpoint's host claims independently;
 * only the claim belonging to the endpoint ZMK is currently sending to
 * ever suppresses anything.
 *
 * Liveness: every slot has its own delayable expiry work item, re-armed
 * on each claim/refresh write. Claims also clear eagerly on the events
 * that make the claiming host unreachable or irrelevant:
 *
 *  - USB detach/reset (zmk_usb_conn_state_changed leaving the HID state)
 *    clears the USB claim;
 *  - disconnect of a BLE connection clears the claim of the profile
 *    bonded to that peer, whether or not it is the active profile;
 *  - an endpoint switch clears every claim except the newly selected
 *    endpoint's own. The claiming host may no longer be watching after a
 *    switch, and a wrongly-cleared claim self-heals: a live host
 *    refreshes at most timeout/2 later, re-establishing the gate.
 *
 * Threading: claim writes arrive on the USB workqueue or the BT RX
 * thread, expiry runs on the system workqueue, and the engaged check runs
 * on the input dispatch path. The state is a couple of booleans guarded
 * by a spinlock; the expiry handler re-checks k_work_delayable_is_pending
 * under the lock so an in-flight expiry cannot clear a claim that a
 * concurrent refresh just re-armed.
 */


/* Review harness: scheduler/transport stubs below are not Zephyr. */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#define ZMK_ENDPOINT_COUNT 1
#define ZMK_ENDPOINT_STR_LEN 16
#define ZMK_RAW_TOUCH_GATE_CMD_LEN 4
#define ZMK_RAW_TOUCH_GATE_CMD_CLAIM 1
#define ZMK_RAW_TOUCH_GATE_OP_CLAIM 1
#define ZMK_RAW_TOUCH_GATE_OP_RELEASE 0
#define ZMK_RAW_TOUCH_GATE_TIMEOUT_MIN_S 5
#define ZMK_RAW_TOUCH_GATE_TIMEOUT_MAX_S 120
#define K_WORK_RUNNING 1
#define K_WORK_DELAYED 2
#define K_WORK_QUEUED 4
#define K_SECONDS(n) (n)
#define CLAMP(n,l,h) ((n)<(l)?(l):((n)>(h)?(h):(n)))
#define CONTAINER_OF(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define ARRAY_INDEX(a,p) ((p)-(a))
#define LOG_INF(...) ((void)0)
#define LOG_WRN(...) ((void)0)
#define LOG_ERR(...) ((void)0)
#define LOG_DBG(...) ((void)0)
/* Single-thread scheduler: hooks inject another thread only outside locks. */
#define K_SPINLOCK(p) for(int once=1;once;once=0)
struct k_spinlock { int unused; };
struct k_work { int unused; };
struct k_work_delayable { struct k_work work; int flags; };
struct zmk_endpoint_instance { int transport; };
static struct zmk_endpoint_instance endpoint = {0};
static int zmk_endpoint_instance_to_index(struct zmk_endpoint_instance ep) { return 0; }
static void zmk_endpoint_instance_to_str(struct zmk_endpoint_instance ep,char *b,size_t s) { snprintf(b,s,"USB"); }
static struct zmk_endpoint_instance zmk_endpoints_selected(void) { return endpoint; }
static struct k_work_delayable *k_work_delayable_from_work(struct k_work *w) { return CONTAINER_OF(w,struct k_work_delayable,work); }
static int k_work_delayable_busy_get(struct k_work_delayable *w) { return w->flags; }
static void (*before_cancel)(void);
static void (*before_rearm)(void);
static int k_work_cancel_delayable(struct k_work_delayable *w) {
    if (before_cancel) { void (*hook)(void)=before_cancel; before_cancel=NULL; hook(); }
    w->flags &= ~K_WORK_DELAYED; return 0;
}
static int k_work_reschedule(struct k_work_delayable *w,int timeout) {
    if (before_rearm) { void (*hook)(void)=before_rearm; before_rearm=NULL; hook(); }
    w->flags |= K_WORK_DELAYED; return 0;
}
struct gate_claim {
    bool engaged;
    struct k_work_delayable expiry_work;
};

static struct gate_claim gate_claims[ZMK_ENDPOINT_COUNT];
static struct k_spinlock gate_lock;

/* The claim slot for an endpoint index, or NULL if the index is out of
 * range (zmk_endpoint_instance_to_index() returns negative on failure). */
static struct gate_claim *gate_slot(int idx) {
    if (idx < 0 || idx >= ZMK_ENDPOINT_COUNT) {
        return NULL;
    }

    return &gate_claims[idx];
}

static void gate_clear_index(int idx, const char *reason) {
    struct gate_claim *claim = gate_slot(idx);

    if (claim == NULL) {
        return;
    }

    bool cleared = false;

    K_SPINLOCK(&gate_lock) {
        if (claim->engaged) {
            claim->engaged = false;
            cleared = true;
        }
    }

    if (cleared) {
        k_work_cancel_delayable(&claim->expiry_work);
        LOG_INF("Raw touch host claim for endpoint %d cleared (%s)", idx, reason);
    }
}

static void gate_expiry_cb(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct gate_claim *claim = CONTAINER_OF(dwork, struct gate_claim, expiry_work);
    bool cleared = false;

    K_SPINLOCK(&gate_lock) {
        /* A refresh racing this expiry re-armed the delayable before we
         * took the lock; the claim it refreshed must survive. Do NOT use
         * k_work_delayable_is_pending() here: it counts K_WORK_RUNNING --
         * i.e. this very handler -- as pending, so it is always true from
         * inside the callback and the claim would never expire. Test the
         * re-arm flags alone. */
        if (!(k_work_delayable_busy_get(dwork) & (K_WORK_DELAYED | K_WORK_QUEUED)) &&
            claim->engaged) {
            claim->engaged = false;
            cleared = true;
        }
    }

    if (cleared) {
        LOG_WRN("Raw touch host claim for endpoint %d expired without a refresh; "
                "wheel fallback restored",
                (int)ARRAY_INDEX(gate_claims, claim));
    }
}

int zmk_raw_touch_gate_handle_command(struct zmk_endpoint_instance source, const uint8_t *body,
                                      size_t len) {
    if (len != ZMK_RAW_TOUCH_GATE_CMD_LEN) {
        LOG_WRN("Rejected gate command with length %d", (int)len);
        return -EMSGSIZE;
    }

    if (body[0] != ZMK_RAW_TOUCH_GATE_CMD_CLAIM) {
        LOG_WRN("Rejected unknown gate command 0x%02x", body[0]);
        return -ENOTSUP;
    }

    if (body[3] != 0) {
        LOG_WRN("Rejected claim with nonzero reserved byte 0x%02x", body[3]);
        return -EINVAL;
    }

    int idx = zmk_endpoint_instance_to_index(source);
    struct gate_claim *claim = gate_slot(idx);

    if (claim == NULL) {
        LOG_ERR("Gate command from unindexable endpoint (transport %d)", source.transport);
        return -EINVAL;
    }

    char label[ZMK_ENDPOINT_STR_LEN];
    zmk_endpoint_instance_to_str(source, label, sizeof(label));

    switch (body[1]) {
    case ZMK_RAW_TOUCH_GATE_OP_CLAIM: {
        uint8_t timeout_s = body[2];

        if (timeout_s == 0) {
            LOG_WRN("Rejected claim with zero timeout");
            return -EINVAL;
        }

        timeout_s =
            CLAMP(timeout_s, ZMK_RAW_TOUCH_GATE_TIMEOUT_MIN_S, ZMK_RAW_TOUCH_GATE_TIMEOUT_MAX_S);

        bool refresh = false;

        K_SPINLOCK(&gate_lock) {
            refresh = claim->engaged;
            claim->engaged = true;
        }

        /* Re-arm the expiry AFTER engaging, so an in-flight expiry either
         * sees the delayable pending again or has already cleared the old
         * claim before this one was recorded. */
        k_work_reschedule(&claim->expiry_work, K_SECONDS(timeout_s));

        if (!refresh) {
            LOG_INF("Raw touch frames claimed by %s (timeout %us); wheel fallback suppressed "
                    "while %s is selected",
                    label, timeout_s, label);
        } else {
            LOG_DBG("Raw touch claim refreshed by %s (timeout %us)", label, timeout_s);
        }

        return 0;
    }

    case ZMK_RAW_TOUCH_GATE_OP_RELEASE:
        /* body[2] (timeout) is ignored on release. Releasing without a
         * claim is a harmless no-op: release must be safe to send from
         * host shutdown paths. Scoping makes this per-endpoint, so one
         * host's release can never clear another endpoint's claim. */
        gate_clear_index(idx, "released by host");
        return 0;

    default:
        LOG_WRN("Rejected gate command with unknown operation 0x%02x", body[1]);
        return -EINVAL;
    }
}

bool zmk_raw_touch_gate_engaged_for_selected(void) {
    struct gate_claim *claim = gate_slot(zmk_endpoint_instance_to_index(zmk_endpoints_selected()));

    if (claim == NULL) {
        return false;
    }

    bool engaged = false;

    K_SPINLOCK(&gate_lock) { engaged = claim->engaged; }

    return engaged;
}


static void refresh(void) {
    uint8_t command[] = {1,1,30,0};
    assert(zmk_raw_touch_gate_handle_command(endpoint,command,4)==0);
}
static void expire(void) {
    gate_claims[0].expiry_work.flags=K_WORK_RUNNING;
    gate_expiry_cb(&gate_claims[0].expiry_work.work);
    gate_claims[0].expiry_work.flags &= ~K_WORK_RUNNING;
}
int main(void) {
    refresh();
    /* A disconnect/release clears engaged, then BT RX refresh runs before cancellation. */
    before_cancel=refresh;
    gate_clear_index(0,"release/disconnect");
    printf("clear/refresh race: engaged=%d expiry_pending=%d\n",gate_claims[0].engaged,!!(gate_claims[0].expiry_work.flags & K_WORK_DELAYED));
    assert(gate_claims[0].engaged && !(gate_claims[0].expiry_work.flags & K_WORK_DELAYED));
    memset(gate_claims,0,sizeof(gate_claims));
    refresh();
    /* Refresh sets engaged, then already-due expiry executes before re-arm. */
    before_rearm=expire;
    refresh();
    printf("refresh/expiry race: engaged=%d expiry_pending=%d\n",gate_claims[0].engaged,!!(gate_claims[0].expiry_work.flags & K_WORK_DELAYED));
    assert(!gate_claims[0].engaged && (gate_claims[0].expiry_work.flags & K_WORK_DELAYED));
}
