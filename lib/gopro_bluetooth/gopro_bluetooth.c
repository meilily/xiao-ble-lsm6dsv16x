/* main.c - Application main entry point */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <app/lib/xiao_gopro_bluetooth.h>
#include <zephyr/types.h>
#include <stddef.h>
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/sys/byteorder.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(gopro_bt, CONFIG_XIAO_GOPRO_BLUETOOTH_LOG_LEVEL);

static void start_scan(void);

static struct bt_conn *default_conn;

static struct bt_uuid_16 discover_uuid = BT_UUID_INIT_16(0);
static struct bt_gatt_discover_params discover_params;
static struct bt_gatt_subscribe_params subscribe_params;

uint64_t total_rx_count; /* This value is exposed to test code */

static uint8_t notify_func(struct bt_conn *conn,
			   struct bt_gatt_subscribe_params *params,
			   const void *data, uint16_t length)
{
	if (!data) {
		LOG_DBG("[UNSUBSCRIBED]");
		params->value_handle = 0U;
		return BT_GATT_ITER_STOP;
	}

	LOG_DBG("[NOTIFICATION] data %p length %u", data, length);

	total_rx_count++;

	return BT_GATT_ITER_CONTINUE;
}

static uint8_t discover_func(struct bt_conn *conn,
			     const struct bt_gatt_attr *attr,
			     struct bt_gatt_discover_params *params)
{
	int err;

	if (!attr) {
		LOG_DBG("Discover complete");
		(void)memset(params, 0, sizeof(*params));
		return BT_GATT_ITER_STOP;
	}

	LOG_DBG("[ATTRIBUTE] handle %u", attr->handle);

	if (!bt_uuid_cmp(discover_params.uuid, BT_UUID_GOPRO)) {
		memcpy(&discover_uuid, BT_UUID_HRS_MEASUREMENT, sizeof(discover_uuid));
		discover_params.uuid = &discover_uuid.uuid;
		discover_params.start_handle = attr->handle + 1;
		discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

		err = bt_gatt_discover(conn, &discover_params);
		if (err) {
			LOG_DBG("Discover failed (err %d)", err);
		}
	} else if (!bt_uuid_cmp(discover_params.uuid,
				BT_UUID_HRS_MEASUREMENT)) {
		memcpy(&discover_uuid, BT_UUID_GATT_CCC, sizeof(discover_uuid));
		discover_params.uuid = &discover_uuid.uuid;
		discover_params.start_handle = attr->handle + 2;
		discover_params.type = BT_GATT_DISCOVER_DESCRIPTOR;
		subscribe_params.value_handle = bt_gatt_attr_value_handle(attr);

		err = bt_gatt_discover(conn, &discover_params);
		if (err) {
			LOG_DBG("Discover failed (err %d)", err);
		}
	} else {
		subscribe_params.notify = notify_func;
		subscribe_params.value = BT_GATT_CCC_NOTIFY;
		subscribe_params.ccc_handle = attr->handle;

		err = bt_gatt_subscribe(conn, &subscribe_params);
		if (err && err != -EALREADY) {
			LOG_DBG("Subscribe failed (err %d)", err);
		} else {
			LOG_DBG("[SUBSCRIBED]");
		}

		return BT_GATT_ITER_STOP;
	}

	return BT_GATT_ITER_STOP;
}

static bool eir_found(struct bt_data *data, void *user_data)
{
	bt_addr_le_t *addr = user_data;
	int i;

	LOG_DBG("[AD]: %u data_len %u", data->type, data->data_len);

	switch (data->type) {
	case BT_DATA_UUID16_SOME:
	case BT_DATA_UUID16_ALL:
		if (data->data_len % sizeof(uint16_t) != 0U) {
			LOG_DBG("AD malformed");
			return true;
		}

		for (i = 0; i < data->data_len; i += sizeof(uint16_t)) {
			struct bt_conn_le_create_param *create_param;
			struct bt_le_conn_param *param;
			const struct bt_uuid *uuid;
			uint16_t u16;
			int err;

			memcpy(&u16, &data->data[i], sizeof(u16));
			uuid = BT_UUID_DECLARE_16(sys_le16_to_cpu(u16));
			if (bt_uuid_cmp(uuid, BT_UUID_GOPRO)) {
				continue;
			}

			err = bt_le_scan_stop();
			if (err) {
				LOG_DBG("Stop LE scan failed (err %d)", err);
				continue;
			}

			LOG_DBG("Creating connection with Coded PHY support");
			param = BT_LE_CONN_PARAM_DEFAULT;
			create_param = BT_CONN_LE_CREATE_CONN;
			create_param->options |= BT_CONN_LE_OPT_CODED;
			err = bt_conn_le_create(addr, create_param, param,
						&default_conn);
			if (err) {
				LOG_DBG("Create connection with Coded PHY support failed (err %d)",
				       err);

				LOG_DBG("Creating non-Coded PHY connection");
				create_param->options &= ~BT_CONN_LE_OPT_CODED;
				err = bt_conn_le_create(addr, create_param,
							param, &default_conn);
				if (err) {
					LOG_DBG("Create connection failed (err %d)", err);
					start_scan();
				}
			}

			return false;
		}
	}

	return true;
}

static void device_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
			 struct net_buf_simple *ad)
{
	char dev[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(addr, dev, sizeof(dev));
	LOG_DBG("[DEVICE]: %s, AD evt type %u, AD data len %u, RSSI %i",
	       dev, type, ad->len, rssi);

	/* We're only interested in legacy connectable events or
	 * possible extended advertising that are connectable.
	 */
	if (type == BT_GAP_ADV_TYPE_ADV_IND ||
	    type == BT_GAP_ADV_TYPE_ADV_DIRECT_IND ||
	    type == BT_GAP_ADV_TYPE_EXT_ADV) {
		bt_data_parse(ad, eir_found, (void *)addr);
	}
}

static void start_scan(void)
{
	int err;

	/* Use active scanning and disable duplicate filtering to handle any
	 * devices that might update their advertising data at runtime. */
	struct bt_le_scan_param scan_param = {
		.type       = BT_LE_SCAN_TYPE_ACTIVE,
		.options    = BT_LE_SCAN_OPT_CODED,
		.interval   = BT_GAP_SCAN_FAST_INTERVAL,
		.window     = BT_GAP_SCAN_FAST_WINDOW,
	};

	err = bt_le_scan_start(&scan_param, device_found);
	if (err) {
		LOG_DBG("Scanning with Coded PHY support failed (err %d)", err);

		LOG_DBG("Scanning without Coded PHY");
		scan_param.options &= ~BT_LE_SCAN_OPT_CODED;
		err = bt_le_scan_start(&scan_param, device_found);
		if (err) {
			LOG_DBG("Scanning failed to start (err %d)", err);
			return;
		}
	}

	LOG_DBG("Scanning successfully started");
}

static void connected(struct bt_conn *conn, uint8_t conn_err)
{
	char addr[BT_ADDR_LE_STR_LEN];
	int err;

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (conn_err) {
		LOG_DBG("Failed to connect to %s (%u)", addr, conn_err);

		bt_conn_unref(default_conn);
		default_conn = NULL;

		start_scan();
		return;
	}

	LOG_DBG("Connected: %s", addr);

	total_rx_count = 0U;

	if (conn == default_conn) {
		memcpy(&discover_uuid, BT_UUID_GOPRO, sizeof(discover_uuid));
		discover_params.uuid = &discover_uuid.uuid;
		discover_params.func = discover_func;
		discover_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
		discover_params.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
		discover_params.type = BT_GATT_DISCOVER_PRIMARY;

		err = bt_gatt_discover(default_conn, &discover_params);
		if (err) {
			LOG_DBG("Discover failed(err %d)", err);
			return;
		}
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_DBG("Disconnected: %s (reason 0x%02x)", addr, reason);

	if (default_conn != conn) {
		return;
	}

	bt_conn_unref(default_conn);
	default_conn = NULL;

	start_scan();
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

void gopro_bluetooth_init()
{
	int err;
	err = bt_enable(NULL);

	if (err) {
		LOG_DBG("Bluetooth init failed (err %d)", err);
		return;
	}

	LOG_DBG("Bluetooth initialized");

	start_scan();
}
