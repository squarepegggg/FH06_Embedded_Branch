// /*
//  * Copyright (c) 2024 Nordic Semiconductor ASA
//  *
//  * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
//  */

//  #include <zephyr/kernel.h>
//  #include <zephyr/logging/log.h>
//  #include <zephyr/device.h>
//  #include <zephyr/pm/device.h>
//  #include <zephyr/devicetree.h>
//  #include <zephyr/drivers/gpio.h>
//  #include <zephyr/drivers/spi.h>
//  #include "bma400.h"
//  #include "bma400_defs.h"
 
//  #include "run_nn.h"
 
//  #include <zephyr/bluetooth/bluetooth.h>
//  #include <zephyr/bluetooth/addr.h>
//  #include <zephyr/drivers/adc.h>
 
//  LOG_MODULE_REGISTER(app, LOG_LEVEL_DBG);
 
//  #define DEVICE_NAME CONFIG_BT_DEVICE_NAME
//  #define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)
 
//  /* STEP 2.1 - Declare the Company identifier (Company ID) */
//  #define COMPANY_ID_CODE 0x0059
 
//  int16_t buf;
 
//  bool last_tx_done = true;
 
// /* STEP 2.2 - Declare the structure for your custom data  */
// typedef struct __attribute__((packed)) adv_mfg_data {
// 	uint16_t company_code; /* Company Identifier Code. */
// 	uint8_t pred;
// 	int16_t x;
// 	int16_t y;
// 	int16_t z;
// 	uint16_t voltage_mv;
// } adv_mfg_data_type;
 
//  struct adc_sequence sequence;
 
 
//  static const struct adc_dt_spec adc_channel = ADC_DT_SPEC_GET(DT_PATH(zephyr_user));
 
//  static const struct bt_le_adv_param *adv_param =
// 	BT_LE_ADV_PARAM(BT_LE_ADV_OPT_USE_IDENTITY,
// 			32, 33, NULL);
// /* STEP 2.3 - Define and initialize a variable of type adv_mfg_data_type */
// static adv_mfg_data_type adv_mfg_data = {
// 	.company_code = COMPANY_ID_CODE,
// 	.pred = 0x0,
// 	.x = 0,
// 	.y = 0,
// 	.z = 0,
// 	.voltage_mv = 0xFFFFU
// };

// static const struct bt_data ad[] = {
// 	BT_DATA(BT_DATA_MANUFACTURER_DATA, (unsigned char *)&adv_mfg_data, sizeof(adv_mfg_data)),
// };

// // #define ADV_DEVICE_NAME "AccelDev"
// // static const struct bt_data sd[] = {
// // 	BT_DATA(BT_DATA_NAME_COMPLETE, ADV_DEVICE_NAME, sizeof(ADV_DEVICE_NAME) - 1),
// // };

// static uint16_t cached_voltage_mv = 0xFFFFU;

// // threads
//  #define STACKSIZE 1024
//  #define THREAD_READ_BMA_PRIORITY 7
//  #define THREAD_RUN_POLICY_PRIORITY 8
//  K_SEM_DEFINE(bma400_ready, 0, 1);
//  K_SEM_DEFINE(run_policy, 0, 1);
 
//  // SPI
//  #define SPIOP	SPI_WORD_SET(8) | SPI_TRANSFER_MSB
//  struct spi_dt_spec spispec = SPI_DT_SPEC_GET(DT_NODELABEL(bma400), SPIOP, 0);
//  uint8_t rx_buffer[128] = {0};
 
//  // interrupt GPIO
//  #define int_NODE DT_ALIAS(int1)
//  static const struct gpio_dt_spec int_pin = GPIO_DT_SPEC_GET(int_NODE, gpios);
//  static struct gpio_callback int_cb_data;
 
//  // BMA400
//  #define BMA400_REG_FIFO_CONFIG_1                  UINT8_C(0x27)
//  #define FIFOINTER 3
//  #define FIFO_SAMPLES 25 // number of samples for fifo content
//  #define FIFO_WATERMARK_LEVEL    UINT16_C(FIFO_SAMPLES*4) // 4 bytes per frame (XYZ+header)
//  #define FIFO_FULL_SIZE          UINT16_C(1024)
//  #define FIFO_SIZE               (FIFO_FULL_SIZE + BMA400_FIFO_BYTES_OVERREAD)
//  #define FIFO_ACCEL_FRAME_COUNT  UINT8_C(FIFO_SAMPLES)
 
//  BMA400_INTF_RET_TYPE read_reg_spi(uint8_t reg_address, uint8_t* data, uint32_t len, void* intf_ptr);
//  BMA400_INTF_RET_TYPE write_reg_spi(uint8_t reg_address, const uint8_t* data, uint32_t len, void* intf_ptr);
//  void bma400_delay_us(uint32_t period, void *intf_ptr) {
// 	 k_usleep(period);
//  }
 
//  static uint8_t              dev_addr    = 31;
//  struct bma400_dev           bma_sensor         = {
// 		 .intf = BMA400_SPI_INTF,
// 		 .intf_ptr = &dev_addr,
// 		 .read = read_reg_spi,
// 		 .write = write_reg_spi,
// 		 .delay_us = bma400_delay_us,
// 		 .read_write_len = 8
//  };
 
//  struct bma400_sensor_data acc_data;
 
//  struct bma400_int_enable int_en;
//  struct bma400_fifo_data fifo_frame;
//  struct bma400_device_conf fifo_conf;
//  struct bma400_sensor_conf conf;
//  uint8_t fifo_buff[FIFO_SIZE] = { 0 };
 
//  struct bma400_fifo_sensor_data accel_data[FIFO_ACCEL_FRAME_COUNT] = { { 0 } };
//  struct bma400_sensor_conf settings;
 
 
//  void bma_int_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
//  {
// 	 // set the semaphore
// 	 k_sem_give(&bma400_ready);
//  }
 
 
//  void thread_read_bma400(void)
//  {
// 	 static int count = 0;
// 	 while(1){
// 		 //LOG_INF("In the read thread");
// 		 k_sem_take(&bma400_ready, K_FOREVER); // Sleep here if semaphore is at 0maw
// 		 // Enable SPI
// 		 const struct device *cons = DEVICE_DT_GET(DT_NODELABEL(spi1));
// 		 pm_device_action_run(cons, PM_DEVICE_ACTION_RESUME);
 
// 		 // read data from bma400 fifo
// 		 bma400_get_fifo_data(&fifo_frame, &bma_sensor);
// 		 uint16_t accel_frames_req = FIFO_SAMPLES;
// 		 bma400_extract_accel(&fifo_frame, accel_data, &accel_frames_req, &bma_sensor);
// 		 // LOG_INF("Read FIFO Data, disabling BMA");
 
// 		 // after reading, disable the interrupt and put the bma400 to sleep
// 		 int_en.type = BMA400_FIFO_WM_INT_EN;
// 		 int_en.conf = BMA400_DISABLE;
// 		 int8_t rslt = bma400_enable_interrupt(&int_en, 1, &bma_sensor);
// 		 bma400_set_power_mode(BMA400_MODE_SLEEP,&bma_sensor);
 
// 		 // Disable SPI
// 		 pm_device_action_run(cons, PM_DEVICE_ACTION_SUSPEND);
 
// 		// TODO: Add neural network code here to do inference
// 		// data is in "accel_data"

// 		/* Run inference when we have enough samples (25 for EI model) */
// 		if (accel_frames_req >= 25) {
// 			run_nn_infer(accel_data, accel_frames_req);
// 		}

// 		/* Fill advertising payload for desktop app (company 0x0059, 11-byte format) */
// 		adv_mfg_data.pred = biggest_idx;
// 		adv_mfg_data.x = accel_data[accel_frames_req > 0 ? accel_frames_req - 1 : 0].x;
// 		adv_mfg_data.y = accel_data[accel_frames_req > 0 ? accel_frames_req - 1 : 0].y;
// 		adv_mfg_data.z = accel_data[accel_frames_req > 0 ? accel_frames_req - 1 : 0].z;
// 		adv_mfg_data.voltage_mv = cached_voltage_mv;

// 		bt_le_adv_update_data(ad, ARRAY_SIZE(ad), NULL,0);
// 		bt_le_adv_start(adv_param, ad,ARRAY_SIZE(ad),NULL,0);
// 		k_sleep(K_MSEC(10));
// 		bt_le_adv_stop();
// 		last_tx_done = true;
 
// 	 }
//  }
 
// // Need to make sure stack is big enough to run NN code (EI/TFLite Micro needs ~8KB)
// K_THREAD_DEFINE(thread_read_bma400_id, 8192, thread_read_bma400, NULL, NULL, NULL, THREAD_READ_BMA_PRIORITY, 0, 0);
 
 
 
//  BMA400_INTF_RET_TYPE read_reg_spi(uint8_t reg_address, uint8_t* data, uint32_t len, void* intf_ptr)
//  {
// 	 int err;
 
// 	 /* STEP 4.1 - Set the transmit and receive buffers */
// 	 // When reading the BMA400, the first byte read is a dummy, so we need to read two bytes and interpret the second one
// 	 // For a transceive there are 3 steps:
// 	 //		   |       step 1         | step 2 | step 3
// 	 //	Master | 1[7 bit reg address] |  0x0   |   0x0
// 	 //	Slave  |	     dummy        | dummy  | data from sensor	
// 	 // therefore, if we want to read 1 byte from the sensor, we need to read 3 bytes from the sensor (1 during send, 2 during read)
// 	 // Since the BMA400 API already adds the dummy byte, we only need to add one more byte
// 	 // This extra byte is because the first read happens during the register write, so we need to read again	
 
// 	 uint8_t tx_buffer = reg_address;
// 	 struct spi_buf tx_spi_buf		= {.buf = (void *)&tx_buffer, .len = 1};
// 	 struct spi_buf_set tx_spi_buf_set 	= {.buffers = &tx_spi_buf, .count = 1};
// 	 struct spi_buf rx_spi_bufs 		= {.buf = rx_buffer, .len = len+1};
// 	 struct spi_buf_set rx_spi_buf_set	= {.buffers = &rx_spi_bufs, .count = 1};
	 
 
// 	 /* STEP 4.2 - Call the transceive function */
// 	 err = spi_transceive_dt(&spispec, &tx_spi_buf_set, &rx_spi_buf_set);
// 	 if (err < 0) {
// 		 LOG_ERR("spi_transceive_dt() failed, err: %d, 0x%02X", err,tx_buffer);
// 		 return err;
// 	 }
 
// 	 for(int i = 0; i < len; i++)
// 	 {
// 		 data[i] = rx_buffer[i+1]; // data[0] = dummy byte, data[1] = data
// 	 }
 
// 	 return 0;
//  }
 
//  BMA400_INTF_RET_TYPE write_reg_spi(uint8_t reg_address, const uint8_t* data, uint32_t len, void* intf_ptr)
//  {
// 	 int err;
 
// 	 /* STEP 5.1 - delcare a tx buffer having register address and data */
// 	 // When writing to the BMA400, the first byte read is an adress, so we need to write two bytes
// 	 // For a transceive there are 2 steps:
// 	 //		   |       step 1         | step 2 |
// 	 //	Master | 1[7 bit reg address] |  val   |
// 	 //	Slave  |	     dummy        | dummy  |
// 	 // therefore, if we want to write 1 byte to the sensor, we need to write 2 bytes from the sensor (1 adress, 1 data)
// 	 uint8_t tx_buf[2] = {reg_address, data[0]}; // to write, set the MSB to 0
// 	 struct spi_buf	tx_spi_buf 		= {.buf = tx_buf, .len = len+1};
// 	 struct spi_buf_set tx_spi_buf_set	= {.buffers = &tx_spi_buf, .count = 1};
 
// 	 /* STEP 5.2 - call the spi_write_dt function with SPISPEC to write buffers */
// 	 err = spi_write_dt(&spispec, &tx_spi_buf_set);
// 	 if (err < 0) {
// 		 LOG_ERR("spi_write_dt() failed, err %d", err);
// 		 return err;
// 	 }
 
// 	 return 0;
//  }
 
//  void init_fifo_watermark()
//  {
// 	 conf.type = BMA400_ACCEL;
// 	 int8_t rslt = bma400_get_sensor_conf(&conf, 1, &bma_sensor);
 
// 	 conf.param.accel.odr = BMA400_ODR_25HZ;
// 	 conf.param.accel.range = BMA400_RANGE_4G;
// 	 conf.param.accel.data_src = BMA400_DATA_SRC_ACCEL_FILT_1;
 
// 	 rslt = bma400_set_sensor_conf(&conf, 1, &bma_sensor);
 
// 	 fifo_conf.type = BMA400_FIFO_CONF;
 
// 	 rslt = bma400_get_device_conf(&fifo_conf, 1, &bma_sensor);
 
// 	 fifo_conf.param.fifo_conf.conf_regs = BMA400_FIFO_8_BIT_EN | BMA400_FIFO_X_EN 
// 										 | BMA400_FIFO_Y_EN 
// 										 | BMA400_FIFO_Z_EN
// 										 | BMA400_FIFO_AUTO_FLUSH;   // flush on power mode change
// 	 fifo_conf.param.fifo_conf.conf_status = BMA400_ENABLE;
// 	 fifo_conf.param.fifo_conf.fifo_watermark = FIFO_WATERMARK_LEVEL;
// 	 fifo_conf.param.fifo_conf.fifo_wm_channel = BMA400_INT_CHANNEL_1;
 
// 	 rslt = bma400_set_device_conf(&fifo_conf, 1, &bma_sensor);
 
// 	 fifo_frame.data = fifo_buff;
// 	 fifo_frame.length = FIFO_SIZE;
 
// 	 int_en.type = BMA400_FIFO_WM_INT_EN;
// 	 int_en.conf = BMA400_DISABLE;
 
// 	 bma400_set_power_mode(BMA400_MODE_SLEEP,&bma_sensor);
// 	 rslt = bma400_enable_interrupt(&int_en, 1, &bma_sensor);
//  }
 
//  void init_activity()
//  {
// 	 settings.type = BMA400_GEN1_INT;
// 	 bma400_get_sensor_conf(&settings, 1, &bma_sensor);
 
// 	 settings.param.gen_int.int_chan = BMA400_INT_CHANNEL_1;
// 	 settings.param.gen_int.axes_sel = BMA400_AXIS_XYZ_EN;
// 	 settings.param.gen_int.data_src = BMA400_DATA_SRC_ACC_FILT2;
// 	 settings.param.gen_int.criterion_sel = BMA400_ACTIVITY_INT;
// 	 settings.param.gen_int.evaluate_axes = BMA400_ANY_AXES_INT;
// 	 settings.param.gen_int.ref_update = BMA400_UPDATE_EVERY_TIME;
// 	 settings.param.gen_int.hysteresis = BMA400_HYST_48_MG;
// 	 settings.param.gen_int.gen_int_thres = 0x10;
// 	 settings.param.gen_int.gen_int_dur = 15;
 
// 	 bma400_set_sensor_conf(&settings, 1, &bma_sensor);
 
// 	 int_en.type = BMA400_GEN1_INT_EN;
// 	 int_en.conf = BMA400_ENABLE;
 
// 	 bma400_set_power_mode(BMA400_MODE_NORMAL,&bma_sensor);
// 	 bma400_enable_interrupt(&int_en, 1, &bma_sensor);
//  }
 
//  void init_read_lp()
//  {
// 	 conf.type = BMA400_ACCEL;
// 	 int8_t rslt = bma400_get_sensor_conf(&conf, 1, &bma_sensor);
 
// 	 conf.param.accel.odr = BMA400_ODR_25HZ;
// 	 conf.param.accel.range = BMA400_RANGE_4G;
// 	 conf.param.accel.data_src = BMA400_DATA_SRC_ACCEL_FILT_1;
// 	 conf.param.accel.osr_lp = BMA400_ACCEL_OSR_SETTING_0;
// 	 conf.param.accel.int_chan = BMA400_INT_CHANNEL_1;
 
// 	 rslt = bma400_set_sensor_conf(&conf, 1, &bma_sensor);
 
// 	 int_en.type = BMA400_DRDY_INT_EN;
// 	 int_en.conf = BMA400_ENABLE;
 
// 	 bma400_set_power_mode(BMA400_MODE_LOW_POWER,&bma_sensor);
// 	 bma400_enable_interrupt(&int_en, 1, &bma_sensor);
//  }
 
 
 
//  static void timer0_handler(struct k_timer *dummy)
//  {
// 	 // set the semaphore
// 	 k_sem_give(&run_policy);
//  }
 
//  void thread_run_policy(void)
//  {
// 	 while(1)
// 	 {
// 		 k_sem_take(&run_policy, K_FOREVER); // Sleep here if semaphore is at 0
// 		 static int val_mv;
// 		 // 1. Read the ADC and convert to uJ
// 		 // LOG_INF("---------- Time: %d ----------",current_time);
// 		 int8_t err = adc_read(adc_channel.dev, &sequence);
// 		//  if (err < 0) {
// 		// 	 LOG_ERR("Could not read (%d)", err);
// 		//  }
// 		val_mv = (int)buf;
// 		adc_raw_to_millivolts_dt(&adc_channel, &val_mv);
// 		// val_mv = val_mv*4; // scale by voltage divider ratio
// 		//LOG_INF("1. Read ADC: %d mv, scaled: %d mv", val_mv, val_mv*15/10);
// 		cached_voltage_mv = (uint16_t)val_mv;
// 		if(val_mv > 1600 && last_tx_done == true)
// 		 // if(last_tx_done == true) // battery powered test, just do it every second
// 		 {
// 			 const struct device *cons = DEVICE_DT_GET(DT_NODELABEL(spi1));
// 			 pm_device_action_run(cons, PM_DEVICE_ACTION_RESUME);

// 			 int_en.type = BMA400_FIFO_WM_INT_EN;
// 			 int_en.conf = BMA400_ENABLE;
// 			 bma400_set_power_mode(BMA400_MODE_NORMAL,&bma_sensor);
// 			 bma400_enable_interrupt(&int_en, 1, &bma_sensor);
// 			 last_tx_done = false;

// 			 pm_device_action_run(cons, PM_DEVICE_ACTION_SUSPEND);
// 		 } 
// 	 }
//  }
//  K_THREAD_DEFINE(thread_run_policy_id, STACKSIZE, thread_run_policy, NULL, NULL, NULL, THREAD_RUN_POLICY_PRIORITY, 0, 0);
//  K_TIMER_DEFINE(timer0, timer0_handler, NULL);
 
 
 
 
//  int main(void)
//  {
// 	 int err;
 
// 	 // Fix the BLE address
// 	 bt_addr_le_t addr;
// 	 err = bt_addr_le_from_str("FF:EE:DD:CC:BB:AD", "random", &addr);

 
// 	 // Enable BLE
// 	 err = bt_enable(NULL);
// 	 if (err) {
// 		 LOG_ERR("Bluetooth init failed (err %d)\n", err);
// 		 return -1;
// 	 }
// 	err = bt_id_create(&addr, NULL);

// 	 if (!device_is_ready(int_pin.port)) {
// 		 return -1;
// 	 }
// 	 /* STEP 10.1 - Check if SPI and GPIO devices are ready */
// 	 err = spi_is_ready_dt(&spispec);
// 	 if (!err) {
// 		 LOG_ERR("Error: SPI device is not ready, err: %d", err);
// 		 return 0;
// 	 }
 
 
// 	 err = gpio_pin_configure_dt(&int_pin, GPIO_INPUT);
// 	 if (err < 0) {
// 		 return -1;
// 	 }
// 	 /* STEP 3 - Configure the interrupt on the button's pin */
// 	 err = gpio_pin_interrupt_configure_dt(&int_pin, GPIO_INT_EDGE_RISING);
// 	 // err = gpio_pin_interrupt_configure_dt(&int_pin, GPIO_INT_LEVEL_ACTIVE);
 
// 	 /* STEP 6 - Initialize the static struct gpio_callback variable   */
// 	 gpio_init_callback(&int_cb_data, bma_int_handler, BIT(int_pin.pin));
 
// 	 /* STEP 7 - Add the callback function by calling gpio_add_callback()   */
// 	 gpio_add_callback(int_pin.port, &int_cb_data);
 
 
// 	 sequence.buffer = &buf;
// 	 sequence.buffer_size = sizeof(buf);
 
// 	 /* STEP 3.3 - validate that the ADC peripheral (SAADC) is ready */
// 	 if (!adc_is_ready_dt(&adc_channel)) {
// 		 LOG_ERR("ADC controller devivce %s not ready", adc_channel.dev->name);
// 		 return 0;
// 	 }
// 	 /* STEP 3.4 - Setup the ADC channel */
// 	 err = adc_channel_setup_dt(&adc_channel);
// 	 if (err < 0) {
// 		 LOG_ERR("Could not setup channel #%d (%d)", 0, err);
// 		 return 0;
// 	 }
// 	 /* STEP 4.2 - Initialize the ADC sequence */
// 	 err = adc_sequence_init_dt(&adc_channel, &sequence);
// 	 if (err < 0) {
// 		 LOG_ERR("Could not initalize sequnce");
// 		 return 0;
// 	 }
 
// 	 LOG_INF("====================== APP START=======***************************");
// 	 bma400_init(&bma_sensor);
// 	 // init_read_lp();
// 	 init_fifo_watermark();
 
	 
 
// 	 const struct device *cons = DEVICE_DT_GET(DT_NODELABEL(spi1));
// 	 pm_device_action_run(cons, PM_DEVICE_ACTION_SUSPEND);
 
// 	 // Do not disable GPIO, need it for interrupt
// 	 // const struct device *cons1 = DEVICE_DT_GET(DT_NODELABEL(gpio0));
// 	 // pm_device_action_run(cons1, PM_DEVICE_ACTION_SUSPEND);
	 
// 	k_timer_start(&timer0, K_MSEC(200), K_MSEC(200));
 
// 	 while(1){
// 		 k_sleep(K_FOREVER);
// 	 }
 
// 	 return 0;
//  }
















// /*
//  * Copyright (c) 2024 Nordic Semiconductor ASA
//  *
//  * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
//  */

// #include "bma400.h"
// #include "bma400_defs.h"
// #include <zephyr/device.h>
// #include <zephyr/devicetree.h>
// #include <zephyr/drivers/gpio.h>
// #include <zephyr/drivers/spi.h>
// #include <zephyr/kernel.h>
// #include <zephyr/logging/log.h>
// #include <zephyr/pm/device.h>

// #include "run_nn.h"

// #include <zephyr/bluetooth/addr.h>
// #include <zephyr/bluetooth/bluetooth.h>
// #include <zephyr/drivers/adc.h>

// LOG_MODULE_REGISTER(app, LOG_LEVEL_DBG);

// #define DEVICE_NAME CONFIG_BT_DEVICE_NAME
// #define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

// /* STEP 2.1 - Declare the Company identifier (Company ID) */
// #define COMPANY_ID_CODE 0x0059

// int16_t buf;

// bool last_tx_done = true;

// /* STEP 2.2 - Declare the structure for your custom data */
// typedef struct __attribute__((packed)) adv_mfg_data {
//  uint16_t company_code; /* Company Identifier Code. */
//  uint8_t pred;
//  int16_t x;
//  int16_t y;
//  int16_t z;
//  uint16_t voltage_mv;
// } adv_mfg_data_type;

// struct adc_sequence sequence;

// static const struct adc_dt_spec adc_channel = ADC_DT_SPEC_GET(DT_PATH(zephyr_user));

// static const struct bt_le_adv_param* adv_param =
//  BT_LE_ADV_PARAM(BT_LE_ADV_OPT_USE_IDENTITY, /* No options specified */
//  32, /* Min Advertising Interval 250ms (400*0.625ms) */
//  33, /* Max Advertising Interval 250.625ms (401*0.625ms) */
//  NULL); /* Set to NULL for undirected advertising */
// /* STEP 2.3 - Define and initialize a variable of type adv_mfg_data_type */
// static adv_mfg_data_type adv_mfg_data = {
//  .company_code = COMPANY_ID_CODE, .pred = 0x0, .x = 0, .y = 0, .z = 0, .voltage_mv = 0xFFFFU};

// static uint16_t cached_voltage_mv = 0xFFFFU;

// static const struct bt_data ad[] = {
//  /* STEP 3 - Include the Manufacturer Specific Data in the advertising packet. */
//  BT_DATA(BT_DATA_MANUFACTURER_DATA, (unsigned char*)&adv_mfg_data, sizeof(adv_mfg_data)),
// };
// // threads
// #define STACKSIZE 1024
// #define THREAD_READ_BMA_PRIORITY 7
// #define THREAD_RUN_POLICY_PRIORITY 8
// K_SEM_DEFINE(bma400_ready, 0, 1);
// K_SEM_DEFINE(run_policy, 0, 1);

// // SPI
// #define SPIOP SPI_WORD_SET(8) | SPI_TRANSFER_MSB
// struct spi_dt_spec spispec = SPI_DT_SPEC_GET(DT_NODELABEL(bma400), SPIOP, 0);
// uint8_t rx_buffer[128] = {0};

// // interrupt GPIO
// #define int_NODE DT_ALIAS(int1)
// static const struct gpio_dt_spec int_pin = GPIO_DT_SPEC_GET(int_NODE, gpios);
// static struct gpio_callback int_cb_data;

// // BMA400
// #define BMA400_REG_FIFO_CONFIG_1 UINT8_C(0x27)
// #define FIFOINTER 3
// #define FIFO_SAMPLES 25 // number of samples for fifo content
// #define FIFO_WATERMARK_LEVEL UINT16_C(FIFO_SAMPLES * 4) // 4 bytes per frame (XYZ+header)
// #define FIFO_FULL_SIZE UINT16_C(1024)
// #define FIFO_SIZE (FIFO_FULL_SIZE + BMA400_FIFO_BYTES_OVERREAD)
// #define FIFO_ACCEL_FRAME_COUNT UINT8_C(FIFO_SAMPLES)

// /* Sliding-window inference controls:
//  * - Keep NN window fixed at 25 samples (model requirement).
//  * - Infer every NN_INFER_STRIDE_SAMPLES new samples after warm-up.
//  * Set to 25 for ~1.0 s cadence at 25 Hz (old behavior).
//  */
// #define NN_WINDOW_SAMPLES 25
// #define NN_INFER_STRIDE_SAMPLES 25
// #define TX_CONFIDENCE_THRESHOLD 0.10f

// BMA400_INTF_RET_TYPE read_reg_spi(uint8_t reg_address, uint8_t* data, uint32_t len, void* intf_ptr);
// BMA400_INTF_RET_TYPE write_reg_spi(uint8_t reg_address, const uint8_t* data, uint32_t len,
//  void* intf_ptr);
// void bma400_delay_us(uint32_t period, void* intf_ptr) {
//  k_usleep(period);
// }

// static uint8_t dev_addr = 31;
// struct bma400_dev bma_sensor = {.intf = BMA400_SPI_INTF,
//  .intf_ptr = &dev_addr,
//  .read = read_reg_spi,
//  .write = write_reg_spi,
//  .delay_us = bma400_delay_us,
//  .read_write_len = 8};

// struct bma400_sensor_data acc_data;

// struct bma400_int_enable int_en;
// struct bma400_fifo_data fifo_frame;
// struct bma400_device_conf fifo_conf;
// struct bma400_sensor_conf conf;
// uint8_t fifo_buff[FIFO_SIZE] = {0};

// struct bma400_fifo_sensor_data accel_data[FIFO_ACCEL_FRAME_COUNT] = {{0}};
// struct bma400_sensor_conf settings;

// static struct bma400_fifo_sensor_data nn_ring[NN_WINDOW_SAMPLES] = {{0}};
// static struct bma400_fifo_sensor_data nn_window[NN_WINDOW_SAMPLES] = {{0}};
// static uint16_t nn_ring_write_idx = 0;
// static uint16_t nn_ring_count = 0;
// static uint16_t nn_samples_since_infer = 0;
// static int last_sent_pred = -1;

// static void nn_ring_push_sample(const struct bma400_fifo_sensor_data* sample) {
//  nn_ring[nn_ring_write_idx] = *sample;
//  nn_ring_write_idx = (nn_ring_write_idx + 1U) % NN_WINDOW_SAMPLES;
//  if (nn_ring_count < NN_WINDOW_SAMPLES) {
//  nn_ring_count++;
//  }
// }

// static void nn_ring_copy_window(struct bma400_fifo_sensor_data* out_window) {
//  uint16_t start_idx =
//  (nn_ring_write_idx + NN_WINDOW_SAMPLES - nn_ring_count) % NN_WINDOW_SAMPLES;
//  for (uint16_t i = 0; i < nn_ring_count; i++) {
//  out_window[i] = nn_ring[(start_idx + i) % NN_WINDOW_SAMPLES];
//  }
// }

// void bma_int_handler(const struct device* dev, struct gpio_callback* cb, uint32_t pins) {
//  // set the semaphore
//  k_sem_give(&bma400_ready);
// }

// void thread_read_bma400(void) {
//  while (1) {
//  k_sem_take(&bma400_ready, K_FOREVER); // Sleep here if semaphore is at 0

//  // Enable SPI
//  const struct device* cons = DEVICE_DT_GET(DT_NODELABEL(spi1));
//  pm_device_action_run(cons, PM_DEVICE_ACTION_RESUME);

//  // read data from bma400 fifo
//  bma400_get_fifo_data(&fifo_frame, &bma_sensor);
//  uint16_t accel_frames_req = FIFO_SAMPLES;
//  bma400_extract_accel(&fifo_frame, accel_data, &accel_frames_req, &bma_sensor);
//  // LOG_INF("Read FIFO Data, disabling BMA");

//  // after reading, disable the interrupt and put the bma400 to sleep
//  int_en.type = BMA400_FIFO_WM_INT_EN;
//  int_en.conf = BMA400_DISABLE;
//  int8_t rslt = bma400_enable_interrupt(&int_en, 1, &bma_sensor);
//  bma400_set_power_mode(BMA400_MODE_SLEEP, &bma_sensor);

//  // Disable SPI
//  pm_device_action_run(cons, PM_DEVICE_ACTION_SUSPEND);

//  if (accel_frames_req == 0) {
//  last_tx_done = true;
//  continue;
//  }

//  uint16_t sample_count = accel_frames_req;
//  if (sample_count > FIFO_ACCEL_FRAME_COUNT) {
//  sample_count = FIFO_ACCEL_FRAME_COUNT;
//  }

//  uint16_t prior_ring_count = nn_ring_count;
//  for (uint16_t i = 0; i < sample_count; i++) {
//  nn_ring_push_sample(&accel_data[i]);
//  }

//  bool should_infer = false;
//  if ((prior_ring_count < NN_WINDOW_SAMPLES) && (nn_ring_count == NN_WINDOW_SAMPLES)) {
//  should_infer = true; // first full 25-sample window (about 1 s at 25 Hz)
//  nn_samples_since_infer = 0;
//  } else if (nn_ring_count == NN_WINDOW_SAMPLES) {
//  nn_samples_since_infer += sample_count;
//  if (nn_samples_since_infer >= NN_INFER_STRIDE_SAMPLES) {
//  should_infer = true;
//  nn_samples_since_infer = 0;
//  }
//  }

//  if (!should_infer) {
//  last_tx_done = true;
//  continue;
//  }

//  nn_ring_copy_window(nn_window);
//  run_nn_infer(nn_window, NN_WINDOW_SAMPLES);

//  bool class_changed = (biggest_idx != last_sent_pred);
//  bool high_confidence = (biggest_score >= TX_CONFIDENCE_THRESHOLD);
//  if (class_changed || high_confidence) {
//  const struct bma400_fifo_sensor_data* latest = &nn_window[NN_WINDOW_SAMPLES - 1];

//  adv_mfg_data.pred = (uint8_t)biggest_idx;
//  adv_mfg_data.x = latest->x;
//  adv_mfg_data.y = latest->y;
//  adv_mfg_data.z = latest->z;
//  adv_mfg_data.voltage_mv = cached_voltage_mv;

//  bt_le_adv_update_data(ad, ARRAY_SIZE(ad), NULL, 0); // update adv data
//  bt_le_adv_start(adv_param, ad, ARRAY_SIZE(ad), NULL, 0); // start advertising
//  k_sleep(K_MSEC(18)); // wait at least one cycle
//  bt_le_adv_stop(); // stop advertising

//  last_sent_pred = biggest_idx;
//  }

//  last_tx_done = true;
//  }
// }

// // Need to make sure stack is big enough to run NN code
// K_THREAD_DEFINE(thread_read_bma400_id, 8192, thread_read_bma400, NULL, NULL, NULL,
//  THREAD_READ_BMA_PRIORITY, 0, 0);

// BMA400_INTF_RET_TYPE read_reg_spi(uint8_t reg_address, uint8_t* data, uint32_t len,
//  void* intf_ptr) {
//  int err;

//  /* STEP 4.1 - Set the transmit and receive buffers */
//  // When reading the BMA400, the first byte read is a dummy, so we need to read two bytes and
//  // interpret the second one For a transceive there are 3 steps:
//  // | step 1 | step 2 | step 3
//  // Master | 1[7 bit reg address] | 0x0 | 0x0
//  // Slave | dummy | dummy | data from sensor
//  // therefore, if we want to read 1 byte from the sensor, we need to read 3 bytes from the sensor
//  // (1 during send, 2 during read) Since the BMA400 API already adds the dummy byte, we only need
//  // to add one more byte This extra byte is because the first read happens during the register
//  // write, so we need to read again

//  uint8_t tx_buffer = reg_address;
//  struct spi_buf tx_spi_buf = {.buf = (void*)&tx_buffer, .len = 1};
//  struct spi_buf_set tx_spi_buf_set = {.buffers = &tx_spi_buf, .count = 1};
//  struct spi_buf rx_spi_bufs = {.buf = rx_buffer, .len = len + 1};
//  struct spi_buf_set rx_spi_buf_set = {.buffers = &rx_spi_bufs, .count = 1};

//  /* STEP 4.2 - Call the transceive function */
//  err = spi_transceive_dt(&spispec, &tx_spi_buf_set, &rx_spi_buf_set);
//  if (err < 0) {
//  LOG_ERR("spi_transceive_dt() failed, err: %d, 0x%02X", err, tx_buffer);
//  // return err;
//  }

//  for (int i = 0; i < len; i++) {
//  data[i] = rx_buffer[i + 1]; // data[0] = dummy byte, data[1] = data
//  }

//  return 0;
// }

// BMA400_INTF_RET_TYPE write_reg_spi(uint8_t reg_address, const uint8_t* data, uint32_t len,
//  void* intf_ptr) {
//  int err;

//  /* STEP 5.1 - delcare a tx buffer having register address and data */
//  // When writing to the BMA400, the first byte read is an adress, so we need to write two bytes
//  // For a transceive there are 2 steps:
//  // | step 1 | step 2 |
//  // Master | 1[7 bit reg address] | val |
//  // Slave | dummy | dummy |
//  // therefore, if we want to write 1 byte to the sensor, we need to write 2 bytes from the sensor
//  // (1 adress, 1 data)
//  uint8_t tx_buf[2] = {reg_address, data[0]}; // to write, set the MSB to 0
//  struct spi_buf tx_spi_buf = {.buf = tx_buf, .len = len + 1};
//  struct spi_buf_set tx_spi_buf_set = {.buffers = &tx_spi_buf, .count = 1};

//  /* STEP 5.2 - call the spi_write_dt function with SPISPEC to write buffers */
//  err = spi_write_dt(&spispec, &tx_spi_buf_set);
//  if (err < 0) {
//  LOG_ERR("spi_write_dt() failed, err %d", err);
//  return err;
//  }

//  return 0;
// }

// void init_fifo_watermark() {
//  conf.type = BMA400_ACCEL;
//  int8_t rslt = bma400_get_sensor_conf(&conf, 1, &bma_sensor);

//  conf.param.accel.odr = BMA400_ODR_25HZ;
//  conf.param.accel.range = BMA400_RANGE_4G;
//  conf.param.accel.data_src = BMA400_DATA_SRC_ACCEL_FILT_1;

//  rslt = bma400_set_sensor_conf(&conf, 1, &bma_sensor);

//  fifo_conf.type = BMA400_FIFO_CONF;

//  rslt = bma400_get_device_conf(&fifo_conf, 1, &bma_sensor);

//  fifo_conf.param.fifo_conf.conf_regs = BMA400_FIFO_8_BIT_EN | BMA400_FIFO_X_EN |
//  BMA400_FIFO_Y_EN | BMA400_FIFO_Z_EN |
//  BMA400_FIFO_AUTO_FLUSH; // flush on power mode change
//  fifo_conf.param.fifo_conf.conf_status = BMA400_ENABLE;
//  fifo_conf.param.fifo_conf.fifo_watermark = FIFO_WATERMARK_LEVEL;
//  fifo_conf.param.fifo_conf.fifo_wm_channel = BMA400_INT_CHANNEL_1;

//  rslt = bma400_set_device_conf(&fifo_conf, 1, &bma_sensor);

//  fifo_frame.data = fifo_buff;
//  fifo_frame.length = FIFO_SIZE;

//  int_en.type = BMA400_FIFO_WM_INT_EN;
//  int_en.conf = BMA400_DISABLE;

//  bma400_set_power_mode(BMA400_MODE_LOW_POWER, &bma_sensor);
//  rslt = bma400_enable_interrupt(&int_en, 1, &bma_sensor);
// }

// void init_activity() {
//  settings.type = BMA400_GEN1_INT;
//  bma400_get_sensor_conf(&settings, 1, &bma_sensor);

//  settings.param.gen_int.int_chan = BMA400_INT_CHANNEL_1;
//  settings.param.gen_int.axes_sel = BMA400_AXIS_XYZ_EN;
//  settings.param.gen_int.data_src = BMA400_DATA_SRC_ACC_FILT2;
//  settings.param.gen_int.criterion_sel = BMA400_ACTIVITY_INT;
//  settings.param.gen_int.evaluate_axes = BMA400_ANY_AXES_INT;
//  settings.param.gen_int.ref_update = BMA400_UPDATE_EVERY_TIME;
//  settings.param.gen_int.hysteresis = BMA400_HYST_48_MG;
//  settings.param.gen_int.gen_int_thres = 0x10;
//  settings.param.gen_int.gen_int_dur = 15;

//  bma400_set_sensor_conf(&settings, 1, &bma_sensor);

//  int_en.type = BMA400_GEN1_INT_EN;
//  int_en.conf = BMA400_ENABLE;

//  bma400_set_power_mode(BMA400_MODE_NORMAL, &bma_sensor);
//  bma400_enable_interrupt(&int_en, 1, &bma_sensor);
// }

// void init_read_lp() {
//  conf.type = BMA400_ACCEL;
//  int8_t rslt = bma400_get_sensor_conf(&conf, 1, &bma_sensor);

//  conf.param.accel.odr = BMA400_ODR_25HZ;
//  conf.param.accel.range = BMA400_RANGE_4G;
//  conf.param.accel.data_src = BMA400_DATA_SRC_ACCEL_FILT_1;
//  conf.param.accel.osr_lp = BMA400_ACCEL_OSR_SETTING_0;
//  conf.param.accel.int_chan = BMA400_INT_CHANNEL_1;

//  rslt = bma400_set_sensor_conf(&conf, 1, &bma_sensor);

//  int_en.type = BMA400_DRDY_INT_EN;
//  int_en.conf = BMA400_ENABLE;

//  bma400_set_power_mode(BMA400_MODE_LOW_POWER, &bma_sensor);
//  bma400_enable_interrupt(&int_en, 1, &bma_sensor);
// }

// static void timer0_handler(struct k_timer* dummy) {
//  // set the semaphore
//  k_sem_give(&run_policy);
// }

// void thread_run_policy(void) {
//  while (1) {
//  k_sem_take(&run_policy, K_FOREVER); // Sleep here if semaphore is at 0
//  static int val_mv;
//  // 1. Read the ADC and convert to uJ
//  // LOG_INF("---------- Time: %d ----------",current_time);
//  int8_t err = adc_read(adc_channel.dev, &sequence);
//  if (err < 0) {
//  LOG_ERR("Could not read (%d)", err);
//  }
//  val_mv = (int)buf;
//  err = adc_raw_to_millivolts_dt(&adc_channel, &val_mv);
//  // val_mv = val_mv*4; // scale by voltage divider ratio
//  cached_voltage_mv = (uint16_t)val_mv;
//  // LOG_INF("1. Read ADC: %d mv, scaled: %d mv", val_mv, val_mv * 15 / 10);
//  if (val_mv > 1600 && last_tx_done == true) {
//  const struct device* cons = DEVICE_DT_GET(DT_NODELABEL(spi1));
//  pm_device_action_run(cons, PM_DEVICE_ACTION_RESUME);

//  int_en.type = BMA400_FIFO_WM_INT_EN;
//  int_en.conf = BMA400_ENABLE;
//  bma400_set_power_mode(BMA400_MODE_NORMAL, &bma_sensor);
//  bma400_enable_interrupt(&int_en, 1, &bma_sensor);
//  last_tx_done = false;

//  pm_device_action_run(cons, PM_DEVICE_ACTION_SUSPEND);
//  }
//  // // continue;
//  // uint16_t energy_val = 5*val_mv*val_mv/1000000 - 41;
//  // LOG_INF("1. Read ADC: %d mv, %d uJ", val_mv, energy_val);
//  // // 2. write to buffer
//  // if(buffer_idx == 5)
//  // {
//  // // When the buffer is full, we shift everything left by one, and set the last index as
//  // newest val for(int buf_i = 0; buf_i < 4; buf_i++)
//  // {
//  // energy_vals[buf_i] = energy_vals[buf_i+1];
//  // }
//  // energy_vals[4] = energy_val;
//  // // if we just sent, wait an iteration
//  // if(first_e_harvest == 1)
//  // {
//  // first_e_harvest = 0;
//  // }
//  // else
//  // {
//  // if(just_filled == 1)
//  // {
//  // // e_h[4] = e[5]-e[4] (e[4]-e[3])
//  // energy_harvested[buffer_idx-1] = energy_vals[4] - energy_vals[3];
//  // }
//  // else
//  // {
//  // for(int buf_i = 0; buf_i < 4; buf_i++)
//  // {
//  // energy_harvested[buf_i] = energy_harvested[buf_i+1];
//  // }
//  // energy_harvested[buffer_idx-1] = energy_vals[4] - energy_vals[3];
//  // }
//  // }

//  // }
//  // else
//  // {
//  // if(first_e_harvest == 1)
//  // {
//  // first_e_harvest = 0;
//  // }
//  // else
//  // {
//  // // e_h[0] = e[1]-e[0], e_h[1] = e[2]-e[1], e_h[2] = e[3]-e[2], e_h[3] = e[4]-e[3]
//  // energy_harvested[buffer_idx-1] = energy_vals[buffer_idx] -
//  // energy_vals[buffer_idx-1];
//  // }
//  // energy_vals[buffer_idx] = energy_val;
//  // buffer_idx += 1;
//  // if(buffer_idx == 5)
//  // {
//  // just_filled = 1;
//  // }
//  // }
//  // // LOG_INF("2. Latest Energy: %d, %d, %d, %d, %d. Latest Harvested: %d, %d, %d, %d, %d",
//  // energy_vals[0], energy_vals[1], energy_vals[2], energy_vals[3], energy_vals[4],
//  // energy_harvested[0], energy_harvested[1], energy_harvested[2], energy_harvested[3],
//  // energy_harvested[4]);
//  // // adv_mfg_data.cap_mv = val_mv;
//  // // adv_mfg_data.cap_mv = val_mv;
//  // // 3. Get average energy harvested
//  // uint16_t avg_energy_harvested = 0;
//  // // implicitly divide by 1 since one second, this estimates uW or uJ harvested per second
//  // for(int buf_i = 0; buf_i < 4; buf_i++)
//  // {
//  // avg_energy_harvested += energy_harvested[buf_i];
//  // }
//  // // LOG_INF("3. Avg Energy Harvested (average power): %d uW", avg_energy_harvested);
//  // // 4. compute policy
//  // uint16_t thresh = theta_e*energy_vals[4] + theta_p*avg_energy_harvested;
//  // // LOG_INF("4. Run Policy.");
//  // // LOG_INF("\t Current E: %d > 100?",energy_vals[4]);
//  // LOG_INF("\t Current time - last sent: %d - %d = %d", current_time, last_send_time,
//  // current_time - last_send_time);
//  // // LOG_INF("\t tau: %d",thresh);
//  // // if( (energy_vals[4] > 100) && ( (current_time - last_send_time) > thresh) &&
//  // (read_once == 1) )
//  // // if(read_once == 1)
//  // if( (4300 < val_mv) && (can_read)) // 4.5V
//  // {
//  // // to_send_flag = 1;
//  // // trigger bma to start reading
//  // int_en.type = BMA400_FIFO_WM_INT_EN;
//  // int_en.conf = BMA400_ENABLE;
//  // bma400_set_power_mode(BMA400_MODE_NORMAL,&bma_sensor);
//  // bma400_enable_interrupt(&int_en, 1, &bma_sensor);
//  // read_once = 0;
//  // can_read = 0; // can only read after last packet sent
//  // }
//  // current_time += 1;
//  }
// }
// K_THREAD_DEFINE(thread_run_policy_id, STACKSIZE, thread_run_policy, NULL, NULL, NULL,
//  THREAD_RUN_POLICY_PRIORITY, 0, 0);
// K_TIMER_DEFINE(timer0, timer0_handler, NULL);

// int main(void) {
//  int err;

//  // Fix the BLE address
//  bt_addr_le_t addr;
//  err = bt_addr_le_from_str("FF:EE:DD:CC:BB:AD", "random", &addr);
//  err = bt_id_create(&addr, NULL);

//  // Enable BLE
//  err = bt_enable(NULL);
//  if (err) {
//  LOG_ERR("Bluetooth init failed (err %d)\n", err);
//  return -1;
//  }

//  /* STEP 10.1 - Check if SPI and GPIO devices are ready */
//  err = spi_is_ready_dt(&spispec);
//  if (!err) {
//  LOG_ERR("Error: SPI device is not ready, err: %d", err);
//  return 0;
//  }

//  if (!device_is_ready(int_pin.port)) {
//  return -1;
//  }

//  err = gpio_pin_configure_dt(&int_pin, GPIO_INPUT);
//  if (err < 0) {
//  return -1;
//  }
//  /* STEP 3 - Configure the interrupt on the button's pin */
//  err = gpio_pin_interrupt_configure_dt(&int_pin, GPIO_INT_EDGE_RISING);
//  // err = gpio_pin_interrupt_configure_dt(&int_pin, GPIO_INT_LEVEL_ACTIVE);

//  /* STEP 6 - Initialize the static struct gpio_callback variable */
//  gpio_init_callback(&int_cb_data, bma_int_handler, BIT(int_pin.pin));

//  /* STEP 7 - Add the callback function by calling gpio_add_callback() */
//  gpio_add_callback(int_pin.port, &int_cb_data);

//  sequence.buffer = &buf;
//  sequence.buffer_size = sizeof(buf);

//  /* STEP 3.3 - validate that the ADC peripheral (SAADC) is ready */
//  if (!adc_is_ready_dt(&adc_channel)) {
//  LOG_ERR("ADC controller devivce %s not ready", adc_channel.dev->name);
//  return 0;
//  }
//  /* STEP 3.4 - Setup the ADC channel */
//  err = adc_channel_setup_dt(&adc_channel);
//  if (err < 0) {
//  LOG_ERR("Could not setup channel #%d (%d)", 0, err);
//  return 0;
//  }
//  /* STEP 4.2 - Initialize the ADC sequence */
//  err = adc_sequence_init_dt(&adc_channel, &sequence);
//  if (err < 0) {
//  LOG_ERR("Could not initalize sequnce");
//  return 0;
//  }

//  LOG_INF("====================== APP START=======***************************");
//  bma400_init(&bma_sensor);
//  // init_read_lp();
//  init_fifo_watermark();

//  const struct device* cons = DEVICE_DT_GET(DT_NODELABEL(spi1));
//  pm_device_action_run(cons, PM_DEVICE_ACTION_SUSPEND);

//  // Do not disable GPIO, need it for interrupt
//  // const struct device *cons1 = DEVICE_DT_GET(DT_NODELABEL(gpio0));
//  // pm_device_action_run(cons1, PM_DEVICE_ACTION_SUSPEND);

//  k_timer_start(&timer0, K_MSEC(1000), K_MSEC(1000));

//  while (1) {
//  k_sleep(K_FOREVER);
//  }

//  return 0;
// }



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
 
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/gap.h>
#include <bluetooth/scan.h>
 
LOG_MODULE_REGISTER(app, LOG_LEVEL_DBG);
 
 
static void scan_filter_match(struct bt_scan_device_info *device_info,
                              struct bt_scan_filter_match *filter_match,
                              bool connectable)
{
    struct bt_data *ad;
    int ad_len = device_info->adv_data->len;
    
    LOG_HEXDUMP_INF(&device_info->adv_data->data[4],
                    ad_len - 4,
                    "DATA");
}
 
BT_SCAN_CB_INIT(scan_cb, scan_filter_match, NULL, NULL, NULL);
 
 
static void scan_init(void)
{
    int err;
 
    /* Use active scanning and disable duplicate filtering to handle any
     * devices that might update their advertising data at runtime. */
    struct bt_le_scan_param scan_param = {
        .type     = BT_LE_SCAN_TYPE_PASSIVE,
        .interval = BT_GAP_SCAN_FAST_INTERVAL, // 5ms
        .window   = BT_GAP_SCAN_FAST_INTERVAL, // 2.5ms
        .options  = BT_LE_SCAN_OPT_NONE
    };
 
    struct bt_scan_init_param scan_init = {
        .connect_if_match = 0,
        .scan_param = &scan_param,
        .conn_param = NULL
    };
 
    bt_scan_init(&scan_init);
    bt_scan_cb_register(&scan_cb);
 
    bt_addr_le_t addr;
    err = bt_addr_le_from_str("FF:EE:DD:CC:BB:AD", "random", &addr);
    err = bt_scan_filter_add(BT_SCAN_FILTER_TYPE_ADDR, &addr);
    if (err) {
        LOG_INF("Scanning filters cannot be set (err %d)\n", err);
        return;
    }
 
    err = bt_scan_filter_enable(BT_SCAN_ADDR_FILTER, false);
    if (err) {
        LOG_INF("Filters cannot be turned on (err %d)\n", err);
    }
 
    bt_scan_start(BT_LE_SCAN_TYPE_PASSIVE);
}
 
 
 
int main(void)
{
    LOG_INF("Application Started ====================");
    int err;
 
    err = bt_enable(NULL);
    if (err) {
        LOG_ERR("Bluetooth init failed (err %d)\n", err);
        return -1;
    }
    LOG_INF("==================== BT INITIALIZED");
   
 
    scan_init();
 
    while(1){
        k_sleep(K_FOREVER);
    }
 
    return 0;
}