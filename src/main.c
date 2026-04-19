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
    // Advertising data is in device_info->adv_data
    struct bt_data *ad;
    int ad_len = device_info->adv_data->len;
 
    // LOG_INF("Advertising data (%d bytes): ", ad_len);
    uint8_t pred = device_info->adv_data->data[4];
    // LOG_INF("%02X%02X ", device_info->adv_data->data[5],device_info->adv_data->data[4]);
    if(pred == 0)
    {
        LOG_INF("Idle");
    }
    else if(pred == 1)
    {
        LOG_INF("Jump");
    }
    else if(pred == 2)
    {
        LOG_INF("Lunge");
    }
    else if(pred == 3)
    {
        LOG_INF("Squat");
    }
    else if(pred == 4)
    {
        LOG_INF("Walk");
    }
    else if(pred == 4)
    {
        LOG_INF("Walk");
    }
    else if(pred == 4)
    {
        LOG_INF("Walk");
    }
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