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

#include <string.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk_raw_touch, CONFIG_ZMK_RAW_TOUCH_LOG_LEVEL);

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>

#include <zmk/ble.h>

#include <zmk/endpoints_types.h>

#include <zmk/raw_touch/gate.h>
#include <zmk/raw_touch/hid.h>
#include <zmk/raw_touch/transport.h>

enum {
    HIDS_REMOTE_WAKE = BIT(0),
    HIDS_NORMALLY_CONNECTABLE = BIT(1),
};

enum {
    HIDS_INPUT = 0x01,
    HIDS_FEATURE = 0x03,
};

struct hids_info {
    uint16_t version; /* version number of base USB HID Specification */
    uint8_t code;     /* country HID Device hardware is localized for */
    uint8_t flags;
} __packed;

struct hids_report {
    uint8_t id;   /* report id */
    uint8_t type; /* report type */
} __packed;

static struct hids_info info = {
    .version = 0x0000,
    .code = 0x00,
    .flags = HIDS_NORMALLY_CONNECTABLE | HIDS_REMOTE_WAKE,
};

/* Over BLE the report ID is NOT part of the notification payload -- it lives
 * here, in the report reference descriptor. That is why the notify below
 * sends sizeof(body) starting at body, while USB sends the whole struct
 * starting at the report_id byte. */
static struct hids_report touch_input = {
    .id = ZMK_RAW_TOUCH_REPORT_ID,
    .type = HIDS_INPUT,
};

static struct hids_report touch_feature = {
    .id = ZMK_RAW_TOUCH_REPORT_ID,
    .type = HIDS_FEATURE,
};

static uint8_t ctrl_point;

static ssize_t read_hids_info(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
                              uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, attr->user_data,
                             sizeof(struct hids_info));
}

static ssize_t read_hids_report_ref(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                    void *buf, uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, attr->user_data,
                             sizeof(struct hids_report));
}

static ssize_t read_hids_report_map(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                    void *buf, uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, zmk_raw_touch_report_desc,
                             zmk_raw_touch_report_desc_size);
}

/* A real read callback rather than NULL: macOS and iOS commonly read every
 * report characteristic when they connect, and a NULL read handler answers
 * those with an ATT error. It also gives the boot-time scan below a distinct
 * function pointer to find this characteristic by. */
static ssize_t read_hids_touch_input_report(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                            void *buf, uint16_t len, uint16_t offset) {
    struct zmk_raw_touch_report_body *body = &zmk_raw_touch_hid_get_report()->body;
    return bt_gatt_attr_read(conn, attr, buf, len, offset, body,
                             sizeof(struct zmk_raw_touch_report_body));
}

static ssize_t read_hids_touch_feature_report(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                              void *buf, uint16_t len, uint16_t offset) {
    struct zmk_raw_touch_feature_body *body = &zmk_raw_touch_hid_get_feature_report()->body;
    return bt_gatt_attr_read(conn, attr, buf, len, offset, body,
                             sizeof(struct zmk_raw_touch_feature_body));
}

static void input_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value) {
    LOG_DBG("Raw touch notifications %s",
            (value == BT_GATT_CCC_NOTIFY) ? "subscribed" : "unsubscribed");
}

/* Host claim: the feature report's write path (HOGP report
 * characteristics of type Feature are read/write per HIDS 1.0 §2.5.2).
 * The GATT write carries the 4-byte body alone - the report ID lives in
 * the report reference descriptor, as on the input report's notify path.
 *
 * The connection identifies the claimant: its peer address maps to the
 * ZMK BLE profile, and the claim is scoped to that profile's endpoint
 * instance. Writes from a peer that is not a bonded host profile (e.g. a
 * split peripheral's connection) are refused. */
static ssize_t write_hids_touch_feature_report(struct bt_conn *conn,
                                               const struct bt_gatt_attr *attr, const void *buf,
                                               uint16_t len, uint16_t offset, uint8_t flags) {
    ARG_UNUSED(attr);
    ARG_UNUSED(flags);

    if (offset != 0) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    int profile = zmk_ble_profile_index(bt_conn_get_dst(conn));

    if (profile < 0) {
        return BT_GATT_ERR(BT_ATT_ERR_WRITE_NOT_PERMITTED);
    }

    struct zmk_endpoint_instance source = {
        .transport = ZMK_TRANSPORT_BLE,
        .ble = {.profile_index = profile},
    };

    int err = zmk_raw_touch_gate_handle_command(source, buf, len);

    switch (err) {
    case 0:
        return len;
    case -EMSGSIZE:
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    default:
        return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
    }
}

static ssize_t write_ctrl_point(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                const void *buf, uint16_t len, uint16_t offset, uint8_t flags) {
    uint8_t *value = attr->user_data;

    if (offset + len > sizeof(ctrl_point)) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    memcpy(value + offset, buf, len);

    return len;
}

/* Raw touch HID Service Declaration */
BT_GATT_SERVICE_DEFINE(
    raw_touch_hog_svc, BT_GATT_PRIMARY_SERVICE(BT_UUID_HIDS),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_INFO, BT_GATT_CHRC_READ, BT_GATT_PERM_READ, read_hids_info,
                           NULL, &info),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT_MAP, BT_GATT_CHRC_READ, BT_GATT_PERM_READ_ENCRYPT,
                           read_hids_report_map, NULL, NULL),

    /* Input report: notified per touch frame, and readable. */
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ_ENCRYPT, read_hids_touch_input_report, NULL, NULL),
    BT_GATT_CCC(input_ccc_changed, BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT),
    BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF, BT_GATT_PERM_READ_ENCRYPT, read_hids_report_ref,
                       NULL, &touch_input),

    /* Feature report: pad capabilities on read, host claim on write;
     * no CCC (feature reports are never notified). This is the BLE
     * counterpart of the USB GET_REPORT/SET_REPORT(FEATURE) paths; hosts
     * must read and validate it before treating the collection as raw
     * touch. Feature reports are read/write per HIDS 1.0 s2.5.2; the
     * write permission is a characteristic property only and does not
     * change the report map or attribute layout, so it does not
     * invalidate a host's cached GATT database. */
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT, BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT,
                           read_hids_touch_feature_report, write_hids_touch_feature_report, NULL),
    BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF, BT_GATT_PERM_READ_ENCRYPT, read_hids_report_ref,
                       NULL, &touch_feature),

    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_CTRL_POINT, BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE, NULL, write_ctrl_point, &ctrl_point));

/* Index of the input report's *characteristic declaration* attribute, which
 * is what bt_gatt_notify_cb() wants (it walks forward to the value handle
 * itself). Discovered at boot rather than hand-counted, because the
 * characteristics above can become Kconfig-conditional and a literal index
 * would then silently point at the wrong attribute. 0 means "not found":
 * attrs[0] is the primary service declaration, never a report. */
static size_t touch_input_attr_idx;

K_THREAD_STACK_DEFINE(raw_touch_hog_q_stack, CONFIG_ZMK_RAW_TOUCH_BLE_THREAD_STACK_SIZE);

static struct k_work_q raw_touch_hog_work_q;

K_MSGQ_DEFINE(raw_touch_hog_msgq, sizeof(struct zmk_raw_touch_report_body),
              CONFIG_ZMK_RAW_TOUCH_BLE_QUEUE_SIZE, 4);

static void send_touch_report_callback(struct k_work *work) {
    struct zmk_raw_touch_report_body report;

    /* One connection lookup per drain, not per frame: the queue holds
     * several frames exactly when BLE batching is in play. The reference
     * zmk_ble_active_profile_conn() hands back is released at the end. */
    struct bt_conn *conn = zmk_ble_active_profile_conn();
    if (conn == NULL) {
        return;
    }

    while (k_msgq_get(&raw_touch_hog_msgq, &report, K_NO_WAIT) == 0) {
        struct bt_gatt_notify_params notify_params = {
            .attr = &raw_touch_hog_svc.attrs[touch_input_attr_idx],
            .data = &report,
            .len = sizeof(report),
        };

        int err = bt_gatt_notify_cb(conn, &notify_params);
        if (err == -EPERM) {
            bt_conn_set_security(conn, BT_SECURITY_L2);
        } else if (err) {
            LOG_DBG("Error notifying %d", err);
        }
    }

    bt_conn_unref(conn);
}

K_WORK_DEFINE(raw_touch_hog_work, send_touch_report_callback);

int zmk_raw_touch_hog_send_report(struct zmk_raw_touch_report_body *body) {
    if (touch_input_attr_idx == 0) {
        return -ENODEV;
    }

    /* Never block the input processing path: touch frames arrive at ~100 Hz
     * and are ephemeral, so a full queue means the connection cannot keep up
     * and the oldest frame is the one worth losing. */
    int err = k_msgq_put(&raw_touch_hog_msgq, body, K_NO_WAIT);
    if (err) {
        struct zmk_raw_touch_report_body discarded;
        k_msgq_get(&raw_touch_hog_msgq, &discarded, K_NO_WAIT);
        err = k_msgq_put(&raw_touch_hog_msgq, body, K_NO_WAIT);
        if (err) {
            LOG_WRN("Failed to queue raw touch report to send (%d)", err);
            return err;
        }
    }

    k_work_submit_to_queue(&raw_touch_hog_work_q, &raw_touch_hog_work);

    return 0;
}

static int raw_touch_hog_init(void) {
    /* BT_GATT_CHARACTERISTIC() emits two attributes -- the declaration and
     * then the value -- so the declaration sits one before the attribute
     * carrying our read callback. */
    for (size_t i = 0; i < raw_touch_hog_svc.attr_count; i++) {
        if (raw_touch_hog_svc.attrs[i].read == read_hids_touch_input_report) {
            touch_input_attr_idx = i - 1;
            break;
        }
    }

    if (touch_input_attr_idx == 0) {
        LOG_ERR("Could not locate the raw touch input report characteristic");
        return -ENODEV;
    }

    static const struct k_work_queue_config queue_config = {.name = "Raw Touch HOG Send Work"};
    k_work_queue_start(&raw_touch_hog_work_q, raw_touch_hog_q_stack,
                       K_THREAD_STACK_SIZEOF(raw_touch_hog_q_stack), CONFIG_ZMK_BLE_THREAD_PRIORITY,
                       &queue_config);

    return 0;
}

SYS_INIT(raw_touch_hog_init, APPLICATION, CONFIG_ZMK_BLE_INIT_PRIORITY);
