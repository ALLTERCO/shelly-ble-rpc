#include "mgos.h"
#include "mgos_bt.h"
#include "mgos_bt_gap.h"
#include "mgos_bt_gattc.h"

#include "host/ble_gap.h"
#include "host/ble_gatt.h"

// strictly esp32
#include "bootloader_common.h"
#include "driver/gpio.h"
#include "esp_ota_ops.h"
#include "esp_sleep.h"

#include "shellyrpc.h"

//  Time in millisecons for scanning
const uint16_t kBtScanPeriod = 800;
//  Time in milliseconds after which the connection is closed
const uint16_t kBtConnectTimeout = 4500;
//  Number of tries to scan for a Shelly
const uint8_t kBtMaxScanRepeat = 3;

//  Size of the characteristics string representation buffer
enum { kChar128BitStringLength = 37 };

//  Shelly BT specidic UUIDs
const char kShellyGattServiceUUID[] = "5f6d4f53-5f52-5043-5f53-56435f49445f";
const char kShellyDataChrUUID[] = "5f6d4f53-5f52-5043-5f64-6174615f5f5f";
const char kShellyTxChrUUID[] = "5f6d4f53-5f52-5043-5f74-785f63746c5f";
const char kShellyRxChrUUID[] = "5f6d4f53-5f52-5043-5f72-785f63746c5f";

static struct mgos_bt_addr s_shelly_bt_address;
static struct mgos_bt_gattc_connect_arg s_shelly_bt_connection;

static RTC_DATA_ATTR uint16_t s_data_chr_handle;
static RTC_DATA_ATTR uint16_t s_tx_chr_handle;
static RTC_DATA_ATTR uint16_t s_rx_chr_handle;

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

static void toggle_shelly_output(const uint16_t conn_id,
                                 const uint16_t data_chr_handle,
                                 const uint16_t tx_chr_handle,
                                 const uint8_t output_id) {
  if (s_shelly_bt_connection.ok == false) return;
  struct mbuf jsmb;
  get_rpc_json_output_toggle(&jsmb, output_id);

  struct mg_str shelly_rpc_command = {.p = jsmb.buf, .len = jsmb.len};

  uint32_t command_size = htonl(shelly_rpc_command.len);
  struct mg_str command_size_buf = {.p = (char *) &command_size,
                                    .len = sizeof(command_size)};
  mgos_bt_gattc_write(conn_id, tx_chr_handle, command_size_buf, false);
  // ask for being informed of the write succeeding
  mgos_bt_gattc_write(conn_id, data_chr_handle, shelly_rpc_command, true);

  mbuf_free(&jsmb);
}

static bool start_ble_scan(void) {
  struct mgos_bt_gap_scan_opts bt_scan_options = {.duration_ms = kBtScanPeriod,
                                                  .active = false};
  return mgos_bt_gap_scan(&bt_scan_options);
}

static void gattc_cb(int ev, UNUSED_ARG void *evd, UNUSED_ARG void *user_data) {
  static mgos_timer_id disconnect_timer;

  switch (ev) {
    case MGOS_BT_GATTC_EV_CONNECT: {
      LOG(LL_INFO, ("GATTC: BLE connected"));

      s_shelly_bt_connection = *((struct mgos_bt_gattc_connect_arg *) evd);

      if (s_shelly_bt_connection.ok == false) goto skip_and_close;

      if (s_data_chr_handle > 0 && s_tx_chr_handle > 0) {
        LOG(LL_INFO, ("Characteristics handles preserved over sleep"));
        toggle_shelly_output(s_shelly_bt_connection.conn.conn_id,
                             s_data_chr_handle, s_tx_chr_handle, 0);
      } else {
        LOG(LL_INFO, ("Discover characteristics handles"));
        if (!mgos_bt_gattc_discover(s_shelly_bt_connection.conn.conn_id)) {
          goto skip_and_close;
        }
      }

      // gcc allows for this
      void disconnect(UNUSED_ARG void *arg) {
        mgos_bt_gattc_disconnect(s_shelly_bt_connection.conn.conn_id);
      }
      disconnect_timer = mgos_set_timer(kBtConnectTimeout, 0, disconnect, NULL);

      break;
    }
    case MGOS_BT_GATTC_EV_DISCONNECT: {
      LOG(LL_INFO, ("GATTC: BLE disconnected"));
    skip_and_close : { deep_sleep(s_btn_pin); } break;
    }
    case MGOS_BT_GATTC_EV_DISCOVERY_RESULT: {
      struct mgos_bt_gattc_discovery_result_arg *discovery_result =
          (struct mgos_bt_gattc_discovery_result_arg *) evd;

      // to retrieve service uuid
      // char service_UUID_str[kChar128BitStringLength] = {0};
      // mgos_bt_uuid_to_str(&(discovery_result->svc), service_UUID);
      char char_UUID[kChar128BitStringLength] = {0};
      mgos_bt_uuid_to_str(&(discovery_result->chr), char_UUID);

      if (strcmp(kShellyDataChrUUID, char_UUID) == 0) {
        s_data_chr_handle = discovery_result->handle;
      }
      if (strcmp(kShellyRxChrUUID, char_UUID) == 0) {
        s_rx_chr_handle = discovery_result->handle;
      }
      if (strcmp(kShellyTxChrUUID, char_UUID) == 0) {
        s_tx_chr_handle = discovery_result->handle;
      }
      if (s_data_chr_handle && s_tx_chr_handle) {
        mgos_clear_timer(disconnect_timer);
        toggle_shelly_output(s_shelly_bt_connection.conn.conn_id,
                             s_data_chr_handle, s_tx_chr_handle, 0);
      }
      break;
    }
    case MGOS_BT_GATTC_EV_WRITE_RESULT: {
      LOG(LL_INFO, ("GATTC: Write done"));
      mgos_bt_gattc_disconnect(s_shelly_bt_connection.conn.conn_id);
      break;
    }
    case MGOS_BT_GATTC_EV_DISCOVERY_DONE: {
      LOG(LL_INFO, ("GATTC: Discovery done"));
      break;
    }
  }
}

static void gap_cb(int ev, UNUSED_ARG void *evd, UNUSED_ARG void *user_data) {
  static size_t scan_repeat = 0;
  switch (ev) {
    case MGOS_BT_GAP_EVENT_SCAN_START: {
      LOG(LL_INFO, ("Scan start"));
      s_shelly_bt_address =
          (struct mgos_bt_addr){.type = MGOS_BT_ADDR_TYPE_NONE, .addr = {0}};
      break;
    }
    case MGOS_BT_GAP_EVENT_SCAN_RESULT: {
      struct mgos_bt_gap_scan_result *gap_scan_result =
          (struct mgos_bt_gap_scan_result *) evd;
      char s_name[MGOS_BT_GAP_ADV_DATA_MAX_LEN] = {0};
      struct mg_str name = mgos_bt_gap_parse_name(gap_scan_result->adv_data);
      size_t len = MIN(name.len, sizeof(s_name) - 1);
      memcpy(s_name, name.p, len);
      if (strcmp(s_name, mgos_sys_config_get_shelly_btname()) == 0) {
        s_shelly_bt_address = gap_scan_result->addr;
        mgos_bt_gap_scan_stop();
        mgos_bt_gattc_connect(&s_shelly_bt_address);
      }
      break;
    }
    case MGOS_BT_GAP_EVENT_SCAN_STOP: {
      if (s_shelly_bt_address.type != MGOS_BT_ADDR_TYPE_NONE) {
        LOG(LL_INFO, ("Scan stop, Shelly found"));
      } else {
        LOG(LL_INFO, ("Scan stop, Shelly not found"));
        scan_repeat++;
        if (scan_repeat >= kBtMaxScanRepeat || !start_ble_scan())
          deep_sleep(s_btn_pin);
      }
      break;
    }
  }
}

static void ble_cb(UNUSED_ARG int ev, UNUSED_ARG void *evd,
                   UNUSED_ARG void *user_data) {
  if (ev == MGOS_BT_EV_STARTED) {
    LOG(LL_INFO, ("BT STARTED"));
    if (!start_ble_scan()) {
      deep_sleep(s_btn_pin);
    }
  }
}

enum mgos_app_init_result mgos_app_init(void) {
  // GAP, GATTC group handlers
  mgos_event_add_group_handler(MGOS_BT_GATTC_EV_BASE, gattc_cb, NULL);
  mgos_event_add_group_handler(MGOS_BT_GAP_EVENT_BASE, gap_cb, NULL);
  mgos_event_add_group_handler(MGOS_BT_EV_BASE, ble_cb, NULL);

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
