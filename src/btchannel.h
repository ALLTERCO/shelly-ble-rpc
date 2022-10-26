#pragma once

#include "mgos.h"

#define SHELLY_BT_CONN_EVENT_BASE MGOS_EVENT_BASE('S', 'B', 'T')
enum my_event {
  SHELLY_BT_STARTED = SHELLY_BT_CONN_EVENT_BASE,
  SHELLY_BT_CONNECTED,
  SHELLY_BT_DISCONNECTED,
  SHELLY_BT_SEND_SUCCESS,
  SHELLY_BT_FAIL
};

typedef void (*bt_channel_send)(struct mg_str rpc_message);

bool bt_channel_init(void);
bool bt_connect_shelly(const char *device_name);
