#ifndef SHAKTI_GOVEE_H
#define SHAKTI_GOVEE_H

#include "shakti.h"

V *bi_govee_lan_scan(V **a, int n);
V *bi_govee_lan_probe(V **a, int n);
V *bi_govee_lan_send(V **a, int n);
V *bi_govee_lan_status(V **a, int n);
V *bi_govee_cmd_turn(V **a, int n);
V *bi_govee_cmd_brightness(V **a, int n);
V *bi_govee_cmd_color(V **a, int n);
V *bi_govee_get_model(V **a, int n);
V *bi_govee_get_ip(V **a, int n);
V *bi_govee_set_ip(V **a, int n);

#endif
