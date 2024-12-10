#pragma once

#include <zephyr/drivers/spi.h>
#include "lsm6dsv16bx_reg.h"

#define LSM6DSV16BX_FSM_ALG_MAX_NB 8

/* Define the structure needed for FSM/MLC */
#ifndef MEMS_UCF_SHARED_TYPES
#define MEMS_UCF_SHARED_TYPES

/** Common data block definition **/
typedef struct {
  uint8_t address;
  uint8_t data;
} ucf_line_t;

#endif /* MEMS_UCF_SHARED_TYPES */

#define BOOT_TIME 10 //ms
#define FIFO_WATERMARK 200

typedef struct {
	bool gbias_enabled;
	bool game_rot_enabled;
	bool gravity_enabled;
} lsm6dsv16bx_sflp_state_t;

typedef enum {
	LSM6DSV16BX_CALIBRATION_NOT_CALIBRATING,
	LSM6DSV16BX_CALIBRATION_SETTLING,
	LSM6DSV16BX_CALIBRATION_RECORDING,
} lsm6dsv16bx_calib_state_t;

typedef struct {
	bool xl_enabled;
	bool gy_enabled;
	bool qvar_enabled;
	lsm6dsv16bx_sflp_state_t sflp_state;
	bool sigmot_enabled;
	bool fsm_enabled;
	lsm6dsv16bx_calib_state_t calib;
	bool int2_on_int1;
} lsm6dsv16bx_state_t;

typedef struct {
	void (*lsm6dsv16bx_ts_sample_cb)(float_t);
	void (*lsm6dsv16bx_acc_sample_cb)(float_t, float_t, float_t);
	void (*lsm6dsv16bx_gyro_sample_cb)(float_t, float_t, float_t);
	void (*lsm6dsv16bx_qvar_sample_cb)(float_t);
	void (*lsm6dsv16bx_gbias_sample_cb)(float_t, float_t, float_t);
	void (*lsm6dsv16bx_game_rot_sample_cb)(float_t, float_t, float_t, float_t);
	void (*lsm6dsv16bx_gravity_sample_cb)(float_t, float_t, float_t);
	void (*lsm6dsv16bx_calibration_result_cb)(int, float_t, float_t, float_t);
	void (*lsm6dsv16bx_sigmot_cb)();
	void (*lsm6dsv16bx_fsm_cbs[LSM6DSV16BX_FSM_ALG_MAX_NB])(uint8_t);
} lsm6dsv16bx_cb_t;

typedef struct {
	const ucf_line_t* fsm_ucf_cfg[LSM6DSV16BX_FSM_ALG_MAX_NB];
	uint32_t fsm_ucf_cfg_size[LSM6DSV16BX_FSM_ALG_MAX_NB];
	int (*fsm_pre_cfg_cbs[LSM6DSV16BX_FSM_ALG_MAX_NB])(stmdev_ctx_t);
} lsm6dsv16bx_fsm_cfg_t;

typedef struct {
	lsm6dsv16bx_xl_full_scale_t xl_scale;
	float_t (*xl_conversion_function)(int16_t);
	lsm6dsv16bx_gy_full_scale_t gy_scale;
	float_t (*gy_conversion_function)(int16_t);
} lsm6dsv16bx_scale_t;

typedef struct {
	stmdev_ctx_t dev_ctx;
	uint8_t whoamI;
	lsm6dsv16bx_cb_t callbacks;
	lsm6dsv16bx_state_t state;
	uint8_t nb_samples_to_discard;
	lsm6dsv16bx_fsm_cfg_t fsm_configs;
	lsm6dsv16bx_scale_t scale;
} lsm6dsv16bx_sensor_t;

void lsm6dsv16bx_init(lsm6dsv16bx_cb_t cb, lsm6dsv16bx_fsm_cfg_t fsm_cfg);
void lsm6dsv16bx_int1_irq(struct k_work *item);
int lsm6dsv16bx_start_acquisition(bool enable_gbias, bool enable_sflp, bool enable_qvar);
int lsm6dsv16bx_reset();
int lsm6dsv16bx_start_calibration();
int lsm6dsv16bx_start_significant_motion_detection();
int lsm6dsv16bx_start_fsm(uint8_t* fsm_alg_nb, uint8_t n);
void lsm6dsv16bx_set_gbias(float x, float y, float z);
