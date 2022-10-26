#include "mgos.h"
#include "mgos_bt.h"
#include "mgos_bt_gap.h"
#include "mgos_bt_gattc.h"

#include "host/ble_gap.h"
#include "host/ble_gatt.h"

#include "btchannel.h"

//  Time in millisecons for scanning
const uint16_t kBtScanPeriod = 1000;
//  Time in milliseconds after which the connection is closed
const uint16_t kBtConnectTimeout = 2500;
//  Number of tries to scan for a Shelly
const uint8_t kBtMaxScanRepeat = 5;

//  Size of the characteristics string representation buffer
enum { kChar128BitStringLength = 37 };

//  Shelly BT UUIDs, same as mongoose default UUIDs
const char kShellyGattServiceUUID[] = "5f6d4f53-5f52-5043-5f53-56435f49445f";
const char kShellyDataChrUUID[] = "5f6d4f53-5f52-5043-5f64-6174615f5f5f";
const char kShellyTxChrUUID[] = "5f6d4f53-5f52-5043-5f74-785f63746c5f";
const char kShellyRxChrUUID[] = "5f6d4f53-5f52-5043-5f72-785f63746c5f";

enum { kMaxShellyName = 50 };
static char s_shelly_name[kMaxShellyName + 1] = {0};
static struct mgos_bt_gattc_connect_arg s_shelly_bt_connection;
static RTC_DATA_ATTR struct mgos_bt_addr s_shelly_bt_address;

static RTC_DATA_ATTR uint16_t s_data_chr_handle;
static RTC_DATA_ATTR uint16_t s_tx_chr_handle;
static RTC_DATA_ATTR uint16_t s_rx_chr_handle;

static RTC_DATA_ATTR bool s_connect_without_scan = false;
static mgos_timer_id connect_timer;

static void bt_send(struct mg_str rpc_message) {
  if (s_shelly_bt_connection.ok == false) {
    mgos_event_trigger(SHELLY_BT_FAIL, NULL);
    return;
  }
  uint32_t command_size = htonl(rpc_message.len);
  struct mg_str command_size_buf = {.p = (char *) &command_size,
                                    .len = sizeof(command_size)};
  mgos_bt_gattc_write(s_shelly_bt_connection.conn.conn_id, s_tx_chr_handle,
                      command_size_buf, false);
  // ask for being informed of the write succeeding
  mgos_bt_gattc_write(s_shelly_bt_connection.conn.conn_id, s_data_chr_handle,
                      rpc_message, true);
}

static bool start_ble_scan(void) {
  struct mgos_bt_gap_scan_opts bt_scan_options = {.duration_ms = kBtScanPeriod,
                                                  .active = false};
  return mgos_bt_gap_scan(&bt_scan_options);
}

void connect_timeout_cb(void *arg UNUSED_ARG) {
  s_connect_without_scan = false;
  mgos_event_trigger(SHELLY_BT_FAIL, NULL);
}

bool bt_connect_shelly(const char *device_name) {
  if (strlen(device_name) > kMaxShellyName) {
    mgos_event_trigger(SHELLY_BT_FAIL, NULL);
    return false;
  }
  strncpy(s_shelly_name, device_name, kMaxShellyName);
  if(s_shelly_bt_address.type != MGOS_BT_ADDR_TYPE_NONE && s_connect_without_scan) {
    connect_timer = mgos_set_timer(kBtConnectTimeout, 0, connect_timeout_cb, NULL);
    return mgos_bt_gattc_connect(&s_shelly_bt_address);
  } else {
    return start_ble_scan();
  }
}

static void ble_cb(int ev UNUSED_ARG, void *evd UNUSED_ARG,
                   void *user_data UNUSED_ARG) {
  if (ev == MGOS_BT_EV_STARTED) {
    mgos_event_trigger(SHELLY_BT_STARTED, NULL);
  }
}

static void gap_cb(int ev, void *evd UNUSED_ARG, void *user_data UNUSED_ARG) {
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
      if (strcasecmp(s_name, mgos_sys_config_get_shelly_btname()) == 0) {
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
          mgos_event_trigger(SHELLY_BT_FAIL, NULL);
      }
      break;
    }
  }
}

static void gattc_cb(int ev, void *evd UNUSED_ARG, void *user_data UNUSED_ARG) {
  static mgos_timer_id disconnect_timer;

  switch (ev) {
    case MGOS_BT_GATTC_EV_CONNECT: {
      LOG(LL_INFO, ("GATTC: BLE connected"));

      mgos_clear_timer(connect_timer);

      s_connect_without_scan = true;

      s_shelly_bt_connection = *((struct mgos_bt_gattc_connect_arg *) evd);

      if (s_shelly_bt_connection.ok == false) {
        mgos_event_trigger(SHELLY_BT_FAIL, NULL);
        return;
      }

      if (s_data_chr_handle > 0 && s_tx_chr_handle > 0) {
        LOG(LL_INFO, ("Characteristics handles preserved over sleep"));
        mgos_event_trigger(SHELLY_BT_CONNECTED, &bt_send);
      } else {
        LOG(LL_INFO, ("Discover characteristics handles"));
        if (!mgos_bt_gattc_discover(s_shelly_bt_connection.conn.conn_id)) {
          mgos_bt_gattc_disconnect(s_shelly_bt_connection.conn.conn_id);
          return;
        }
      }

      // gcc allows for this
      void disconnect(void *arg UNUSED_ARG) {
        mgos_bt_gattc_disconnect(s_shelly_bt_connection.conn.conn_id);
      }
      disconnect_timer = mgos_set_timer(kBtConnectTimeout, 0, disconnect, NULL);

      break;
    }
    case MGOS_BT_GATTC_EV_DISCONNECT: {
      LOG(LL_INFO, ("GATTC: BLE disconnected"));
      mgos_event_trigger(SHELLY_BT_DISCONNECTED, NULL);
      break;
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
        mgos_event_trigger(SHELLY_BT_CONNECTED, &bt_send);
      }
      break;
    }
    case MGOS_BT_GATTC_EV_WRITE_RESULT: {
      LOG(LL_INFO, ("GATTC: Write done"));
      mgos_event_trigger(SHELLY_BT_SEND_SUCCESS, NULL);
      mgos_bt_gattc_disconnect(s_shelly_bt_connection.conn.conn_id);
      break;
    }
    case MGOS_BT_GATTC_EV_DISCOVERY_DONE: {
      LOG(LL_INFO, ("GATTC: Discovery done"));
      break;
    }
  }
}

bool bt_channel_init(void) {
  mgos_event_add_group_handler(MGOS_BT_GATTC_EV_BASE, gattc_cb, NULL);
  mgos_event_add_group_handler(MGOS_BT_GAP_EVENT_BASE, gap_cb, NULL);
  mgos_event_add_group_handler(MGOS_BT_EV_BASE, ble_cb, NULL);
  return true;
}