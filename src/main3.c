/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/* Solar / batteryless support:
 * USE_SOLAR_GATEKEEPER=1: duty-cycle sensing (run_policy), SPI/BMA400 power down when idle
 * USE_ADC=1: voltage gatekeeper (requires ADC devicetree), only run when V > threshold
 * USE_SLIDING_WINDOW=1: small FIFO chunks + ring buffer for fast sliding-window predictions (~5 Hz).
 * Set both 0 for original battery-powered behavior. */
#define USE_SOLAR_GATEKEEPER 1
#define USE_ADC 1
#define USE_SLIDING_WINDOW 1

#if USE_SOLAR_GATEKEEPER
/* Run-policy timer period (ms). Longer = fewer wake-ups, lower power; 1000 = 1 s. */
#define SOLAR_RUN_POLICY_MS 50
#endif

/* Cycle-to-ns conversion: use Kconfig value; fallback for nRF52 @ 64 MHz */
#ifndef CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC
#define CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC 64000000
#endif

/* Energy profiling: set to 1 to measure time/energy per region and print to RTT.
 * Build with CONFIG_LOG=y (e.g. use prj_energy_profile.conf overlay) to see output. */
#define ENERGY_PROFILE 0

#if ENERGY_PROFILE
#define ENERGY_PROFILE_INTERVAL 20
#define CYCLES_TO_US(cyc) ((uint32_t)((uint64_t)(cyc) * 1000000ULL / CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC))
#define ENERGY_UJ(v_mv, i_ma, us) ((uint32_t)((uint64_t)(v_mv) * (i_ma) * (us) / 1000U))
#define I_INFERENCE_MA 5
#define I_BLE_MA 10
#define I_SPI_BMA_MA 4
#define I_ADC_MA 2
#define I_DATA_PREP_MA 5
#define VOLTAGE_DEFAULT_MV 3300
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

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/addr.h>

//////////////////////////////////////////////////////////////////////////
//																		//
//				Advertising-only BLE (no connection / GATT)				//
//																		//
//////////////////////////////////////////////////////////////////////////

#define COMPANY_ID_CODE 0x0059

typedef struct __attribute__((packed)) adv_mfg_data {
	uint16_t company_code;
	uint8_t pred;
	int16_t x;
	int16_t y;
	int16_t z;
	uint16_t voltage_mv;
} adv_mfg_data_type;

static adv_mfg_data_type adv_mfg_data = {
	.company_code = COMPANY_ID_CODE,
	.pred = 0x0,
	.x = 0,
	.y = 0,
	.z = 0,
	.voltage_mv = 0xFFFFU
};

static const struct bt_le_adv_param *adv_param =
	BT_LE_ADV_PARAM(BT_LE_ADV_OPT_USE_IDENTITY | BT_LE_ADV_OPT_SCANNABLE,
			32, 33, NULL);

#define ADV_DEVICE_NAME "AccelDev"

static const struct bt_data ad[] = {
	BT_DATA(BT_DATA_MANUFACTURER_DATA, (unsigned char *)&adv_mfg_data,
		sizeof(adv_mfg_data)),
};

static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, ADV_DEVICE_NAME, sizeof(ADV_DEVICE_NAME) - 1),
};

#define VOLTAGE_NO_DATA 0xFFFFU

static uint16_t cached_voltage_mv = VOLTAGE_NO_DATA;

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

#if ENERGY_PROFILE
static uint64_t total_us_inference;
static uint64_t total_us_ble;
static uint64_t total_us_spi_bma;
static uint64_t total_us_adc;
static uint64_t total_us_data_prep;
static uint32_t calls_inference;
static uint32_t calls_ble;
static uint32_t calls_spi_bma;
static uint32_t calls_adc;
static uint32_t calls_data_prep;
#endif

#if ENERGY_PROFILE
static void energy_profile_print_and_reset(uint16_t voltage_mv)
{
	uint32_t v = (voltage_mv == VOLTAGE_NO_DATA) ? VOLTAGE_DEFAULT_MV : voltage_mv;
	if (calls_inference > 0) {
		uint32_t avg_us = (uint32_t)(total_us_inference / calls_inference);
		uint32_t uj = ENERGY_UJ(v, I_INFERENCE_MA, total_us_inference);
		LOG_INF("ENERGY_PROFILE: inference total_us=%u calls=%u avg_us=%u total_uj=%u",
			(unsigned)total_us_inference, calls_inference, avg_us, uj);
	}
	if (calls_ble > 0) {
		uint32_t avg_us = (uint32_t)(total_us_ble / calls_ble);
		uint32_t uj = ENERGY_UJ(v, I_BLE_MA, total_us_ble);
		LOG_INF("ENERGY_PROFILE: ble_notify total_us=%u calls=%u avg_us=%u total_uj=%u",
			(unsigned)total_us_ble, calls_ble, avg_us, uj);
	}
	if (calls_spi_bma > 0) {
		uint32_t avg_us = (uint32_t)(total_us_spi_bma / calls_spi_bma);
		uint32_t uj = ENERGY_UJ(v, I_SPI_BMA_MA, total_us_spi_bma);
		LOG_INF("ENERGY_PROFILE: spi_bma total_us=%u calls=%u avg_us=%u total_uj=%u",
			(unsigned)total_us_spi_bma, calls_spi_bma, avg_us, uj);
	}
	if (calls_adc > 0) {
		uint32_t avg_us = (uint32_t)(total_us_adc / calls_adc);
		uint32_t uj = ENERGY_UJ(v, I_ADC_MA, total_us_adc);
		LOG_INF("ENERGY_PROFILE: adc total_us=%u calls=%u avg_us=%u total_uj=%u",
			(unsigned)total_us_adc, calls_adc, avg_us, uj);
	}
	if (calls_data_prep > 0) {
		uint32_t avg_us = (uint32_t)(total_us_data_prep / calls_data_prep);
		uint32_t uj = ENERGY_UJ(v, I_DATA_PREP_MA, total_us_data_prep);
		LOG_INF("ENERGY_PROFILE: data_prep total_us=%u calls=%u avg_us=%u total_uj=%u",
			(unsigned)total_us_data_prep, calls_data_prep, avg_us, uj);
	}
	total_us_inference = total_us_ble = total_us_spi_bma = total_us_adc = total_us_data_prep = 0;
	calls_inference = calls_ble = calls_spi_bma = calls_adc = calls_data_prep = 0;
}
#endif



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
#if USE_SLIDING_WINDOW
#define FIFO_BATCH  5
#else
#define FIFO_BATCH  25
#endif
#define FIFO_WATERMARK_LEVEL    UINT16_C(FIFO_BATCH*4)
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

#if USE_SLIDING_WINDOW
static int16_t ring_x[FIFO_SAMPLES];
static int16_t ring_y[FIFO_SAMPLES];
static int16_t ring_z[FIFO_SAMPLES];
static int ring_count;
#endif

#if USE_SOLAR_GATEKEEPER
K_SEM_DEFINE(run_policy, 0, 1);
volatile bool last_tx_done = true;
#define THREAD_RUN_POLICY_PRIORITY 8
#endif

#if USE_ADC
static int16_t adc_buf;
static struct adc_sequence adc_seq;
static const struct adc_dt_spec adc_ch = ADC_DT_SPEC_GET(DT_PATH(zephyr_user));
/* Only run sensing when V above this (mV). Raise for more margin under solar (e.g. 1800). */
#define VOLTAGE_THRESHOLD_MV 800

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

/* Runs inference on demo_data (caller must fill it) and advertises. Sets adv_mfg_data from arguments and prediction. */
static void run_inference_and_advertise(int16_t last_x, int16_t last_y, int16_t last_z, uint16_t voltage_mv)
{
	const char *predictedLabel = NULL;
	float predictedScore = 0.0f;

	adv_mfg_data.x = last_x;
	adv_mfg_data.y = last_y;
	adv_mfg_data.z = last_z;
	adv_mfg_data.voltage_mv = voltage_mv;

	uint32_t start_cyc = k_cycle_get_32();
	int inferenceResult = ei_v3_classify_test(&predictedLabel, &predictedScore);
	uint32_t end_cyc = k_cycle_get_32();
	uint32_t delta_cyc = end_cyc - start_cyc;
	uint32_t latency_us = (uint32_t)((uint64_t)delta_cyc * 1000000ULL /
		CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC);
	(void)latency_us;

#if ENERGY_PROFILE
	total_us_inference += latency_us;
	calls_inference++;
	if (calls_inference >= ENERGY_PROFILE_INTERVAL) {
		energy_profile_print_and_reset(voltage_mv);
	}
#endif

	if (inferenceResult == 0 && predictedLabel != NULL) {
		uint8_t result_to_send = 0xFF;
		if      (strcmp(predictedLabel, "downstairs") == 0) result_to_send = 0;
		else if (strcmp(predictedLabel, "jump") == 0)       result_to_send = 1;
		else if (strcmp(predictedLabel, "running") == 0)    result_to_send = 2;
		else if (strcmp(predictedLabel, "sitting") == 0)   result_to_send = 3;
		else if (strcmp(predictedLabel, "standing") == 0)  result_to_send = 4;
		else if (strcmp(predictedLabel, "upstairs") == 0) result_to_send = 5;
		else if (strcmp(predictedLabel, "walking") == 0)   result_to_send = 6;
		adv_mfg_data.pred = result_to_send;
	}

#if ENERGY_PROFILE
	uint32_t t_ble = k_cycle_get_32();
#endif
	int adv_err = bt_le_adv_start(adv_param, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	if (adv_err) {
		LOG_ERR("adv_start err %d", adv_err);
	} else {
		k_sleep(K_MSEC(200));
		bt_le_adv_stop();
	}
	LOG_INF("ADV pred=%u", adv_mfg_data.pred);
#if ENERGY_PROFILE
	total_us_ble += CYCLES_TO_US(k_cycle_get_32() - t_ble);
	calls_ble++;
#endif
}

void thread_read_bma400(void)
{
	while (1) {

		k_sem_take(&bma400_ready, K_FOREVER);

#if USE_SOLAR_GATEKEEPER
		const struct device *spi_dev = DEVICE_DT_GET(DT_NODELABEL(spi1));
		pm_device_action_run(spi_dev, PM_DEVICE_ACTION_RESUME);
#endif

#if ENERGY_PROFILE
		uint32_t t0 = k_cycle_get_32();
#endif
		bma400_get_fifo_data(&fifo_frame, &bma_sensor);
		uint16_t accel_frames_req = FIFO_SAMPLES;
		bma400_extract_accel(&fifo_frame, accel_data, &accel_frames_req, &bma_sensor);
#if ENERGY_PROFILE
		total_us_spi_bma += CYCLES_TO_US(k_cycle_get_32() - t0);
		calls_spi_bma++;
		t0 = k_cycle_get_32();
#endif
		uint16_t batch_voltage = cached_voltage_mv;
#if !USE_SOLAR_GATEKEEPER
		batch_voltage = get_voltage_mv();
		cached_voltage_mv = batch_voltage;
#endif
#if ENERGY_PROFILE
		total_us_adc += CYCLES_TO_US(k_cycle_get_32() - t0);
		calls_adc++;
#endif

		int_en.type = BMA400_FIFO_WM_INT_EN;
		int_en.conf = BMA400_DISABLE;
		(void)bma400_enable_interrupt(&int_en, 1, &bma_sensor);
		bma400_set_power_mode(BMA400_MODE_SLEEP, &bma_sensor);

#if USE_SOLAR_GATEKEEPER
		pm_device_action_run(spi_dev, PM_DEVICE_ACTION_SUSPEND);
#endif

#if ENERGY_PROFILE
		uint32_t t_dp = k_cycle_get_32();
#endif
#if USE_SLIDING_WINDOW
		{
			int n = (int)accel_frames_req;
			if (n <= 0) goto after_inference;
			if (ring_count + n > FIFO_SAMPLES) {
				int drop = (ring_count + n) - FIFO_SAMPLES;
				memmove(ring_x, ring_x + drop, (FIFO_SAMPLES - drop) * sizeof(int16_t));
				memmove(ring_y, ring_y + drop, (FIFO_SAMPLES - drop) * sizeof(int16_t));
				memmove(ring_z, ring_z + drop, (FIFO_SAMPLES - drop) * sizeof(int16_t));
				ring_count = FIFO_SAMPLES - n;
			}
			for (int i = 0; i < n; i++) {
				ring_x[ring_count + i] = accel_data[i].x;
				ring_y[ring_count + i] = accel_data[i].y;
				ring_z[ring_count + i] = accel_data[i].z;
			}
			ring_count += n;
			if (ring_count > FIFO_SAMPLES) ring_count = FIFO_SAMPLES;
			if (ring_count < FIFO_SAMPLES) goto after_inference;
			for (int i = 0; i < FIFO_SAMPLES; i++) {
				demo_data[i]      = (float)ring_x[i];
				demo_data[i + 25] = (float)ring_z[i];
				demo_data[i + 50] = (float)ring_y[i];
			}
			run_inference_and_advertise(ring_x[FIFO_SAMPLES - 1], ring_y[FIFO_SAMPLES - 1],
						    ring_z[FIFO_SAMPLES - 1], batch_voltage);
		}
#else
		if (accel_frames_req >= FIFO_SAMPLES) {
			for (int i = 0; i < FIFO_SAMPLES; i++) {
				demo_data[i]      = (float)accel_data[i].x;
				demo_data[i + 25] = (float)accel_data[i].z;
				demo_data[i + 50] = (float)accel_data[i].y;
			}
			run_inference_and_advertise(accel_data[FIFO_SAMPLES - 1].x,
						    accel_data[FIFO_SAMPLES - 1].y,
						    accel_data[FIFO_SAMPLES - 1].z,
						    batch_voltage);
		}
#endif
after_inference:
#if ENERGY_PROFILE
		total_us_data_prep += CYCLES_TO_US(k_cycle_get_32() - t_dp);
		calls_data_prep++;
#endif

		(void)accel_frames_req;

#if USE_SOLAR_GATEKEEPER
		last_tx_done = true;
#else
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
			uint16_t val_mv;
			if (!last_tx_done)
				continue;
			val_mv = get_voltage_mv();
			if (val_mv == VOLTAGE_NO_DATA) {
				LOG_ERR("ADC read failed");
				continue;
			}
			cached_voltage_mv = val_mv;
			LOG_INF("1. Read ADC: %d mv, scaled: %d mv", val_mv, val_mv*15/10);
			if (val_mv < VOLTAGE_THRESHOLD_MV)
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

	fifo_conf.param.fifo_conf.conf_regs = BMA400_FIFO_8_BIT_EN
										| BMA400_FIFO_X_EN
										| BMA400_FIFO_Y_EN
										| BMA400_FIFO_Z_EN
										| BMA400_FIFO_AUTO_FLUSH;
	fifo_conf.param.fifo_conf.conf_status = BMA400_ENABLE;
	fifo_conf.param.fifo_conf.fifo_watermark = FIFO_WATERMARK_LEVEL;
	fifo_conf.param.fifo_conf.fifo_wm_channel = BMA400_INT_CHANNEL_1;

	rslt = bma400_set_device_conf(&fifo_conf, 1, &bma_sensor);

	fifo_frame.data = fifo_buff;
	fifo_frame.length = FIFO_SIZE;

	int_en.type = BMA400_FIFO_WM_INT_EN;
#if USE_SOLAR_GATEKEEPER
	int_en.conf = BMA400_DISABLE;
	bma400_set_power_mode(BMA400_MODE_SLEEP, &bma_sensor);
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

	// #region agent log
	LOG_INF("App started, RTT ok");
	// #endregion

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
	cached_voltage_mv = get_voltage_mv();
#endif

	bt_addr_le_t addr;
	err = bt_addr_le_from_str("FF:EE:DD:CC:BB:AD", "random", &addr);
	err = bt_id_create(&addr, NULL);
	err = bt_enable(NULL);
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
	k_timer_start(&timer_run_policy, K_MSEC(SOLAR_RUN_POLICY_MS), K_MSEC(SOLAR_RUN_POLICY_MS));
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
