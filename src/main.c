/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/pm/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include "bma400.h"
#include "bma400_defs.h"

//BLE STUFF
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/gap.h>

// BLE STUFF
#define DEVICE_NAME       CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN   (sizeof(DEVICE_NAME) - 1)
#define BT_UUID_ACCEL_SERVICE_VAL \ 
	BT_UUID_128_ENCODE(0x12345678,0x1234,0x5678,0x1234,0x1234567890ab)

#define BT_UUID_ACCEL_CHAR_VAL \
	BT_UUID_128_ENCODE(0x12345679,0x1234,0x5678,0x1234,0x1234567890ab)

static struct bt_uuid_128 accel_service_uuid = BT_UUID_INIT_128(BT_UUID_ACCEL_SERVICE_VAL);
static struct bt_uuid_128 accel_char_uuid    = BT_UUID_INIT_128(BT_UUID_ACCEL_CHAR_VAL);	

static uint8_t accel_value[6] = {0};

static void accel_ccc_cfg_changed(const struct bt_gatt_attr *attr,uint16_t value){

	bool notif_enabled = (value == BT_GATT_CCC_NOTIFY);
	printk("Accel notifications %s\n",notif_enabled ? "enabled" : "disabled");
}

BT_GATT_SERVICE_DEFINE(accel_svc,
	BT_GATT_PRIMARY_SERVICE(&accel_service_uuid),
	BT_GATT_CHARACTERISTIC(&accel_char_uuid.uuid,
			       BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_NONE,
			       NULL, NULL, accel_value),
	BT_GATT_CCC(accel_ccc_cfg_changed,
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE)
);

static struct bt_conn *current_conn;

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		printk("Connection failed (err %u)\n", err);
		return;
	}
	printk("Connected\n");
	current_conn = bt_conn_ref(conn);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	printk("Disconnected (reason 0x%02x)\n", reason);
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

static void bt_ready(int err)
{
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return;
	}

	printk("Bluetooth initialized\n");

	err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, ad, ARRAY_SIZE(ad),
			      NULL, 0);
	if (err) {
		printk("Advertising failed to start (err %d)\n", err);
		return;
	}

	printk("Advertising started\n");
}

static void send_accel_notification(int16_t x, int16_t y, int16_t z){
	if(!current_conn) return;

	accel_value[0] = x & 0xFF;
	accel_value[1] = (x >> 8) & 0xFF;
	accel_value[2] = y & 0xFF;
	accel_value[3] = (y >> 8) & 0xFF;
	accel_value[4] = z & 0xFF;
	accel_value[5] = (z >> 8) & 0xFF;

	int err = bt_gatt_notify(current_conn, &accel_svc.attrs[1],
				 accel_value, sizeof(accel_value));
	if (err) {
		printk("Notify failed (err %d)\n", err);
	}
}


LOG_MODULE_REGISTER(app, LOG_LEVEL_DBG);

// threads
#define STACKSIZE 1024
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
#define BMA400_REG_FIFO_CONFIG_1                  UINT8_C(0x27)
#define FIFOINTER 3
#define FIFO_SAMPLES 75 // number of samples for fifo content
#define FIFO_WATERMARK_LEVEL    UINT16_C(FIFO_SAMPLES*4) // 4 bytes per frame (XYZ+header)
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

struct bma400_sensor_conf settings;


void bma_int_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	// set the semaphore
	//LOG_INF("INT fired! pins=0x%08x", pins);	
	k_sem_give(&bma400_ready);
}


void thread_read_bma400(void)
{
	static int count = 0;
	while(1){
		LOG_INF("In the read thread");
		// Add this to your nRF52832 code
bt_addr_le_t addr;
size_t count = 1;

bt_id_get(&addr, &count);

printk("MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n",
       addr.a.val[5], addr.a.val[4], addr.a.val[3],
       addr.a.val[2], addr.a.val[1], addr.a.val[0]);
		k_sem_take(&bma400_ready, K_FOREVER); // Sleep here if semaphore is at 0

		// Enable SPI
		const struct device *cons = DEVICE_DT_GET(DT_NODELABEL(spi1));
		pm_device_action_run(cons, PM_DEVICE_ACTION_RESUME);

		// Read one sample
		bma400_get_accel_data(BMA400_DATA_ONLY, &acc_data, &bma_sensor);
		
		LOG_INF("x=%d, y=%d, z=%d", acc_data.x, acc_data.y, acc_data.z); //print data to console
		send_accel_notification(acc_data.x,acc_data.y,acc_data.z);
		// Disable SPI
		pm_device_action_run(cons, PM_DEVICE_ACTION_SUSPEND);
	}
}

// Need to make sure stack is big enough to run NN code
K_THREAD_DEFINE(thread_read_bma400_id, STACKSIZE*4, thread_read_bma400, NULL, NULL, NULL, THREAD_READ_BMA_PRIORITY, 0, 0);



BMA400_INTF_RET_TYPE read_reg_spi(uint8_t reg_address, uint8_t* data, uint32_t len, void* intf_ptr)
{
	int err;

	/* STEP 4.1 - Set the transmit and receive buffers */
	// When reading the BMA400, the first byte read is a dummy, so we need to read two bytes and interpret the second one
	// For a transceive there are 3 steps:
	//		   |       step 1         | step 2 | step 3
	//	Master | 1[7 bit reg address] |  0x0   |   0x0
	//	Slave  |	     dummy        | dummy  | data from sensor	
	// therefore, if we want to read 1 byte from the sensor, we need to read 3 bytes from the sensor (1 during send, 2 during read)
	// Since the BMA400 API already adds the dummy byte, we only need to add one more byte
	// This extra byte is because the first read happens during the register write, so we need to read again	

	uint8_t tx_buffer = reg_address;
	struct spi_buf tx_spi_buf		= {.buf = (void *)&tx_buffer, .len = 1};
	struct spi_buf_set tx_spi_buf_set 	= {.buffers = &tx_spi_buf, .count = 1};
	struct spi_buf rx_spi_bufs 		= {.buf = rx_buffer, .len = len+1};
	struct spi_buf_set rx_spi_buf_set	= {.buffers = &rx_spi_bufs, .count = 1};
	

	/* STEP 4.2 - Call the transceive function */
	err = spi_transceive_dt(&spispec, &tx_spi_buf_set, &rx_spi_buf_set);
	if (err < 0) {
		LOG_ERR("spi_transceive_dt() failed, err: %d, 0x%02X", err,tx_buffer);
		// return err;
	}

	for(int i = 0; i < len; i++)
	{
		data[i] = rx_buffer[i+1]; // data[0] = dummy byte, data[1] = data
	}

	return 0;
}

BMA400_INTF_RET_TYPE write_reg_spi(uint8_t reg_address, const uint8_t* data, uint32_t len, void* intf_ptr)
{
	int err;

	/* STEP 5.1 - delcare a tx buffer having register address and data */
	// When writing to the BMA400, the first byte read is an adress, so we need to write two bytes
	// For a transceive there are 2 steps:
	//		   |       step 1         | step 2 |
	//	Master | 1[7 bit reg address] |  val   |
	//	Slave  |	     dummy        | dummy  |
	// therefore, if we want to write 1 byte to the sensor, we need to write 2 bytes from the sensor (1 adress, 1 data)
	uint8_t tx_buf[2] = {reg_address, data[0]}; // to write, set the MSB to 0
	struct spi_buf	tx_spi_buf 		= {.buf = tx_buf, .len = len+1};
	struct spi_buf_set tx_spi_buf_set	= {.buffers = &tx_spi_buf, .count = 1};

	/* STEP 5.2 - call the spi_write_dt function with SPISPEC to write buffers */
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
	int8_t rslt = bma400_get_sensor_conf(&conf, 1, &bma_sensor);

	conf.param.accel.odr = BMA400_ODR_25HZ;
	conf.param.accel.range = BMA400_RANGE_4G;
	conf.param.accel.data_src = BMA400_DATA_SRC_ACCEL_FILT_1;

	rslt = bma400_set_sensor_conf(&conf, 1, &bma_sensor);

	fifo_conf.type = BMA400_FIFO_CONF;

	rslt = bma400_get_device_conf(&fifo_conf, 1, &bma_sensor);

	fifo_conf.param.fifo_conf.conf_regs = BMA400_FIFO_8_BIT_EN | BMA400_FIFO_X_EN 
										| BMA400_FIFO_Y_EN 
										| BMA400_FIFO_Z_EN
										| BMA400_FIFO_AUTO_FLUSH;   // flush on power mode change
	fifo_conf.param.fifo_conf.conf_status = BMA400_ENABLE;
	fifo_conf.param.fifo_conf.fifo_watermark = FIFO_WATERMARK_LEVEL;
	fifo_conf.param.fifo_conf.fifo_wm_channel = BMA400_INT_CHANNEL_1;

	rslt = bma400_set_device_conf(&fifo_conf, 1, &bma_sensor);

	fifo_frame.data = fifo_buff;
	fifo_frame.length = FIFO_SIZE;

	int_en.type = BMA400_FIFO_WM_INT_EN;
	int_en.conf = BMA400_ENABLE;

	bma400_set_power_mode(BMA400_MODE_NORMAL,&bma_sensor);
	rslt = bma400_enable_interrupt(&int_en, 1, &bma_sensor);
}

void init_activity()
{
	settings.type = BMA400_GEN1_INT;
	bma400_get_sensor_conf(&settings, 1, &bma_sensor);

	settings.param.gen_int.int_chan = BMA400_INT_CHANNEL_1;
    settings.param.gen_int.axes_sel = BMA400_AXIS_XYZ_EN;
    settings.param.gen_int.data_src = BMA400_DATA_SRC_ACC_FILT2;
	settings.param.gen_int.criterion_sel = BMA400_ACTIVITY_INT;
	settings.param.gen_int.evaluate_axes = BMA400_ANY_AXES_INT;
    settings.param.gen_int.ref_update = BMA400_UPDATE_EVERY_TIME;
	settings.param.gen_int.hysteresis = BMA400_HYST_48_MG;
	settings.param.gen_int.gen_int_thres = 0x10;
	settings.param.gen_int.gen_int_dur = 15;

	bma400_set_sensor_conf(&settings, 1, &bma_sensor);

	int_en.type = BMA400_GEN1_INT_EN;
	int_en.conf = BMA400_ENABLE;

	bma400_set_power_mode(BMA400_MODE_NORMAL,&bma_sensor);
	bma400_enable_interrupt(&int_en, 1, &bma_sensor);
}

void init_read_lp()
{
	conf.type = BMA400_ACCEL;
	int8_t rslt = bma400_get_sensor_conf(&conf, 1, &bma_sensor);

	conf.param.accel.odr = BMA400_ODR_25HZ;
	conf.param.accel.range = BMA400_RANGE_4G;
	conf.param.accel.data_src = BMA400_DATA_SRC_ACCEL_FILT_1;
	conf.param.accel.osr_lp = BMA400_ACCEL_OSR_SETTING_0;
	conf.param.accel.int_chan = BMA400_INT_CHANNEL_1;

	rslt = bma400_set_sensor_conf(&conf, 1, &bma_sensor);

	int_en.type = BMA400_DRDY_INT_EN;
	int_en.conf = BMA400_ENABLE;

	bma400_set_power_mode(BMA400_MODE_LOW_POWER,&bma_sensor);
	bma400_enable_interrupt(&int_en, 1, &bma_sensor);
}

int main(void)
{
	int err;
	
	/* STEP 10.1 - Check if SPI and GPIO devices are ready */
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
	err = bt_enable(bt_ready);
	if(err){
		printk("bt_enable failed (err %d)\n",err);
		return -1;
	} else{
		printk("bt_enable() called, waiting for callback...\n");
	}
	/* STEP 3 - Configure the interrupt on the button's pin */
	err = gpio_pin_interrupt_configure_dt(&int_pin, GPIO_INT_EDGE_RISING);
	// err = gpio_pin_interrupt_configure_dt(&int_pin, GPIO_INT_LEVEL_ACTIVE);

	/* STEP 6 - Initialize the static struct gpio_callback variable   */
	gpio_init_callback(&int_cb_data, bma_int_handler, BIT(int_pin.pin));

	/* STEP 7 - Add the callback function by calling gpio_add_callback()   */
	gpio_add_callback(int_pin.port, &int_cb_data);


	bma400_init(&bma_sensor);
  

	// init_activity();
	// init_fifo_watermark();
	init_read_lp();

	const struct device *cons = DEVICE_DT_GET(DT_NODELABEL(spi1));
	pm_device_action_run(cons, PM_DEVICE_ACTION_SUSPEND);
	

	while(1){
		k_sleep(K_FOREVER);
	}

	return 0;
}