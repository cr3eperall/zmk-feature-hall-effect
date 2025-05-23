/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/settings/settings.h>
#include <zephyr/init.h>

#include <zephyr/logging/log.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>

#include <zmk/ble.h>
#include <zmk/endpoints_types.h>
#include <zmk/hog.h>
#include <zmk/hid.h>

#include <he/hid/hid.h>
LOG_MODULE_DECLARE(feature_hall_effect, CONFIG_HE_LOG_LEVEL);

enum {
    HIDS_REMOTE_WAKE = BIT(0),
    HIDS_NORMALLY_CONNECTABLE = BIT(1),
};

struct hids_info {
    uint16_t version; /* version number of base USB HID Specification */
    uint8_t code;     /* country HID Device hardware is localized for. */
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

enum {
    HIDS_INPUT = 0x01,
    HIDS_OUTPUT = 0x02,
    HIDS_FEATURE = 0x03,
};

static struct hids_report gamepad_input = {
    .id = HE_GAMEPAD_HID_REPORT_ID,
    .type = HIDS_INPUT,
};

static bool host_requests_notification = false;
static uint8_t ctrl_point;
// static uint8_t proto_mode;

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
    return bt_gatt_attr_read(conn, attr, buf, len, offset, zmk_hid_gamepad_report_desc,
                             sizeof(zmk_hid_gamepad_report_desc));
}
//START
size_t bt_gatt_char_offset_gamepad = 0;
static ssize_t read_hids_gamepad_input_report(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                            void *buf, uint16_t len, uint16_t offset) {
    struct zmk_hid_gamepad_report_body *report_body = &zmk_hid_get_gamepad_report()->body;
    return bt_gatt_attr_read(conn, attr, buf, len, offset, report_body,
                             sizeof(struct zmk_hid_gamepad_report_body));
}

static void input_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value) {
    host_requests_notification = (value == BT_GATT_CCC_NOTIFY) ? 1 : 0;
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

/* HID Service Declaration */
BT_GATT_SERVICE_DEFINE(
    hog_svc_gamepad, BT_GATT_PRIMARY_SERVICE(BT_UUID_HIDS),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_INFO, BT_GATT_CHRC_READ, BT_GATT_PERM_READ, read_hids_info,
                           NULL, &info),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT_MAP, BT_GATT_CHRC_READ, BT_GATT_PERM_READ_ENCRYPT,
                           read_hids_report_map, NULL, NULL),

    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ_ENCRYPT, read_hids_gamepad_input_report, NULL, NULL),
    BT_GATT_CCC(input_ccc_changed, BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT),
    BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF, BT_GATT_PERM_READ_ENCRYPT, read_hids_report_ref,
                       NULL, &gamepad_input),

    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_CTRL_POINT, BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE, NULL, write_ctrl_point, &ctrl_point));


K_THREAD_STACK_DEFINE(hog_gamepad_q_stack, CONFIG_ZMK_BLE_THREAD_STACK_SIZE);

struct k_work_q hog_gamepad_work_q;

K_MSGQ_DEFINE(zmk_hog_gamepad_msgq, sizeof(struct zmk_hid_gamepad_report_body),
              CONFIG_ZMK_BLE_GAMEPAD_REPORT_QUEUE_SIZE, 4);

void send_gamepad_report_callback(struct k_work *work) {
    struct zmk_hid_gamepad_report_body report;
    while (k_msgq_get(&zmk_hog_gamepad_msgq, &report, K_NO_WAIT) == 0) {
        struct bt_conn *conn = zmk_ble_active_profile_conn();
        if (conn == NULL) {
            return;
        }

        struct bt_gatt_notify_params notify_params = {
            .attr = &hog_svc_gamepad.attrs[ bt_gatt_char_offset_gamepad ],
            .data = &report,
            .len = sizeof(report),
        };

        int err = bt_gatt_notify_cb(conn, &notify_params);
        if (err == -EPERM) {
            bt_conn_set_security(conn, BT_SECURITY_L2);
        } else if (err) {
            LOG_DBG("Error notifying %d", err);
        }

        bt_conn_unref(conn);
    }
};

K_WORK_DEFINE(hog_gamepad_work, send_gamepad_report_callback);

int zmk_hog_send_gamepad_report(struct zmk_hid_gamepad_report_body *report) {
    int err = k_msgq_put(&zmk_hog_gamepad_msgq, report, K_MSEC(100));
    if (err) {
        switch (err) {
        case -EAGAIN: {
            LOG_WRN("gamepad message queue full, popping first message and queueing again");
            struct zmk_hid_gamepad_report_body discarded_report;
            k_msgq_get(&zmk_hog_gamepad_msgq, &discarded_report, K_NO_WAIT);
            return zmk_hog_send_gamepad_report(report);
        }
        default:
            LOG_WRN("Failed to queue gamepad report to send (%d)", err);
            return err;
        }
    }

    k_work_submit_to_queue(&hog_gamepad_work_q, &hog_gamepad_work);

    return 0;
};

static int zmk_hog_init(void) {

    for (size_t i = 0; i < hog_svc_gamepad.attr_count; i++) {
        // scan the cb from output of BT_GATT_CHARACTERISTIC() macros,
        // each output inserts 2 elements into attrs array, so we minus one the offset.

        if (hog_svc_gamepad.attrs[i].read == read_hids_gamepad_input_report) {
            bt_gatt_char_offset_gamepad = i - 1;
        }

    }

    static const struct k_work_queue_config queue_config = {.name = "HID Over GATT Send Work"};
    k_work_queue_start(&hog_gamepad_work_q, hog_gamepad_q_stack, K_THREAD_STACK_SIZEOF(hog_gamepad_q_stack),
                       CONFIG_ZMK_BLE_THREAD_PRIORITY, &queue_config);

    return 0;
}

SYS_INIT(zmk_hog_init, APPLICATION, CONFIG_ZMK_BLE_INIT_PRIORITY);
