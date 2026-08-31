/*
 * Copyright (c) 2026 The ZMK Contributors
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

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/sys/util.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk_raw_touch, CONFIG_ZMK_RAW_TOUCH_LOG_LEVEL);

#include <zmk/endpoints.h>
#include <zmk/endpoints_types.h>
#include <zmk/event_manager.h>
#include <zmk/events/endpoint_changed.h>

#include <zmk/raw_touch/gate.h>

#if IS_ENABLED(CONFIG_ZMK_RAW_TOUCH_USB)
#include <zmk/usb.h>
#include <zmk/events/usb_conn_state_changed.h>
#endif

#if IS_ENABLED(CONFIG_ZMK_RAW_TOUCH_BLE)
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zmk/ble.h>
#endif

struct gate_claim {
    bool engaged;
    struct k_work_delayable expiry_work;
};

static struct gate_claim gate_claims[ZMK_ENDPOINT_COUNT];
static struct k_spinlock gate_lock;

static void gate_clear_index(int idx, const char *reason) {
    if (idx < 0 || idx >= ZMK_ENDPOINT_COUNT) {
        return;
    }

    bool cleared = false;

    K_SPINLOCK(&gate_lock) {
        if (gate_claims[idx].engaged) {
            gate_claims[idx].engaged = false;
            cleared = true;
        }
    }

    if (cleared) {
        k_work_cancel_delayable(&gate_claims[idx].expiry_work);
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
         * inside the callback and the claim would never expire (caught on
         * hardware, BENCH-mode-gate.md section 2). Test the re-arm flags
         * alone. */
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

    if (idx < 0 || idx >= ZMK_ENDPOINT_COUNT) {
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
            refresh = gate_claims[idx].engaged;
            gate_claims[idx].engaged = true;
        }

        /* Re-arm the expiry AFTER engaging, so an in-flight expiry either
         * sees the delayable pending again or has already cleared the old
         * claim before this one was recorded. */
        k_work_reschedule(&gate_claims[idx].expiry_work, K_SECONDS(timeout_s));

        if (!refresh) {
            LOG_INF("Raw touch stream claimed by %s (timeout %us); wheel fallback suppressed "
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
    int idx = zmk_endpoint_instance_to_index(zmk_endpoints_selected());

    if (idx < 0 || idx >= ZMK_ENDPOINT_COUNT) {
        return false;
    }

    bool engaged = false;

    K_SPINLOCK(&gate_lock) { engaged = gate_claims[idx].engaged; }

    return engaged;
}

static int gate_event_listener(const zmk_event_t *eh) {
    const struct zmk_endpoint_changed *epc = as_zmk_endpoint_changed(eh);

    if (epc != NULL) {
        /* Endpoint switch: clear every claim except the newly selected
         * endpoint's own, so a host already holding a claim on the
         * switched-to endpoint engages seamlessly while claims left
         * behind cannot go stale out of sight. */
        int selected = zmk_endpoint_instance_to_index(epc->endpoint);

        for (int i = 0; i < ZMK_ENDPOINT_COUNT; i++) {
            if (i != selected) {
                gate_clear_index(i, "endpoint switched away");
            }
        }

        return ZMK_EV_EVENT_BUBBLE;
    }

#if IS_ENABLED(CONFIG_ZMK_RAW_TOUCH_USB)
    const struct zmk_usb_conn_state_changed *usb = as_zmk_usb_conn_state_changed(eh);

    if (usb != NULL && usb->conn_state != ZMK_USB_CONN_HID) {
        struct zmk_endpoint_instance usb_endpoint = {.transport = ZMK_TRANSPORT_USB};

        gate_clear_index(zmk_endpoint_instance_to_index(usb_endpoint), "USB detached");
    }
#endif

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(zmk_raw_touch_gate, gate_event_listener);
ZMK_SUBSCRIPTION(zmk_raw_touch_gate, zmk_endpoint_changed);
#if IS_ENABLED(CONFIG_ZMK_RAW_TOUCH_USB)
ZMK_SUBSCRIPTION(zmk_raw_touch_gate, zmk_usb_conn_state_changed);
#endif

#if IS_ENABLED(CONFIG_ZMK_RAW_TOUCH_BLE)

/* Our own connection callback rather than zmk_ble_active_profile_changed:
 * that event only fires for the ACTIVE profile, and a claim can belong to
 * any connected profile. zmk_ble_profile_index() maps the peer to its
 * profile (returning a negative value for non-host connections such as
 * split peripherals, which this must ignore). */
static void gate_ble_disconnected(struct bt_conn *conn, uint8_t reason) {
    ARG_UNUSED(reason);

    int profile = zmk_ble_profile_index(bt_conn_get_dst(conn));

    if (profile < 0) {
        return;
    }

    struct zmk_endpoint_instance ble_endpoint = {
        .transport = ZMK_TRANSPORT_BLE,
        .ble = {.profile_index = profile},
    };

    gate_clear_index(zmk_endpoint_instance_to_index(ble_endpoint), "BLE profile disconnected");
}

BT_CONN_CB_DEFINE(zmk_raw_touch_gate_conn_callbacks) = {
    .disconnected = gate_ble_disconnected,
};

#endif /* IS_ENABLED(CONFIG_ZMK_RAW_TOUCH_BLE) */

static int gate_init(void) {
    for (size_t i = 0; i < ARRAY_SIZE(gate_claims); i++) {
        k_work_init_delayable(&gate_claims[i].expiry_work, gate_expiry_cb);
    }

    return 0;
}

SYS_INIT(gate_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
