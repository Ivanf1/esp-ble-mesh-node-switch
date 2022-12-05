#include <stdio.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "iot_button.h"

#define TAG "BUTTON"

#define BUTTON_IO_NUM 33
#define BUTTON_ACTIVE_LEVEL 0

extern void ble_mesh_send_gen_onoff_set(void);

static void button_tap_cb(void *arg) {
  ESP_LOGI(TAG, "tap cb (%s)", (char *)arg);

  ble_mesh_send_gen_onoff_set();
}

void board_button_init(void) {
  button_handle_t btn_handle = iot_button_create(BUTTON_IO_NUM, BUTTON_ACTIVE_LEVEL);
  if (btn_handle) {
    iot_button_set_evt_cb(btn_handle, BUTTON_CB_RELEASE, button_tap_cb, "RELEASE");
  }
}
