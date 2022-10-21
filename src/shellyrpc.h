#include "mgos.h"

struct mbuf *get_rpc_json_output_toggle(struct mbuf *output_buffer,
                                        uint8_t output_id);
struct mbuf *get_rpc_json_output_set(struct mbuf *output_buffer,
                                     uint8_t output_id, bool on);
