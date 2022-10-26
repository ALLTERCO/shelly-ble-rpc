#include "mgos.h"

// strictly esp32
#include "bootloader_common.h"
#include "driver/gpio.h"
#include "esp_ota_ops.h"
#include "esp_sleep.h"

#include "btchannel.h"
#include "shellyrpc.h"

static RTC_DATA_ATTR int s_btn_pin = -1;
static RTC_DATA_ATTR bool s_btn_pull_up = false;

static RTC_DATA_ATTR int s_led_pin = -1;

static void deep_sleep(const uint8_t btn_pin) {
  const esp_partition_t *current_boot_partition = esp_ota_get_boot_partition();
  esp_partition_pos_t partition_pos;
  partition_pos.offset = current_boot_partition->address;
  partition_pos.size = current_boot_partition->size;
  bootloader_common_update_rtc_retain_mem(&partition_pos, true);

  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_ON);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_ON);
  esp_sleep_enable_ext0_wakeup(btn_pin, 0);
  esp_deep_sleep_start();
}

static void bt_channel_cb(int ev, void *evd, void *user_data UNUSED_ARG) {
  switch (ev) {
    case SHELLY_BT_STARTED: {
      bt_connect_shelly(mgos_sys_config_get_shelly_btname());
      break;
    }
    case SHELLY_BT_CONNECTED: {
      bt_channel_send bt_send_fn = evd;
      uint8_t output_id = mgos_sys_config_get_shelly_channel();
      struct mbuf jsmb;
      get_rpc_json_output_toggle(&jsmb, output_id);
      struct mg_str shelly_rpc_message = {.p = jsmb.buf, .len = jsmb.len};
      bt_send_fn(shelly_rpc_message);
      mbuf_free(&jsmb);
      break;
    }
    case SHELLY_BT_SEND_SUCCESS: {
      LOG(LL_INFO, ("Message sent"));
      deep_sleep(s_btn_pin);
      break;
    }
    case SHELLY_BT_DISCONNECTED:
    case SHELLY_BT_FAIL: {
      LOG(LL_INFO, ("We're done. Going to sleep"));
      deep_sleep(s_btn_pin);
      break;
    }
  }
}

enum mgos_app_init_result mgos_app_init(void) {
  mgos_event_add_group_handler(SHELLY_BT_CONN_EVENT_BASE, bt_channel_cb, NULL);
  bt_channel_init();

#ifndef MGOS_CONFIG_HAVE_BOARD_BTN
#error We need a button for waking up
#endif

  // Buttons
  if (s_btn_pin < 0) {
    s_btn_pin = mgos_sys_config_get_board_btn_pin();
    s_btn_pull_up = mgos_sys_config_get_board_btn_pull_up();
  }
  if (s_btn_pin >= 0) {
    mgos_gpio_set_pull(s_btn_pin,
                       s_btn_pull_up ? MGOS_GPIO_PULL_UP : MGOS_GPIO_PULL_DOWN);
  }

#ifdef MGOS_CONFIG_HAVE_BOARD_LED
  // Board leds, keep them off and don't use
  if (s_led_pin < 0) {
    s_led_pin = mgos_sys_config_get_board_led_pin();
  }
  if (s_led_pin >= 0) {
    mgos_gpio_setup_output(s_led_pin, false);
    gpio_hold_en(s_led_pin);
    gpio_deep_sleep_hold_en();
  }
#endif

  return MGOS_APP_INIT_SUCCESS;
}
