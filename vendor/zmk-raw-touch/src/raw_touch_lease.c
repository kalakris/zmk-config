/*
 * Copyright (c) 2026 Mrinal Kalakrishnan
 *
 * SPDX-License-Identifier: MIT
 *
 * Host-lease state: per-endpoint-instance leases with timeout expiry.
 *
 * One lease slot per selectable endpoint instance, indexed by ZMK core's
 * own zmk_endpoint_instance_to_index(). A slot, not a single global
 * lease, because a global boolean is a latent multi-host bug: host A
 * leasing over USB must never mute (or steal) the fallback that host B
 * on a BLE profile relies on. Each endpoint's host leases independently;
 * only the lease belonging to the endpoint ZMK is currently sending to
 * ever suppresses anything.
 *
 * Liveness: every slot has its own delayable expiry work item, re-armed
 * on each acquire/renew write. Leases also lapse eagerly on the events
 * that make the leasing host unreachable or irrelevant:
 *
 *  - USB detach/reset (zmk_usb_conn_state_changed leaving the HID state)
 *    clears the USB lease;
 *  - disconnect of a BLE connection clears the lease of the profile
 *    bonded to that peer, whether or not it is the active profile;
 *  - an endpoint switch clears every lease except the newly selected
 *    endpoint's own. The leasing host may no longer be watching after a
 *    switch, and a wrongly-cleared lease self-heals: a live host
 *    renews at most timeout/2 later, re-acquiring the lease.
 *
 * Threading: lease writes arrive on the USB workqueue or the BT RX
 * thread, expiry runs on the system workqueue, and the held check runs
 * on the input dispatch path. A slot's state and its timer move together
 * under lease_lock: whoever sets `held` arms the expiry in the same
 * critical section, and whoever clears it cancels the expiry there too.
 * Split across two critical sections they can be interleaved into a lease
 * that is held with no expiry armed (so it never expires) or cleared
 * with one still armed. Both k_work_reschedule() and
 * k_work_cancel_delayable() are ISR-safe and non-blocking, which is what
 * makes holding them under a spinlock legal; their _sync variants are
 * not, and must never appear here.
 *
 * lease_expiry_cb() cannot be brought under that rule -- it IS the timer --
 * so it stays as it was: it takes lease_lock and then re-reads the work's
 * busy flags, declining to clear a lease whose expiry a concurrent
 * renewal has just re-armed. The lock order is the same on every path
 * (lease_lock, then the kernel's own work-queue lock inside the k_work_*
 * call), so the two can never deadlock against each other.
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

#include <zmk/raw_touch/lease.h>
#include <zmk/raw_touch/tap.h>

#if IS_ENABLED(CONFIG_ZMK_RAW_TOUCH_USB)
#include <zmk/usb.h>
#include <zmk/events/usb_conn_state_changed.h>
#endif

#if IS_ENABLED(CONFIG_ZMK_RAW_TOUCH_BLE)
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zmk/ble.h>
#endif

struct lease {
    bool held;
    struct k_work_delayable expiry_work;
};

static struct lease leases[ZMK_ENDPOINT_COUNT];
static struct k_spinlock lease_lock;

/* The lease slot for an endpoint index, or NULL if the index is out of
 * range (zmk_endpoint_instance_to_index() returns negative on failure). */
static struct lease *lease_slot(int idx) {
    if (idx < 0 || idx >= ZMK_ENDPOINT_COUNT) {
        return NULL;
    }

    return &leases[idx];
}

static void lease_clear_index(int idx, const char *reason) {
    struct lease *lease = lease_slot(idx);

    if (lease == NULL) {
        return;
    }

    bool cleared = false;

    K_SPINLOCK(&lease_lock) {
        if (lease->held) {
            lease->held = false;
            /* Inside the lock, with the flag: a lease arriving between the
             * two would otherwise have its freshly armed expiry cancelled
             * here, leaving it held forever. k_work_cancel_delayable()
             * is ISR-safe and non-blocking, so it is legal under a
             * spinlock -- unlike its _sync variant, which waits for a
             * running handler and must never be called from here. */
            k_work_cancel_delayable(&lease->expiry_work);
            cleared = true;
        }
    }

    if (cleared) {
        LOG_INF("Raw touch host lease for endpoint %d cleared (%s)", idx, reason);
    }
}

static void lease_expiry_cb(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct lease *lease = CONTAINER_OF(dwork, struct lease, expiry_work);
    bool cleared = false;

    K_SPINLOCK(&lease_lock) {
        /* A renewal racing this expiry re-armed the delayable before we
         * took the lock; the lease it renewed must survive. Do NOT use
         * k_work_delayable_is_pending() here: it counts K_WORK_RUNNING --
         * i.e. this very handler -- as pending, so it is always true from
         * inside the callback and the lease would never expire. Test the
         * re-arm flags alone. */
        if (!(k_work_delayable_busy_get(dwork) & (K_WORK_DELAYED | K_WORK_QUEUED)) &&
            lease->held) {
            lease->held = false;
            cleared = true;
        }
    }

    if (cleared) {
        LOG_WRN("Raw touch host lease for endpoint %d expired without a renewal; "
                "wheel fallback restored",
                (int)ARRAY_INDEX(leases, lease));
    }
}

int zmk_raw_touch_lease_handle_command(struct zmk_endpoint_instance source, const uint8_t *body,
                                      size_t len) {
    if (len != ZMK_RAW_TOUCH_LEASE_CMD_LEN) {
        LOG_WRN("Rejected lease command with length %d", (int)len);
        return -EMSGSIZE;
    }

    if (body[0] == ZMK_RAW_TOUCH_CMD_TAP_CONFIRM) {
        if (body[2] != 0 || body[3] != 0) {
            LOG_WRN("Rejected tap confirm with nonzero reserved bytes");
            return -EINVAL;
        }

        /* Only the leasing, selected endpoint has seen the touch it is
         * confirming; a stale confirmation from another endpoint (after a
         * switch, or a lapsed lease) must not click. Not an error. */
        if (!zmk_endpoint_instance_eq(source, zmk_endpoints_selected()) ||
            !zmk_raw_touch_lease_held_for_selected()) {
            LOG_DBG("Ignored tap confirm for pad %d from an endpoint without the lease", body[1]);
            return 0;
        }

        return zmk_raw_touch_tap_confirm(body[1]);
    }

    if (body[0] != ZMK_RAW_TOUCH_LEASE_CMD_HOST_LEASE) {
        LOG_WRN("Rejected unknown lease command 0x%02x", body[0]);
        return -ENOTSUP;
    }

    if (body[3] != 0) {
        LOG_WRN("Rejected lease command with nonzero reserved byte 0x%02x", body[3]);
        return -EINVAL;
    }

    int idx = zmk_endpoint_instance_to_index(source);
    struct lease *lease = lease_slot(idx);

    if (lease == NULL) {
        LOG_ERR("Lease command from unindexable endpoint (transport %d)", source.transport);
        return -EINVAL;
    }

    char label[ZMK_ENDPOINT_STR_LEN];
    zmk_endpoint_instance_to_str(source, label, sizeof(label));

    switch (body[1]) {
    case ZMK_RAW_TOUCH_LEASE_OP_ACQUIRE: {
        uint8_t timeout_s = body[2];

        if (timeout_s == 0) {
            LOG_WRN("Rejected lease with zero timeout");
            return -EINVAL;
        }

        timeout_s =
            CLAMP(timeout_s, ZMK_RAW_TOUCH_LEASE_TIMEOUT_MIN_S, ZMK_RAW_TOUCH_LEASE_TIMEOUT_MAX_S);

        bool renewed = false;

        K_SPINLOCK(&lease_lock) {
            renewed = lease->held;
            lease->held = true;

            /* Re-armed under the same lock as the flag, and after setting
             * it, so an expiry racing this lease either sees the delayable
             * pending again (and declines to clear) or has already cleared
             * the old lease before this one was recorded -- and a clear
             * racing it cannot cancel this expiry after the fact.
             * k_work_reschedule() is ISR-safe and non-blocking, so it is
             * legal under a spinlock; the _sync variants, which wait for a
             * running handler, are not. */
            k_work_reschedule(&lease->expiry_work, K_SECONDS(timeout_s));
        }

        if (!renewed) {
            LOG_INF("Raw touch lease acquired by %s (timeout %us); wheel fallback suppressed "
                    "while %s is selected",
                    label, timeout_s, label);
        } else {
            LOG_DBG("Raw touch lease renewed by %s (timeout %us)", label, timeout_s);
        }

        return 0;
    }

    case ZMK_RAW_TOUCH_LEASE_OP_RELEASE:
        /* body[2] (timeout) is ignored on release. Releasing without a
         * lease is a harmless no-op: release must be safe to send from
         * host shutdown paths. Scoping makes this per-endpoint, so one
         * host's release can never clear another endpoint's lease. */
        lease_clear_index(idx, "released by host");
        return 0;

    default:
        LOG_WRN("Rejected lease command with unknown operation 0x%02x", body[1]);
        return -EINVAL;
    }
}

bool zmk_raw_touch_lease_held_for_selected(void) {
    struct lease *lease = lease_slot(zmk_endpoint_instance_to_index(zmk_endpoints_selected()));

    if (lease == NULL) {
        return false;
    }

    bool held = false;

    K_SPINLOCK(&lease_lock) { held = lease->held; }

    return held;
}

static int lease_event_listener(const zmk_event_t *eh) {
    const struct zmk_endpoint_changed *epc = as_zmk_endpoint_changed(eh);

    if (epc != NULL) {
        /* Endpoint switch: clear every lease except the newly selected
         * endpoint's own, so a host already holding a lease on the
         * switched-to endpoint takes over seamlessly while leases left
         * behind cannot go stale out of sight. */
        int selected = zmk_endpoint_instance_to_index(epc->endpoint);

        for (int i = 0; i < ZMK_ENDPOINT_COUNT; i++) {
            if (i != selected) {
                lease_clear_index(i, "endpoint switched away");
            }
        }

        return ZMK_EV_EVENT_BUBBLE;
    }

#if IS_ENABLED(CONFIG_ZMK_RAW_TOUCH_USB)
    const struct zmk_usb_conn_state_changed *usb = as_zmk_usb_conn_state_changed(eh);

    if (usb != NULL && usb->conn_state != ZMK_USB_CONN_HID) {
        struct zmk_endpoint_instance usb_endpoint = {.transport = ZMK_TRANSPORT_USB};

        lease_clear_index(zmk_endpoint_instance_to_index(usb_endpoint), "USB detached");
    }
#endif

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(zmk_raw_touch_lease, lease_event_listener);
ZMK_SUBSCRIPTION(zmk_raw_touch_lease, zmk_endpoint_changed);
#if IS_ENABLED(CONFIG_ZMK_RAW_TOUCH_USB)
ZMK_SUBSCRIPTION(zmk_raw_touch_lease, zmk_usb_conn_state_changed);
#endif

#if IS_ENABLED(CONFIG_ZMK_RAW_TOUCH_BLE)

/* Our own connection callback rather than zmk_ble_active_profile_changed:
 * that event only fires for the ACTIVE profile, and a lease can belong to
 * any connected profile. zmk_ble_profile_index() maps the peer to its
 * profile (returning a negative value for non-host connections such as
 * split peripherals, which this must ignore). */
static void lease_ble_disconnected(struct bt_conn *conn, uint8_t reason) {
    ARG_UNUSED(reason);

    int profile = zmk_ble_profile_index(bt_conn_get_dst(conn));

    if (profile < 0) {
        return;
    }

    struct zmk_endpoint_instance ble_endpoint = {
        .transport = ZMK_TRANSPORT_BLE,
        .ble = {.profile_index = profile},
    };

    lease_clear_index(zmk_endpoint_instance_to_index(ble_endpoint), "BLE profile disconnected");
}

BT_CONN_CB_DEFINE(zmk_raw_touch_lease_conn_callbacks) = {
    .disconnected = lease_ble_disconnected,
};

#endif /* IS_ENABLED(CONFIG_ZMK_RAW_TOUCH_BLE) */

static int lease_init(void) {
    for (size_t i = 0; i < ARRAY_SIZE(leases); i++) {
        k_work_init_delayable(&leases[i].expiry_work, lease_expiry_cb);
    }

    return 0;
}

SYS_INIT(lease_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
