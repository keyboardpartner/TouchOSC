#ifndef HX3_UTILS_H
#define HX3_UTILS_H

#include <Arduino.h>

#include "global_vars.h"

#define RESEND_TIMEOUT 20
#define JSON_BUFLEN 15000

enum { not_complete, complete_binary_single, complete_binary_bulk, complete_text};

enum { t_none, t_page, t_label, t_led, t_toggle, t_push, t_fader, t_rotary, t_multitoggle, t_multipush, t_multifader, t_xypad, t_dec, t_inc };

const int c_first_param = 1000;
const int c_last_param = 1699;

const int c_knobs_start = 1264;
const int c_knobs_end = 1271;

const int c_special_start = 1600;
const int c_pushb_start = 1620;
const int c_pushb_end = 1639;
const int c_led_start = 1640;
const int c_led_end = 1649;
const int c_led_connected = 1649;
const int c_pages = 1650;

const int c_color_percena = 1660;
const int c_color_adsr = 1661;
const int c_color_percdb = 1662;

const int c_version_request = 1690;
const int c_param_apmode = 1695;
const int c_param_inval = 1696;
const int c_param_inval_ext = 1697;
const int c_eeprom_reset = 1698;
const int c_wifi_reset = 1699;

const int c_HX3_binary_min_len = 6;

extern int HX3_text_idx;
extern int HX3_binary_buffer[256];
extern int HX3_binary_idx;
extern char HX3_text_buffer[256];
extern int HX3_binary_esc_received;
extern int HX3_text_received;
extern int HX3_binary_calculated_len;

extern int HX3_main_volume;
extern int HX3_rotary_volume;

extern int HX3_values[700];
extern int HX3_values_sent[700];

extern float OSC_values[700];
extern float OSC_values_sent[700];
extern float OSC_values_resent[700];

extern const int esc_timeout;
extern int esc_timer;
extern const int resend_timeout;
extern int resend_timer;
extern int packet_sent_counter;

void resend_invalidate(int start_idx, int end_idx);
void param_invalidate(int start_idx, int end_idx);
void param_reset_all(int len);

int val_in_range(int value, int min_val, int max_val);
void send_hx_binary(int param, int val);
int wait_serial_byte(int expected_byte, int timeout);
String extract_value(String data, char separator, int index);
String extract_bracket_string(String data);
void write_eeprom();
int collect_serial_binary(int s_byte);
int collect_serial_text(int s_byte);
void chores_and_timeouts();

#endif