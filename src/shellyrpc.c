#include "shellyrpc.h"

struct mbuf *get_rpc_json_output_toggle(struct mbuf *output_buffer,
                                        uint8_t output_id) {
  struct json_out json_out = JSON_OUT_MBUF(output_buffer);
  mbuf_init(output_buffer, 0);

  json_printf(&json_out, "{id:%d,method:\"%s\",src:\"%s\",params:{id:%d}}", 0,
              "Switch.Toggle", "espbt", output_id);

  return output_buffer;
}

struct mbuf *get_rpc_json_output_set(struct mbuf *output_buffer,
                                     uint8_t output_id, bool on) {
  struct json_out json_out = JSON_OUT_MBUF(output_buffer);
  mbuf_init(output_buffer, 0);

  json_printf(&json_out,
              "{id:%d,method:\"%s\",src:\"%s\",params:{id:%d, on:%B}}", 0,
              "Switch.Set", "espbt", output_id, on);

  return output_buffer;
}
