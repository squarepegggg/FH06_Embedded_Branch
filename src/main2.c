/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/* Set to 1 for solar voltage gatekeeper (requires ADC devicetree). 0 = battery test mode. */
#define USE_ADC 0

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/pm/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include "bma400.h"
#include "bma400_defs.h"

#include "run_nn.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/addr.h>
#if USE_ADC
#include <zephyr/drivers/adc.h>
#endif

LOG_MODULE_REGISTER(app, LOG_LEVEL_DBG);

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

/* STEP 2.1 - Declare the Company identifier (Company ID) */
#define COMPANY_ID_CODE 0x0059

int16_t buf;

bool last_tx_done = true;

/* STEP 2.2 - Declare the structure for your custom data  */
typedef struct adv_mfg_data {
    uint16_t company_code; /* Company Identifier Code. */
	uint8_t pred;
} adv_mfg_data_type;

#if USE_ADC
struct adc_sequence sequence;
static const struct adc_dt_spec adc_channel = ADC_DT_SPEC_GET(DT_PATH(zephyr_user));
#endif

static const struct bt_le_adv_param *adv_param =
    BT_LE_ADV_PARAM(BT_LE_ADV_OPT_USE_IDENTITY,
            32, /* Min Advertising Interval 250ms */
            33, /* Max Advertising Interval 250.625ms */
            NULL);
static adv_mfg_data_type adv_mfg_data = {
    .company_code = COMPANY_ID_CODE,
	.pred = 0x0
};
static const struct bt_data ad[] = {
    BT_DATA(BT_DATA_MANUFACTURER_DATA, (unsigned char *)&adv_mfg_data, sizeof(adv_mfg_data)),
};

#define STACKSIZE 1024
#define THREAD_READ_BMA_PRIORITY 7
#define THREAD_RUN_POLICY_PRIORITY 8
K_SEM_DEFINE(bma400_ready, 0, 1);
K_SEM_DEFINE(run_policy, 0, 1);

#define SPIOP	SPI_WORD_SET(8) | SPI_TRANSFER_MSB
struct spi_dt_spec spispec = SPI_DT_SPEC_GET(DT_NODELABEL(bma400), SPIOP, 0);
uint8_t rx_buffer[128] = {0};

#define int_NODE DT_ALIAS(int1)
static const struct gpio_dt_spec int_pin = GPIO_DT_SPEC_GET(int_NODE, gpios);
static struct gpio_callback int_cb_data;

#define BMA400_REG_FIFO_CONFIG_1                  UINT8_C(0x27)
#define FIFOINTER 3
#define FIFO_SAMPLES 25
#define FIFO_WATERMARK_LEVEL    UINT16_C(FIFO_SAMPLES*4)
#define FIFO_FULL_SIZE          UINT16_C(1024)
#define FIFO_SIZE               (FIFO_FULL_SIZE + BMA400_FIFO_BYTES_OVERREAD)
#define FIFO_ACCEL_FRAME_COUNT  UINT8_C(FIFO_SAMPLES)

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

struct bma400_sensor_data acc_data;
struct bma400_int_enable int_en;
struct bma400_fifo_data fifo_frame;
struct bma400_device_conf fifo_conf;
struct bma400_sensor_conf conf;
uint8_t fifo_buff[FIFO_SIZE] = { 0 };
struct bma400_fifo_sensor_data accel_data[FIFO_ACCEL_FRAME_COUNT] = { { 0 } };
struct bma400_sensor_conf settings;

void bma_int_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	k_sem_give(&bma400_ready);
}

void thread_read_bma400(void)
{
	while(1){
		LOG_INF("In the read thread");
		k_sem_take(&bma400_ready, K_FOREVER);

		const struct device *cons = DEVICE_DT_GET(DT_NODELABEL(spi1));
		pm_device_action_run(cons, PM_DEVICE_ACTION_RESUME);

		bma400_get_fifo_data(&fifo_frame, &bma_sensor);
		uint16_t accel_frames_req = FIFO_SAMPLES;
		bma400_extract_accel(&fifo_frame, accel_data, &accel_frames_req, &bma_sensor);

		int_en.type = BMA400_FIFO_WM_INT_EN;
		int_en.conf = BMA400_DISABLE;
		bma400_enable_interrupt(&int_en, 1, &bma_sensor);
		bma400_set_power_mode(BMA400_MODE_SLEEP,&bma_sensor);

		pm_device_action_run(cons, PM_DEVICE_ACTION_SUSPEND);

		run_nn_infer(accel_data, accel_frames_req);
		adv_mfg_data.pred = (uint8_t)biggest_idx;

		if (biggest_idx == 0) LOG_INF("downstairs");
		else if (biggest_idx == 1) LOG_INF("jump");
		else if (biggest_idx == 2) LOG_INF("running");
		else if (biggest_idx == 3) LOG_INF("sitting");
		else if (biggest_idx == 4) LOG_INF("standing");
		else if (biggest_idx == 5) LOG_INF("upstairs");
		else if (biggest_idx == 6) LOG_INF("walking");

		bt_le_adv_update_data(ad, ARRAY_SIZE(ad), NULL, 0);
		bt_le_adv_start(adv_param, ad, ARRAY_SIZE(ad), NULL, 0);
		k_sleep(K_MSEC(10));
		bt_le_adv_stop();
		last_tx_done = true;
	}
}

K_THREAD_DEFINE(thread_read_bma400_id, STACKSIZE*4, thread_read_bma400, NULL, NULL, NULL, THREAD_READ_BMA_PRIORITY, 0, 0);

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
	}
	for(int i = 0; i < len; i++) {
		data[i] = rx_buffer[i+1];
	}
	return 0;
}

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

void init_fifo_watermark()
{
	conf.type = BMA400_ACCEL;
	bma400_get_sensor_conf(&conf, 1, &bma_sensor);
	conf.param.accel.odr = BMA400_ODR_25HZ;
	conf.param.accel.range = BMA400_RANGE_4G;
	conf.param.accel.data_src = BMA400_DATA_SRC_ACCEL_FILT_1;
	bma400_set_sensor_conf(&conf, 1, &bma_sensor);

	fifo_conf.type = BMA400_FIFO_CONF;
	bma400_get_device_conf(&fifo_conf, 1, &bma_sensor);
	fifo_conf.param.fifo_conf.conf_regs = BMA400_FIFO_8_BIT_EN | BMA400_FIFO_X_EN
										| BMA400_FIFO_Y_EN | BMA400_FIFO_Z_EN
										| BMA400_FIFO_AUTO_FLUSH;
	fifo_conf.param.fifo_conf.conf_status = BMA400_ENABLE;
	fifo_conf.param.fifo_conf.fifo_watermark = FIFO_WATERMARK_LEVEL;
	fifo_conf.param.fifo_conf.fifo_wm_channel = BMA400_INT_CHANNEL_1;
	bma400_set_device_conf(&fifo_conf, 1, &bma_sensor);

	fifo_frame.data = fifo_buff;
	fifo_frame.length = FIFO_SIZE;
	int_en.type = BMA400_FIFO_WM_INT_EN;
	int_en.conf = BMA400_DISABLE;
	bma400_set_power_mode(BMA400_MODE_LOW_POWER,&bma_sensor);
	bma400_enable_interrupt(&int_en, 1, &bma_sensor);
}

static void timer0_handler(struct k_timer *dummy)
{
	k_sem_give(&run_policy);
}

void thread_run_policy(void)
{
    while(1)
    {
        k_sem_take(&run_policy, K_FOREVER);
#if USE_ADC
        static int val_mv;
        int8_t err = adc_read(adc_channel.dev, &sequence);
        if (err < 0) LOG_ERR("Could not read (%d)", err);
        val_mv = (int)buf;
        err = adc_raw_to_millivolts_dt(&adc_channel, &val_mv);
        LOG_INF("ADC: %d mv", val_mv);
        if (val_mv > 1600 && last_tx_done == true)
#else
        if (last_tx_done == true)
#endif
        {
            int_en.type = BMA400_FIFO_WM_INT_EN;
            int_en.conf = BMA400_ENABLE;
            bma400_set_power_mode(BMA400_MODE_NORMAL, &bma_sensor);
            bma400_enable_interrupt(&int_en, 1, &bma_sensor);
            last_tx_done = false;
        }
    }
}
K_THREAD_DEFINE(thread_run_policy_id, STACKSIZE, thread_run_policy, NULL, NULL, NULL, THREAD_RUN_POLICY_PRIORITY, 0, 0);
K_TIMER_DEFINE(timer0, timer0_handler, NULL);

int main(void)
{
	int err;

	bt_addr_le_t addr;
	err = bt_addr_le_from_str("FF:EE:DD:CC:BB:AD", "random", &addr);
	err = bt_id_create(&addr, NULL);

	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)\n", err);
		return -1;
	}

	err = spi_is_ready_dt(&spispec);
	if (!err) {
		LOG_ERR("Error: SPI device is not ready, err: %d", err);
		return 0;
	}
	if (!device_is_ready(int_pin.port)) {
		return -1;
	}
	err = gpio_pin_configure_dt(&int_pin, GPIO_INPUT);
	if (err < 0) return -1;
	err = gpio_pin_interrupt_configure_dt(&int_pin, GPIO_INT_EDGE_RISING);
	gpio_init_callback(&int_cb_data, bma_int_handler, BIT(int_pin.pin));
	gpio_add_callback(int_pin.port, &int_cb_data);

#if USE_ADC
	sequence.buffer = &buf;
	sequence.buffer_size = sizeof(buf);
	if (!adc_is_ready_dt(&adc_channel)) {
		LOG_ERR("ADC not ready");
		return 0;
	}
	err = adc_channel_setup_dt(&adc_channel);
	if (err < 0) {
		LOG_ERR("ADC setup failed (%d)", err);
		return 0;
	}
	err = adc_sequence_init_dt(&adc_channel, &sequence);
	if (err < 0) {
		LOG_ERR("ADC sequence init failed");
		return 0;
	}
#endif

	LOG_INF("APP START (main2, battery mode)");
	bma400_init(&bma_sensor);
	init_fifo_watermark();

	const struct device *cons = DEVICE_DT_GET(DT_NODELABEL(spi1));
	pm_device_action_run(cons, PM_DEVICE_ACTION_SUSPEND);

	k_timer_start(&timer0, K_MSEC(200), K_MSEC(200));

	while(1){
		k_sleep(K_FOREVER);
	}
	return 0;
}
