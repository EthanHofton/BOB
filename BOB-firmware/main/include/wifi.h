#pragma once

#include "esp_err.h"

// ---- Soft AP ---------------------------------------------------------------

#define WIFI_AP_SSID     "BOB"
#define WIFI_AP_PASS     "bobbalance"
#define WIFI_AP_CHANNEL  6
#define WIFI_AP_MAX_CONN 2

// ---- WebSocket telemetry stream --------------------------------------------
// Connect: ws://192.168.4.1/telemetry
// Receives JSON frames at WIFI_STREAM_HZ, no data expected from client.
//
// Frame format (all floats 3 decimal places):
//   {"t":<us>,"roll":<deg>,"pitch":<deg>,
//    "ax":<m/s2>,"ay":<m/s2>,"az":<m/s2>,
//    "gx":<dps>,"gy":<dps>,"gz":<dps>,
//    "temp":<C>,"pid_out":<raw>,"motor":<pwm>}

#define WIFI_HTTP_PORT  80
#define WIFI_WS_PATH    "/telemetry"
#define WIFI_STREAM_HZ  50  // websocket push rate, independent of balance loop

// ---- TCP tuning server -----------------------------------------------------
// Connect: TCP 192.168.4.1:4242
// Protocol: newline-terminated JSON commands, newline-terminated JSON replies.
//
// Commands:
//   {"cmd":"set_gains","kp":<f>,"ki":<f>,"kd":<f>}
//   {"cmd":"set_setpoint","value":<f>}
//   {"cmd":"get_params"}
//   {"cmd":"reset_pid"}
//
// Replies:
//   {"status":"ok"}
//   {"status":"ok","params":{"kp":<f>,"ki":<f>,"kd":<f>,"setpoint":<f>}}
//   {"status":"error","message":"<reason>"}

#define WIFI_TCP_PORT   4242

// ----------------------------------------------------------------------------

esp_err_t wifi_init(void);
void      wifi_stop(void);
