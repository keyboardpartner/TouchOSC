#ifndef GLOBAL_VARS_H
#define GLOBAL_VARS_H

#include <Arduino.h>

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <EEPROM.h>
#include <WiFiClient.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>

#define LED_GN 4
#define LED_OR 5

constexpr int c_webpage_timeout = 9999;

extern unsigned int localPort;

#define DEBUG_MON

#ifdef DEBUG
    #define DEBUG_COM
    #define DEBUG_WEB
    #define DEBUG_WEB_NOHX3
    #define DEBUG_WEB_JSON
    #define DEBUG_NOLED
#endif

#ifdef DEBUG
    #define DEBUG_MSG(...) Serial1.print( __VA_ARGS__ )
    #define DEBUG_MSGLN(...) Serial1.println( __VA_ARGS__ )
    #define ACK_TIMEOUT 2
#else
    #define DEBUG_MSG(...)
    #define DEBUG_MSGLN(...)
    #define ACK_TIMEOUT 50
#endif

#ifdef DEBUG_MON
    #define MON_MSGLN(...) Serial1.println( __VA_ARGS__ )
    #define MON_MSG(...) Serial1.print( __VA_ARGS__ )
#else
    #define MON_MSGLN(...)
    #define MON_MSG(...)
#endif

#ifdef DEBUG_COM
    #define COM_MSGLN(...) Serial1.println( __VA_ARGS__ )
    #define COM_MSG(...) Serial1.print( __VA_ARGS__ )
#else
    #define COM_MSGLN(...)
    #define COM_MSG(...)
#endif

#ifdef DEBUG_WEB
    #define WEB_MSGLN(...) Serial1.println( __VA_ARGS__ )
    #define WEB_MSG(...) Serial1.print( __VA_ARGS__ )
#else
    #define WEB_MSGLN(...)
    #define WEB_MSG(...)
#endif

extern MDNSResponder mdns;
extern WiFiUDP udp;

typedef struct
{
    char password[50];
    char ssid[50];
    int udp_delay;
    int udp_timeout;
    int udp_fb_others;
    int udp_fb_self;
	int ap_mode;
	IPAddress station_ip;
    char password_station[50];
    char ssid_station[50];
    char flag;
} EEPromData;

extern EEPromData eePromData;

extern IPAddress CurrentClientIP;
extern const IPAddress c_UnsetIP;
extern const int c_clients_max;

extern IPAddress ClientIPs[];
extern int ClientIPs_timer[];
extern int ClientIPs_idx;

extern char UDP_rcv_buffer[256];
extern char UDP_send_buffer[256];

extern int ValuesIndex;
extern int ResendIndex;

extern int LED_toggle;
extern unsigned long last_millis_led_toggle, last_millis_led_gn_timeout, last_millis_led_or_on;
extern unsigned long last_millis_1s, last_millis_10ms;
extern String ssid_list;

extern int ap_timeout;

extern int PageParamStart;
extern int PagePresetStart;
extern int PageCCsetStart;
extern int CurrentPage;

#define CONTROL_COUNT 700
extern byte ControlTypeArr[CONTROL_COUNT];
extern byte ControlIdx1Arr[CONTROL_COUNT];
extern byte ControlIdx2Arr[CONTROL_COUNT];
extern byte ControlMaxArr[CONTROL_COUNT];
extern int ControlBaseArr[CONTROL_COUNT];

extern char ControlNames[14][16];

#endif