/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#if CONFIG_USB_MASS_STORAGE
#include <usb_mass_storage/usb_mass_storage.h>
#endif
#include <state_machine/state_machine.h>

#include <app_version.h>

#include <app/lib/lsm6dsv16x.h>
#include <app/lib/xiao_smp_bluetooth.h>

LOG_MODULE_REGISTER(main, CONFIG_APP_LOG_LEVEL);

struct line {
	float_t acc_x;
	float_t acc_y;
	float_t acc_z;
	bool acc_updated;
	float_t gyro_x;
	float_t gyro_y;
	float_t gyro_z;
	bool gyro_updated;
	float_t ts;
	bool ts_updated;
	float_t gbias_x;
	float_t gbias_y;
	float_t gbias_z;
	bool gbias_updated;
	float_t gravity_x;
	float_t gravity_y;
	float_t gravity_z;
	bool gravity_updated;
	float_t game_rot_x;
	float_t game_rot_y;
	float_t game_rot_z;
	float_t game_rot_w;
	bool game_rot_updated;
};

static struct line l = {
	.acc_updated = false,
	.gyro_updated = false,
	.ts_updated = false,
	.gbias_updated = false,
	.gravity_updated = false,
	.game_rot_updated = false,
};

static void print_line_if_needed(){
	char txt[100];
	if (l.acc_updated && l.gyro_updated && l.ts_updated) {
		l.acc_updated = false;
		l.gyro_updated = false;
		l.ts_updated = false;

		sprintf(txt, "%.3f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f\n", (double)l.ts, (double)l.acc_x, (double)l.acc_y, (double)l.acc_z, (double)l.gyro_x, (double)l.gyro_y, (double)l.gyro_z);
		int res = usb_mass_storage_write_to_current_session(txt, strlen(txt));

		if (res < 0) {
			LOG_ERR("Unable to write to session file, ending session");
			state_machine_post_event(XIAO_EVENT_STOP_RECORDING);
		}
	}
}

static void acc_received_cb(float_t x, float_t y, float_t z)
{
	l.acc_x = x;
	l.acc_y = y;
	l.acc_z = z;
	l.acc_updated = true;

	print_line_if_needed();
	return;
}

static void gyro_received_cb(float_t x, float_t y, float_t z)
{
	l.gyro_x = x;
	l.gyro_y = y;
	l.gyro_z = z;
	l.gyro_updated = true;

	print_line_if_needed();
	return;
}

static void ts_received_cb(float_t ts)
{
	l.ts = ts / 1000000; // Convert ns to ms.
	l.ts_updated = true;

	print_line_if_needed();
	return;
}

static void gbias_received_cb(float_t x, float_t y, float_t z)
{
	l.gbias_x = x;
	l.gbias_y = y;
	l.gbias_z = z;
	l.gbias_updated = true;
	LOG_DBG("Received gyroscope bias: x=%f, y=%f, z=%f", (double)x, (double)y, (double)z);
}

static void gravity_received_cb(float_t x, float_t y, float_t z)
{
	l.gravity_x = x;
	l.gravity_y = y;
	l.gravity_z = z;
	l.gravity_updated = true;
	LOG_DBG("Received gravity: x=%f, y=%f, z=%f", (double)x, (double)y, (double)z);
}

static void game_rot_received_cb(float_t x, float_t y, float_t z, float_t w)
{
	l.game_rot_x = x;
	l.game_rot_y = y;
	l.game_rot_z = z;
	l.game_rot_w = w;
	l.game_rot_updated = true;
	LOG_DBG("Received game rotation: x=%f, y=%f, z=%f, w=%f", (double)x, (double)y, (double)z, (double)w);
}

static void calib_res_cb(float_t x, float_t y, float_t z)
{
	LOG_INF("Calibration succeeded. Gbias x: %f y: %f z: %f", (double)x, (double)y, (double)z);
	int res = usb_mass_storage_create_file(NULL, CALIBRATION_FILE_NAME, usb_mass_storage_get_calibration_file_p(), true);
	if (res != 0)
	{
		LOG_ERR("Error creating calibration file (%i)", res);
	} else {
		char txt[CALIBRATION_FILE_SIZE];
		sprintf(txt, "x:%+3.2f\ny:%+3.2f\nz:%+3.2f", (double)x, (double)y, (double)z);
		res = usb_mass_storage_write_to_file(txt, strlen(txt), usb_mass_storage_get_calibration_file_p(), true);
		if (res)
		{
			LOG_ERR("Failed to write to cal file (%i)", res);
		}
		res = usb_mass_storage_close_file(usb_mass_storage_get_calibration_file_p());
		if (res)
		{
			LOG_ERR("Failed to close cal file (%i)", res);
		}
	}

	lsm6dsv16x_set_gbias(x, y, z);

	state_machine_post_event(XIAO_EVENT_STOP_CALIBRATION);
}

int main(void)
{
	int ret;

	LOG_INF("Xiao LSM6DSV16X Evaluation %s", APP_VERSION_STRING);

	lsm6dsv16x_cb_t callbacks = {
		.lsm6dsv16x_ts_sample_cb = ts_received_cb,
		.lsm6dsv16x_acc_sample_cb = acc_received_cb,
		.lsm6dsv16x_gyro_sample_cb = gyro_received_cb,
		.lsm6dsv16x_gbias_sample_cb = gbias_received_cb,
		.lsm6dsv16x_gravity_sample_cb = gravity_received_cb,
		.lsm6dsv16x_game_rot_sample_cb = game_rot_received_cb,
		.lsm6dsv16x_calibration_result_cb = calib_res_cb,
	};

	lsm6dsv16x_init(callbacks);

	xiao_state_t starting_state = IDLE;

#if CONFIG_USB_MASS_STORAGE
	ret = usb_mass_storage_init();

	if (ret) {
		LOG_ERR("The device could not be put in USB mass storage mode.");
		return 0;
	}

	LOG_INF("The device is put in USB mass storage mode.");

	float x, y, z;
	ret = usb_mass_storage_check_calibration_file_contents(&x, &y, &z);
	if (ret == -ENOENT) {
		// No calibration file present, or it has the wrong size, trigger calibration.
		LOG_WRN("Correct calibration file not found, triggering calibration");
		starting_state = CALIBRATING;
	} else if (ret != 0) {
		// Something else went wrong when reading calibration file
		LOG_ERR("Failed to check calibration file (%i)", ret);
	} else {
		// Calibration file has been read properly.
		//LOG_DBG("x: %+3.2f - y: %+3.2f - z: %+3.2f", (double)x, (double)y, (double)z);
		lsm6dsv16x_set_gbias(x, y, z);
	}

#endif

	start_smp_bluetooth_adverts();

	state_machine_init(starting_state);

	return state_machine_run();
}
