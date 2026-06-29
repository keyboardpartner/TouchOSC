#include "global_vars.h"

unsigned int localPort = 8000;

MDNSResponder mdns;
WiFiUDP udp;

EEPromData eePromData;

IPAddress CurrentClientIP(0,0,0,0);
const IPAddress c_UnsetIP(0,0,0,0);
const int c_clients_max = 4;

IPAddress ClientIPs[c_clients_max];
int ClientIPs_timer[c_clients_max];
int ClientIPs_idx = 0;

char UDP_rcv_buffer[256];
char UDP_send_buffer[256];

int ValuesIndex = 0;
int ResendIndex = 0;

int LED_toggle = 0;
unsigned long last_millis_led_toggle, last_millis_led_gn_timeout, last_millis_led_or_on;
unsigned long last_millis_1s, last_millis_10ms;
String ssid_list;

int ap_timeout = c_webpage_timeout;

int PageParamStart = 0;
int PagePresetStart = 0;
int PageCCsetStart = 0;
int CurrentPage = 0;

byte ControlTypeArr[CONTROL_COUNT];
byte ControlIdx1Arr[CONTROL_COUNT];
byte ControlIdx2Arr[CONTROL_COUNT];
byte ControlMaxArr[CONTROL_COUNT];
int ControlBaseArr[CONTROL_COUNT];

char ControlNames[14][16] = {
	"/none", "/page", "/label", "/led", "/param", "/param", "/param",
	"/switch", "/param_mbh", "/param_mbh", "/param_mf", "/param_xy", "/param", "/param"
};
