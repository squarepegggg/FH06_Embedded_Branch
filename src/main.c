/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/* Solar / batteryless support:
 * USE_SOLAR_GATEKEEPER=1: duty-cycle sensing (run_policy), SPI/BMA400 power down when idle
 * USE_ADC=1: voltage gatekeeper (requires ADC devicetree), only run when V > threshold
 * Set both 0 for original battery-powered behavior. */
#define USE_SOLAR_GATEKEEPER 1
#define USE_ADC 1

/* Cycle-to-ns conversion: use Kconfig value; fallback for nRF52 @ 64 MHz */
#ifndef CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC
#define CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC 64000000
#endif

// Basic Libs
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/pm/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/sys/util.h>
#include "bma400.h"
#include "glueV3.h"
#include <string.h>
#include "bma400_defs.h"


#if USE_ADC
#include <zephyr/drivers/adc.h>
#endif

//BLE STUFF
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/gap.h>


//////////////////////////////////////////////////////////////////////////
//																		//
//						All BLE/Android functionality					//
//																		//
//////////////////////////////////////////////////////////////////////////

// BLE STUFF
#define DEVICE_NAME       CONFIG_BT_DEVICE_NAME	// Name of Device
#define DEVICE_NAME_LEN   (sizeof(DEVICE_NAME) - 1)	// Length of device
#define BT_UUID_ACCEL_SERVICE_VAL \
	BT_UUID_128_ENCODE(0x12345678,0x1234,0x5678,0x1234,0x1234567890ab)	// ID of device

#define BT_UUID_ACCEL_CHAR_VAL \
	BT_UUID_128_ENCODE(0x12345679,0x1234,0x5678,0x1234,0x1234567890ab)	// how many chars of ID
static struct bt_uuid_128 accel_service_uuid = BT_UUID_INIT_128(BT_UUID_ACCEL_SERVICE_VAL);	// ID
static struct bt_uuid_128 accel_char_uuid    = BT_UUID_INIT_128(BT_UUID_ACCEL_CHAR_VAL);	// Length of DI
static struct bt_conn *current_conn;	// current connection ptr
/* label(1) + x(2) + y(2) + z(2) + infer_us(4) + arena(2) + model_bytes(4) + voltage_mv(2) = 19 */
#define ACCEL_PAYLOAD_SIZE 19
static uint8_t accel_value[ACCEL_PAYLOAD_SIZE] = {0};
#define VOLTAGE_NO_DATA 0xFFFFU
#define LATENCY_SENTINEL 0xFFFFFFFFU
// Func for Notifying External Device
static void accel_ccc_cfg_changed(const struct bt_gatt_attr *attr,uint16_t value){
}
// Initialization Constructor
BT_GATT_SERVICE_DEFINE(accel_svc,
	BT_GATT_PRIMARY_SERVICE(&accel_service_uuid),
	BT_GATT_CHARACTERISTIC(&accel_char_uuid.uuid,
			       BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_NONE,
			       NULL, NULL, accel_value),
	BT_GATT_CCC(accel_ccc_cfg_changed,
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE)
);

// Forward declaration
static void request_fast_ble_interval(void);

// BLE HELPER FUNCTIONS
static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		return;
	}
	current_conn = bt_conn_ref(conn);
	/* Kick a fast connection interval so BLE notifications arrive in real-time */
	request_fast_ble_interval();
}
static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	if (current_conn) {
		bt_conn_unref(current_conn);
		current_conn = NULL;
	}
}
BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

// called from main to see if BT is ready
static void bt_ready(int err)
{
	if (err) {
		return;
	}
	err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, ad, ARRAY_SIZE(ad),
			      NULL, 0);
}

static void send_prediction_accel_notification(uint8_t label, int16_t x, int16_t y, int16_t z,
					       uint32_t inference_us, uint16_t arena_bytes,
					       uint32_t model_bytes, uint16_t voltage_mv){
	if (!current_conn) return;
	accel_value[0] = label;
	accel_value[1] = x & 0xFF;
	accel_value[2] = (x >> 8) & 0xFF;
	accel_value[3] = y & 0xFF;
	accel_value[4] = (y >> 8) & 0xFF;
	accel_value[5] = z & 0xFF;
	accel_value[6] = (z >> 8) & 0xFF;
	accel_value[7] = inference_us & 0xFF;
	accel_value[8] = (inference_us >> 8) & 0xFF;
	accel_value[9] = (inference_us >> 16) & 0xFF;
	accel_value[10] = (inference_us >> 24) & 0xFF;
	accel_value[11] = arena_bytes & 0xFF;
	accel_value[12] = (arena_bytes >> 8) & 0xFF;
	accel_value[13] = model_bytes & 0xFF;
	accel_value[14] = (model_bytes >> 8) & 0xFF;
	accel_value[15] = (model_bytes >> 16) & 0xFF;
	accel_value[16] = (model_bytes >> 24) & 0xFF;
	accel_value[17] = voltage_mv & 0xFF;
	accel_value[18] = (voltage_mv >> 8) & 0xFF;
	bt_gatt_notify(current_conn, &accel_svc.attrs[1],
		       accel_value, sizeof(accel_value));
}

/* Request a balanced BLE connection interval: responsive enough for
 * near-real-time data while keeping the radio duty cycle low. */
static void request_fast_ble_interval(void)
{
	if (!current_conn) return;
	/* min 30ms, max 50ms, latency 0, timeout 4s */
	static const struct bt_le_conn_param fast_params =
		BT_LE_CONN_PARAM_INIT(24, 40, 0, 400);
	bt_conn_le_param_update(current_conn, &fast_params);
}

/* Cached ML result so accel-only updates still carry the latest label */
static uint8_t cached_label = 0xFF;

LOG_MODULE_REGISTER(app, LOG_LEVEL_WRN);



//////////////////////////////////////////////////////////////////////////
//																		//
//						Embedded functionality for sensor				//
//																		//
//////////////////////////////////////////////////////////////////////////
// threads
#define STACKSIZE 2048
#define THREAD_READ_BMA_PRIORITY 7
K_SEM_DEFINE(bma400_ready, 0, 1);

// SPI
#define SPIOP	SPI_WORD_SET(8) | SPI_TRANSFER_MSB
struct spi_dt_spec spispec = SPI_DT_SPEC_GET(DT_NODELABEL(bma400), SPIOP, 0);
uint8_t rx_buffer[128] = {0};

// interrupt GPIO
#define int_NODE DT_ALIAS(int1)
static const struct gpio_dt_spec int_pin = GPIO_DT_SPEC_GET(int_NODE, gpios);
static struct gpio_callback int_cb_data;

// BMA400
#define FIFO_SAMPLES 25
#define FIFO_BATCH  5
#define FIFO_WATERMARK_LEVEL    UINT16_C(FIFO_BATCH*7)
#define FIFO_FULL_SIZE          UINT16_C(1024)
#define FIFO_SIZE               (FIFO_FULL_SIZE + BMA400_FIFO_BYTES_OVERREAD)

BMA400_INTF_RET_TYPE read_reg_spi(uint8_t reg_address, uint8_t* data, uint32_t len, void* intf_ptr);
BMA400_INTF_RET_TYPE write_reg_spi(uint8_t reg_address, const uint8_t* data, uint32_t len, void* intf_ptr);
void bma400_delay_us(uint32_t period, void *intf_ptr) {
	k_usleep(period);
}

static uint8_t              dev_addr    = 31;
struct bma400_dev           bma_sensor         = {
        .intf = BMA400_SPI_INTF,
        .intf_ptr = &dev_addr,
        .read = read_reg_spi,
        .write = write_reg_spi,
        .delay_us = bma400_delay_us,
        .read_write_len = 8
};

struct bma400_int_enable int_en;
struct bma400_fifo_data fifo_frame;
struct bma400_device_conf fifo_conf;
struct bma400_sensor_conf conf;
uint8_t fifo_buff[FIFO_SIZE] = { 0 };
struct bma400_fifo_sensor_data accel_data[FIFO_SAMPLES] = { { 0 } };

#if USE_SOLAR_GATEKEEPER
K_SEM_DEFINE(run_policy, 0, 1);
volatile bool last_tx_done = true;
#define THREAD_RUN_POLICY_PRIORITY 8
#endif

#if USE_ADC
static int16_t adc_buf;
static struct adc_sequence adc_seq;
static const struct adc_dt_spec adc_ch = ADC_DT_SPEC_GET(DT_PATH(zephyr_user));
#define VOLTAGE_THRESHOLD_MV 1600

static uint16_t get_voltage_mv(void)
{
	int val_mv;
	int err = adc_read(adc_ch.dev, &adc_seq);
	if (err < 0) return VOLTAGE_NO_DATA;
	val_mv = (int)adc_buf;
	err = adc_raw_to_millivolts_dt(&adc_ch, &val_mv);
	if (err < 0) return VOLTAGE_NO_DATA;
	if (val_mv < 0) return 0;
	if (val_mv > 65535) return 65535;
	return (uint16_t)val_mv;
}
#else
static uint16_t get_voltage_mv(void) { return VOLTAGE_NO_DATA; }
#endif

void bma_int_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	k_sem_give(&bma400_ready);
}

// for reading every 25 samples from a buffer
void thread_read_bma400(void)
{
	/* Ring buffer accumulates FIFO_BATCH-sized chunks into a full
	 * FIFO_SAMPLES (25) window for ML inference.  BLE accel data
	 * is sent on every batch for near-real-time display (~5Hz). */
	struct bma400_fifo_sensor_data ml_window[FIFO_SAMPLES];
	int ml_idx = 0;   /* how many samples in ml_window so far */

	while (1) {

		k_sem_take(&bma400_ready, K_FOREVER);

#if USE_SOLAR_GATEKEEPER
		const struct device *spi_dev = DEVICE_DT_GET(DT_NODELABEL(spi1));
		pm_device_action_run(spi_dev, PM_DEVICE_ACTION_RESUME);
#endif

		/* ── 1. Read whatever is in the FIFO ── */
		bma400_get_fifo_data(&fifo_frame, &bma_sensor);
		uint16_t got = FIFO_SAMPLES;          /* max we can parse */
		bma400_extract_accel(&fifo_frame, accel_data, &got, &bma_sensor);

		uint16_t batch_voltage = get_voltage_mv();

		/* ── 2. Send each new sample via BLE immediately ── */
		for (int i = 0; i < got; i++) {
			send_prediction_accel_notification(cached_label, accel_data[i].x, accel_data[i].y,
							accel_data[i].z, LATENCY_SENTINEL,
							(uint16_t)ei_model_arena_size,
							ei_model_tflite_len,
							batch_voltage);

			if (ml_idx < FIFO_SAMPLES) {
				demo_data[ml_idx] = accel_data[i].x;
				demo_data[ml_idx + 50] = accel_data[i].y;
				demo_data[ml_idx + 25] = accel_data[i].z;
				ml_idx++;
			} else {
				memmove(&demo_data[0], &demo_data[1], 24 * sizeof(float));
				demo_data[24] = (float)accel_data[i].x;

				memmove(&demo_data[25], &demo_data[26], 24 * sizeof(float));
				demo_data[49] = (float)accel_data[i].y;

				memmove(&demo_data[50], &demo_data[51], 24 * sizeof(float));
				demo_data[74] = (float)accel_data[i].z;

				memmove(&ml_window[0], &ml_window[1], 24 * sizeof(struct bma400_fifo_sensor_data));
				ml_window[24] = accel_data[i];
			}

			/* ── 3. Run ML inference once we have a full 25-sample window ── */
			if (ml_idx >= FIFO_SAMPLES) {
				const char *predictedLabel = NULL;
				float predictedScore = 0.0f;

				uint32_t start_cyc = k_cycle_get_32();
				int inferenceResult = ei_v3_classify_test(&predictedLabel, &predictedScore);
				uint32_t end_cyc = k_cycle_get_32();
				uint32_t delta_cyc = end_cyc - start_cyc;
				uint32_t latency_us = (uint32_t)((uint64_t)delta_cyc * 1000000ULL /
					CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC);

				if (inferenceResult == 0 && predictedLabel != NULL) {
					uint8_t result_to_send = 0xFF;
					if      (strcmp(predictedLabel, "downstairs") == 0) result_to_send = 0;
					else if (strcmp(predictedLabel, "jump") == 0)       result_to_send = 1;
					else if (strcmp(predictedLabel, "running") == 0)    result_to_send = 2;
					else if (strcmp(predictedLabel, "sitting") == 0)    result_to_send = 3;
					else if (strcmp(predictedLabel, "standing") == 0)   result_to_send = 4;
					else if (strcmp(predictedLabel, "upstairs") == 0)   result_to_send = 5;
					/* Send one notification with the fresh ML label */
					else if (strcmp(predictedLabel, "walking") == 0)    result_to_send = 6;

					cached_label = result_to_send;

					int16_t lx = ml_window[FIFO_SAMPLES - 1].x;
					int16_t ly = ml_window[FIFO_SAMPLES - 1].y;
					int16_t lz = ml_window[FIFO_SAMPLES - 1].z;
					send_prediction_accel_notification(result_to_send, lx, ly, lz,
								latency_us,
								(uint16_t)ei_model_arena_size,
								ei_model_tflite_len,
								batch_voltage);
				}
			}
		}

#if USE_SOLAR_GATEKEEPER
		/* ── 4a. Solar: power down BMA400 + SPI, gate re-arm via run_policy ── */
		int_en.type = BMA400_FIFO_WM_INT_EN;
		int_en.conf = BMA400_DISABLE;
		bma400_enable_interrupt(&int_en, 1, &bma_sensor);
		bma400_set_power_mode(BMA400_MODE_LOW_POWER, &bma_sensor);
		pm_device_action_run(spi_dev, PM_DEVICE_ACTION_SUSPEND);
		last_tx_done = true;
#else
		/* ── 4b. Battery: re-arm sensor immediately ── */
		bma400_set_power_mode(BMA400_MODE_NORMAL, &bma_sensor);
		int_en.conf = BMA400_ENABLE;
		bma400_enable_interrupt(&int_en, 1, &bma_sensor);
#endif
	}

}


// Need to make sure stack is big enough to run NN code
K_THREAD_DEFINE(thread_read_bma400_id, STACKSIZE*4, thread_read_bma400, NULL, NULL, NULL, THREAD_READ_BMA_PRIORITY, 0, 0);

#if USE_SOLAR_GATEKEEPER
static void timer_run_policy_handler(struct k_timer *dummy)
{
	k_sem_give(&run_policy);
}

void thread_run_policy(void)
{
	while (1) {
		k_sem_take(&run_policy, K_FOREVER);
#if USE_ADC
		{
			int val_mv;
			int err = adc_read(adc_ch.dev, &adc_seq);
			if (err < 0) {
				LOG_ERR("ADC read failed (%d)", err);
				continue;
			}
			val_mv = (int)adc_buf;
			err = adc_raw_to_millivolts_dt(&adc_ch, &val_mv);
			if (err < 0) continue;
			if (val_mv < VOLTAGE_THRESHOLD_MV || !last_tx_done)
				continue;
		}
#else
		if (!last_tx_done)
			continue;
#endif
		{
			const struct device *spi_dev = DEVICE_DT_GET(DT_NODELABEL(spi1));
			pm_device_action_run(spi_dev, PM_DEVICE_ACTION_RESUME);
			int_en.type = BMA400_FIFO_WM_INT_EN;
			int_en.conf = BMA400_ENABLE;
			bma400_set_power_mode(BMA400_MODE_NORMAL, &bma_sensor);
			bma400_enable_interrupt(&int_en, 1, &bma_sensor);
			last_tx_done = false;
			pm_device_action_run(spi_dev, PM_DEVICE_ACTION_SUSPEND);
		}
	}
}
K_THREAD_DEFINE(thread_run_policy_id, STACKSIZE, thread_run_policy, NULL, NULL, NULL, THREAD_RUN_POLICY_PRIORITY, 0, 0);
K_TIMER_DEFINE(timer_run_policy, timer_run_policy_handler, NULL);
#endif
// Function for SPI Read
BMA400_INTF_RET_TYPE read_reg_spi(uint8_t reg_address, uint8_t* data, uint32_t len, void* intf_ptr)
{
	int err;

	uint8_t tx_buffer = reg_address;
	struct spi_buf tx_spi_buf		= {.buf = (void *)&tx_buffer, .len = 1};
	struct spi_buf_set tx_spi_buf_set 	= {.buffers = &tx_spi_buf, .count = 1};
	struct spi_buf rx_spi_bufs 		= {.buf = rx_buffer, .len = len+1};
	struct spi_buf_set rx_spi_buf_set	= {.buffers = &rx_spi_bufs, .count = 1};

	err = spi_transceive_dt(&spispec, &tx_spi_buf_set, &rx_spi_buf_set);
	if (err < 0) {
		LOG_ERR("spi_transceive_dt() failed, err: %d, 0x%02X", err,tx_buffer);
		// return err;
	}

	for(int i = 0; i < len; i++) {
		data[i] = rx_buffer[i+1];
	}

	return 0;
}
// Func for SPI Write
BMA400_INTF_RET_TYPE write_reg_spi(uint8_t reg_address, const uint8_t* data, uint32_t len, void* intf_ptr)
{
	int err;

	uint8_t tx_buf[2] = {reg_address, data[0]};
	struct spi_buf	tx_spi_buf 		= {.buf = tx_buf, .len = len+1};
	struct spi_buf_set tx_spi_buf_set	= {.buffers = &tx_spi_buf, .count = 1};

	err = spi_write_dt(&spispec, &tx_spi_buf_set);
	if (err < 0) {
		LOG_ERR("spi_write_dt() failed, err %d", err);
		return err;
	}

	return 0;
}

// Init Functions
// for FIFO Buffer Reads
void init_fifo_watermark()
{
	conf.type = BMA400_ACCEL;
	int8_t rslt = bma400_get_sensor_conf(&conf, 1, &bma_sensor);

	conf.param.accel.odr = BMA400_ODR_25HZ;
	conf.param.accel.range = BMA400_RANGE_4G;
	conf.param.accel.data_src = BMA400_DATA_SRC_ACCEL_FILT_1;

	rslt = bma400_set_sensor_conf(&conf, 1, &bma_sensor);

	fifo_conf.type = BMA400_FIFO_CONF;

	rslt = bma400_get_device_conf(&fifo_conf, 1, &bma_sensor);

	fifo_conf.param.fifo_conf.conf_regs = BMA400_FIFO_X_EN 
										| BMA400_FIFO_Y_EN 
										| BMA400_FIFO_Z_EN	
										| BMA400_FIFO_AUTO_FLUSH;   // flush on power mode change (12-bit mode for full resolution)
	fifo_conf.param.fifo_conf.conf_status = BMA400_ENABLE;
	fifo_conf.param.fifo_conf.fifo_watermark = 1;
	fifo_conf.param.fifo_conf.fifo_wm_channel = BMA400_INT_CHANNEL_1;

	rslt = bma400_set_device_conf(&fifo_conf, 1, &bma_sensor);

	fifo_frame.data = fifo_buff;
	fifo_frame.length = FIFO_SIZE;

	int_en.type = BMA400_FIFO_WM_INT_EN;
#if USE_SOLAR_GATEKEEPER
	int_en.conf = BMA400_DISABLE;
	bma400_set_power_mode(BMA400_MODE_LOW_POWER, &bma_sensor);
	bma400_enable_interrupt(&int_en, 1, &bma_sensor);
#else
	int_en.conf = BMA400_ENABLE;
	bma400_set_power_mode(BMA400_MODE_NORMAL, &bma_sensor);
	rslt = bma400_enable_interrupt(&int_en, 1, &bma_sensor);
#endif
}

int main(void)
{
	int err;
	
	err = spi_is_ready_dt(&spispec);
	if (!err) {
		LOG_ERR("Error: SPI device is not ready, err: %d", err);
		return 0;
	}

	if (!device_is_ready(int_pin.port)) {
		LOG_ERR("Device not Ready");
		return -1;
	}

	err = gpio_pin_configure_dt(&int_pin, GPIO_INPUT);
	if (err < 0) {
		LOG_ERR("Error: GPIO device is not ready, err: %d", err);
		return -1;
	}
#if USE_ADC
	adc_seq.buffer = &adc_buf;
	adc_seq.buffer_size = sizeof(adc_buf);
	if (!adc_is_ready_dt(&adc_ch)) {
		LOG_ERR("ADC not ready");
		return -1;
	}
	err = adc_channel_setup_dt(&adc_ch);
	if (err < 0) {
		LOG_ERR("ADC channel setup failed (%d)", err);
		return -1;
	}
	err = adc_sequence_init_dt(&adc_ch, &adc_seq);
	if (err < 0) {
		LOG_ERR("ADC sequence init failed (%d)", err);
		return -1;
	}
#endif

	err = bt_enable(bt_ready);
	if(err){
		return -1;
	}

	err = gpio_pin_interrupt_configure_dt(&int_pin, GPIO_INT_EDGE_RISING);

	gpio_init_callback(&int_cb_data, bma_int_handler, BIT(int_pin.pin));
	gpio_add_callback(int_pin.port, &int_cb_data);

	bma400_init(&bma_sensor);
	init_fifo_watermark();

#if USE_SOLAR_GATEKEEPER
	const struct device *spi_dev = DEVICE_DT_GET(DT_NODELABEL(spi1));
	pm_device_action_run(spi_dev, PM_DEVICE_ACTION_SUSPEND);
	k_timer_start(&timer_run_policy, K_MSEC(500), K_MSEC(500));
#else
	/* If the interrupt pin is already high after init, kick the semaphore manually
	 * This handles the case where BMA400 has stale FIFO data from a previous session
	 * (chip-erase only erases the nRF, not the BMA400) */
	if (gpio_pin_get_dt(&int_pin)) {
		k_sem_give(&bma400_ready);
	}
#endif
	

	while(1){
		k_sleep(K_FOREVER);
	}

	return 0;
}
