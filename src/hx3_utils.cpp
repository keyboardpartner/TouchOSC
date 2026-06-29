#include <EEPROM.h>

#include "hx3_utils.h"

int HX3_text_idx = 0;
int HX3_binary_buffer[256];
int HX3_binary_idx = 0;
char HX3_text_buffer[256];
int HX3_binary_esc_received = 0;
int HX3_text_received = 0;
int HX3_binary_calculated_len = 6;

int HX3_main_volume = 0;
int HX3_rotary_volume = 0;

int HX3_values[700];
int HX3_values_sent[700];

float OSC_values[700];
float OSC_values_sent[700];
float OSC_values_resent[700];

const int esc_timeout = 20;
int esc_timer = esc_timeout;
const int resend_timeout = RESEND_TIMEOUT;
int resend_timer = RESEND_TIMEOUT;
int packet_sent_counter = 0;

static int get_serial_if_avail() {
	int s_byte = -1;
	if (Serial.available()>0)
		s_byte = Serial.read();
	return(s_byte);
}

void param_invalidate(int start_idx, int end_idx) {
	int i;
	for (i=start_idx; i<=end_idx; i++) {
		HX3_values_sent[i] = 0x55AA;
	}
}

void resend_invalidate(int start_idx, int end_idx) {
	int i;
	for (i=start_idx; i<=end_idx; i++) {
		OSC_values_resent[i] = -1.0;
	}
}

void param_reset_all(int len) {
	int i;
	for (i=0; i<= len; i++) {
		OSC_values[i] = 0;
		OSC_values_sent[i] = 0;
		OSC_values_resent[i] = 0;
		HX3_values[i] = 0;
		HX3_values_sent[i] = 0;
	}
}

void write_eeprom() {
	EEPROM.begin(sizeof(EEPromData));
	EEPROM.put(0,eePromData);
	EEPROM.commit();
	EEPROM.end();
	delay(200);
}

int val_in_range(int value, int min_val, int max_val) {
	return (value >= min_val) && (value <= max_val);
}

String extract_value(String data, char separator, int index) {
	int found = 0;
	int strIndex[] = { 0, -1 };
	int maxIndex = data.length()-1;
	for (int i = 0; i <= maxIndex && found <= index; i++) {
		if (data.charAt(i) == separator || data.charAt(i) == 0 || i == maxIndex) {
			found++;
			strIndex[0] = strIndex[1] + 1;
			strIndex[1] = (i == maxIndex) ? i+1 : i;
		}
	}
	return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}

String extract_bracket_string(String data) {
	int found = 0;
	int strIndex[] = { 0, -1 };
	int maxIndex = data.length()-1;
	for (int i = 0; i <= maxIndex && found <= 1; i++) {
		if (data.charAt(i) == '[' || data.charAt(i) == ']' || i > maxIndex) {
			found++;
			strIndex[0] = strIndex[1] + 1;
			strIndex[1] = i;
		}
	}
	return found > 1 ? data.substring(strIndex[0], strIndex[1]) : "";
}

int wait_serial_byte(int expected_byte, int timeout) {
#ifdef DEBUG_WEB_NOHX3
	return(0);
#else
	int s_byte = 0;
	int i;
	for (i = 0; i < timeout; i++) {
		s_byte = get_serial_if_avail();
		if ((s_byte == expected_byte) || (s_byte == 6) || (s_byte == 21))
			break;
		delay(1);
	}
	return(s_byte);
#endif
}

void send_hx_binary(int param, int val) {
	int chksum, i, s_byte;
	char ser_buf[8];
	chksum = 0;
	ser_buf[0] = 27;
	ser_buf[1] = 1;
	ser_buf[2] = param & 0xFF;
	ser_buf[3] = param >> 8;
	ser_buf[4] = 1;
	ser_buf[5] = val & 0xFF;
	for (i=0; i<6; i++) {
		s_byte = ser_buf[i];
		Serial.write(s_byte);
		chksum += s_byte;
	}
	Serial.write(chksum);
	wait_serial_byte('>', ACK_TIMEOUT);
}

int collect_serial_binary(int s_byte) {
	int i, param, val, len;
	int chksum;
	int complete_state;

	complete_state = not_complete;
	HX3_binary_buffer[HX3_binary_idx] = s_byte;

	if (HX3_binary_idx >= HX3_binary_calculated_len) {
		s_byte = HX3_binary_buffer[1];
		if ((s_byte == 4) || (s_byte == 5)) {
			len = HX3_binary_buffer[4] - 1;
			HX3_binary_calculated_len = c_HX3_binary_min_len + len;
			if (HX3_binary_idx < HX3_binary_calculated_len) {
				HX3_binary_idx++;
				return(not_complete);
			}
			param = HX3_binary_buffer[2] + (HX3_binary_buffer[3] * 256);
			chksum = 0;
			for (i = 0; i < HX3_binary_calculated_len; i++)
				chksum += HX3_binary_buffer[i];
			chksum = chksum & 0xFF;
			if (chksum == HX3_binary_buffer[HX3_binary_calculated_len]) {
				if (len == 0) {
					if(val_in_range(param, c_first_param, c_last_param)) {
						val = HX3_binary_buffer[5];
						HX3_values[param - 1000] = val;
						COM_MSGLN("-> BIN: " + String(param) + "=" + String(val));
					}
					complete_state = complete_binary_single;
				} else {
					if (val_in_range(param, c_first_param, c_last_param)) {
						COM_MSG("-> BIN_G: " + String(param) + "=");
						for (i=0; i<=len; i++) {
							val = HX3_binary_buffer[5 + i];
							HX3_values[(param - 1000) + i] = val;
							COM_MSG(String(val) + " ");
						}
						COM_MSGLN("");
					}
					complete_state = complete_binary_bulk;
				}
				Serial.write(6);
				COM_MSGLN("<- ACK");
			} else {
				Serial.write(21);
				COM_MSGLN("<- NAK");
			}
		}
	}
	HX3_binary_idx++;
	if (HX3_binary_idx > 255)
		HX3_binary_idx = 0;
	return(complete_state);
}

int collect_serial_text(int s_byte) {
	int complete_state;
	complete_state = not_complete;
	if (s_byte == 13) {
		HX3_text_buffer[HX3_text_idx] = 0;
		complete_state = complete_text;
		COM_MSG("-> TXT: ");
		COM_MSGLN(HX3_text_buffer);
	} else if ((s_byte == 8) && HX3_text_idx) {
		HX3_text_idx--;
	} else if (val_in_range(s_byte, 32, 127)) {
		HX3_text_buffer[HX3_text_idx] = s_byte;
		HX3_text_idx++;
	}
	if (HX3_text_idx > 255)
		HX3_text_idx = 0;
	return(complete_state);
}

void chores_and_timeouts() {
	const int c_1s = 1000;
	const int c_10ms = 10;
	unsigned long current_millis = millis();
	if (current_millis >= last_millis_10ms + c_10ms) {
		last_millis_10ms = current_millis;
		if (esc_timer)
			esc_timer--;
		if (resend_timer > 0)
			resend_timer--;
	}
	if (current_millis >= last_millis_1s + c_1s) {
		last_millis_1s = current_millis;
		for (int i=0; i<c_clients_max; i++) {
			if (ClientIPs_timer[i] > 0) {
				ClientIPs_timer[i]--;
			}
			if (ClientIPs_timer[i] == 1) {
				DEBUG_MSG("client timed out: #");
				DEBUG_MSGLN(i);
			}
		}
		if (ap_timeout) ap_timeout--;
	}
}