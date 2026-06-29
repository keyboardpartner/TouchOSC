

/*
	#############################################################################
       __ ________  _____  ____  ___   ___  ___
      / //_/ __/\ \/ / _ )/ __ \/ _ | / _ \/ _ \
     / ,< / _/   \  / _  / /_/ / __ |/ , _/ // /
    /_/|_/___/_  /_/____/\____/_/_|_/_/|_/____/
      / _ \/ _ | / _ \/_  __/ |/ / __/ _ \
     / ___/ __ |/ , _/ / / /    / _// , _/
    /_/  /_/ |_/_/|_| /_/ /_/|_/___/_/|_|

	#############################################################################

	Wireless TouchOSC/Serial bridge using UDP on ESP8266
	Creates Access Point on 192.168.4.1 or connects to WIFI station
	Interpreter for OSC messages intended for HX3.5
	Carsten Meyer & KeyboardPartner 02/2020

	Flash size: 4MByte (FS 2 MByte, OTA 1019 KB)

	versteht ESC-Kommandos von HX3.5 (Parameter-Bereich 1000 bis 1495 und 16xx)
	sowie Klartext-Befehle in der Form
	/param/1234,100 (gesendet als Float)
	/led/1500/color,"green"

	UDP-Messages vom TouchPad werden wie folgt umgesetzt:
	"/param_xy/<param_nr X>/<param_nr Y>" und 2 Ints 0..127 bei XY Pad
	"/param_mf/<param_nr>/<idx>" und 1 Int 0..127 bei Multi-Fadern
	"/param_mbh/<param_nr>/1/<idx>" und 1 Int 0..127 bei Multi-Buttons hor.
	"/param_mbv/<param_nr>/<idx>/1" und 1 Int 0..127 bei Multi-Buttons vert.
	"/param/<param_nr>" und 1 Int 0..127 bei anderen Controllern
	"/page/<page_nr>" und 1 Int = 127 bei Page-Wechsel  (darf nicht auf "auto" stehen!)
	"/led/<led_nr>" und 1 Int 0..127 (Helligkeit) bei LED
	"/irgendeinname/<param_nr>" und 1 Int

	<param_nr> liegt im Bereich 1000..1495
	<page_nr>  liegt im Bereich 1650..1659 (max. 10 Pages)
	<led_nr>   liegt im Bereich 1640..1649 (max. 10 LEDs)

	Sendet Klartext in der Form "1000=127" nach erstem Text-Befehl
	oder nach erstem ESC-Binärbefehl mit Event-CMD #4 oder #5.
	Versteht serielle Daten von HX3.5 im Binärformat:

	ESC CMD ADRL ADRH LEN DATA0...DATAn CHK, hier also
	[0] [1]  [2]  [3] [4]  [5]          [6]
	27   5  ADRL ADRH  1  <val>        <chk>
	oder Bulk, hier 4 Datenbytes:
	[0] [1]  [2]  [3] [4]  [5]    [6]    [7]    [8]    [9]
	27   5  ADRL ADRH  4  <val1> <val2> <val3> <val4>  CHK
	mit CHK = 8-Bit-Summe aller Bytes, hier 0 bis 8.

	Sendet serielle Daten an HX3.5 im Binärformat,
	falls ESC-Befehl erstmals empfangen:

	ESC CMD ADRL ADRH LEN  DATA  CHK, hier also
	[0] [1]  [2]  [3] [4]  [5]   [6]
	27   1  ADRL ADRH  1  <val> <chk>
	mit CHK = 8-Bit-Summe aller Bytes 0 bis 5.

	HX3.5-spezifisch: Zurücksenden von Parametern an TouchOSC
	Multi-Fader, -Buttons und XY-Pads müssen anders addressiert werden
	Default: Fader/Buttons
	1000..1071, 1096..1111: Multi-Fader
	1072..1079: Multi-Fader 8
	1128..1159: Buttons
	1160..1191: Multi-Buttons hor.
	1080, 1081: XY-Pad
	1640..1649: LEDs
	1650..1659: Pages (darf nicht auf "auto" stehen!)

	Setzen von Farbe und anderer Optionen nur als Text-Befehl:
	/param/1135/color,"green"
	/param_mbh/1160/color,'blue'
	/param/1135/visible,0

	PROGRAMMIERUNG:
	Setup/Board "Wemos D1", roten PRG-Taster länger drücken, währenddessen kurz RST-Taster drücken
	UPLOAD_AGAIN.bat ausführen oder Upload aus Arduino-IDE
	File-System: Arduino IDE starten, Werkzeuge -> ESP8266 Data Upload

*/

// D E B U G - #define in hx3_utils.h!

#include <Arduino.h>

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <EEPROM.h>
#include <WiFiClient.h>
#include <ESP8266mDNS.h>	// haut bei Android nicht hin
#include <LittleFS.h>		// Include the LittleFS library

#include "webpages.h"
#include "hx3_utils.h"
#include "global_vars.h"


// #############################################################################
// ###                            SETUP & UTILS                              ###
// #############################################################################

void init_eeprom_ap() {
	strcpy(eePromData.ssid, "HX3.5 TouchOSC");
	strcpy(eePromData.password, "password");
	eePromData.udp_fb_others = 1;	// default
	eePromData.udp_fb_self = 1;
	eePromData.udp_delay = 2;
	eePromData.udp_timeout = 300;	// 5 Minuten
	eePromData.ap_mode = 1;			// ESP8266 ist selbst Access Point
}

void setup() {
	int i, temp_ap_mode, len, idx, lines, val;
	String temp;
	char line_buf[100];

	pinMode(LED_OR, OUTPUT);
	pinMode(LED_GN, OUTPUT);	// grüne LED ON
	digitalWrite(LED_OR, HIGH);
	Serial.begin(57600);
	Serial1.begin(230400);
	MON_MSGLN("");
	MON_MSGLN("Starting ESP8266...");
	delay(100);
	while (Serial.available()>0)
		Serial.read();
	MON_MSGLN("HX3.5 WiFi module " + webpages_version_str);
	MON_MSGLN(webpages_copyright_str);
	param_reset_all(699);

	LittleFS.begin();	// Start the flash file system

	// Datei mit Control-Zuordnungen einlesen
	// Tabelleninhalt: Control; Type; Base; Idx1; Idx2; Max;
	for (i=0; i<CONTROL_COUNT; i++) {
		ControlTypeArr[i] = t_none;
		ControlIdx1Arr[i] = 0;
		ControlIdx2Arr[i] = 0;
		ControlMaxArr[i] = 0;
		ControlBaseArr[i] = 0;
	}

	if (LittleFS.exists("/control_types.ini")) {			// Datei vorhanden?
		File file = LittleFS.open("/control_types.ini", "r");	// zum Lesen öffnen
		lines = 0;
		while (file.available()) {
			// Tabelleninhalt: Control; Type; Base; Idx1; Idx2; Max;
			// Type:
			// 0 = t_none, 1 = t_page, 2 = t_label, 3 = t_led, 4 = t_toggle, 5 = t_push, 6 = t_fader,
			// 7 = t_rotary, 8 = t_multitoggle, 9 = t_multipush, 10 = t_multifader, 11 = t_xypad, 12 = t_incdec
			len = file.readBytesUntil('\n', line_buf, sizeof(line_buf));
			line_buf[len-1] = 0;
			temp = extract_value(line_buf, ';', 0);		// Control, wird Index auf Array
			idx = temp.toInt();
			if (val_in_range(idx, 1000, 1699)) {
				idx -= 1000;	// 0..699
				//COM_MSG("IDX_ARR=");
				//COM_MSG(idx);
				temp = extract_value(line_buf, ';', 1);	// Type
				val = temp.toInt();
				if (val) {
					//COM_MSG(" TYPE=");
					//COM_MSG(val);
					ControlTypeArr[idx] = val;
				}
				temp = extract_value(line_buf, ';', 2);	// Base
				val = temp.toInt();
				if (val) {
					//COM_MSG(" BASE=");
					//COM_MSG(val);
					ControlBaseArr[idx] = val;
				}
				temp = extract_value(line_buf, ';', 3);	// Idx1
				val = temp.toInt();
				if (val) {
					//COM_MSG(" IDX1=");
					//COM_MSG(val);
					ControlIdx1Arr[idx] = val;
				}
				temp = extract_value(line_buf, ';', 4);	// Idx2
				val = temp.toInt();
				if (val) {
					//COM_MSG(" IDX2=");
					//COM_MSG(val);
					ControlIdx2Arr[idx] = val;
				}
				temp = extract_value(line_buf, ';', 5);	// Max/Quantize
				val = temp.toInt();
				if (val) {
					//COM_MSG(" MAX=");
					//COM_MSG(val);
					ControlMaxArr[idx] = val;
				}
				lines++;
				//COM_MSGLN("");
			}
		}
    	file.close();
		MON_MSG(lines);
		MON_MSGLN(" lines read from file 'control_types.ini'");
	} else {
		MON_MSGLN("File 'control_types.ini' not found!");
	}

	// Read EEPROM
	EEPROM.begin(sizeof(EEPromData));
	EEPROM.get(0,eePromData);
	EEPROM.end();
	ssid_list = "";
	int ssids_found = WiFi.scanNetworks();
	if (ssids_found) {
		MON_MSGLN("Found SSIDs: ");
		for(i=0; i<ssids_found; i++){
			temp = WiFi.SSID(i);
			MON_MSGLN(temp);
			if (i) ssid_list += ",\r\n";
			else ssid_list += "\r\n";
			ssid_list += "\"" + temp + "\"";
		}
	}

#ifdef DEBUG
    if (eePromData.flag != 0x55) {
		init_eeprom_ap();
	    strcpy(eePromData.ssid_station, "FRITZ!Box 6490 Cable");		// "Your Network"
	    strcpy(eePromData.password_station, "88462590539281722797");	// "password"
	    eePromData.flag = 0x55;			// initialized
	    write_eeprom();
		MON_MSGLN("WiFi name reset to SSID 'HX3.5 TouchOSC' and PW 'password'");
		// LittleFS.format();
	 	// MON_MSGLN("LittleFS formatted, upload HTML/CSS files!");
   }
#else
    if (eePromData.flag != 0x55) {
		init_eeprom_ap();
	    strcpy(eePromData.ssid_station, "Your Network");	// "Your Network"
	    strcpy(eePromData.password_station, "password");	// "password"
	    eePromData.flag = 0x55;			// initialized
	    write_eeprom();
		MON_MSGLN("WiFi name reset to SSID 'HX3.5 TouchOSC' and PW 'password'");
		// LittleFS.format();
	 	// MON_MSGLN("LittleFS formatted, upload HTML/CSS files!");
   }
#endif

	temp_ap_mode= eePromData.ap_mode;
	if (temp_ap_mode) {
		// ESP8266 ist selbst Access Point
		MON_MSGLN("WiFi Acces Point Mode, IP 192.168.4.1");
		WiFi.mode(WIFI_AP);			// Only Access point
		WiFi.softAP(eePromData.ssid, eePromData.password);	// access point
	} else {
	    // Externen Access Point bzw. Router verwenden
		MON_MSGLN("WiFi Station Mode ");
	    WiFi.mode(WIFI_STA);
	    WiFi.begin(eePromData.ssid_station, eePromData.password_station);
	    i = 0;
		LED_toggle = 0;
		MON_MSG("Connecting ...");
		digitalWrite(LED_GN, HIGH);
		while (WiFi.status() != WL_CONNECTED) {
		// 10 Sekunden Warten auf Verbindung
			delay(333);
			MON_MSG(".");
		    digitalWrite(LED_OR, LED_toggle);
			LED_toggle ^= 127;	// invertieren
			i++;
			if (i > 30) {
				temp_ap_mode = 1;
				break;
			}
		}
		MON_MSGLN("");
		if (temp_ap_mode) {
			MON_MSGLN("Station mode failed, will setup Access Point Mode, IP 192.168.4.1");
			WiFi.mode(WIFI_AP);			// Only Access point
			WiFi.softAP(eePromData.ssid, eePromData.password);	// access point
			digitalWrite(LED_GN, LOW);
		} else {
			MON_MSG("Connected with IP ");
			MON_MSGLN(WiFi.localIP());
			digitalWrite(LED_OR, LOW);
		}
	}

	//Start UDP
	udp.begin(localPort);
	MON_MSG("UDP started, local port: ");
	MON_MSGLN(udp.localPort());

	for (i=0; i<4; i++)
		ClientIPs[i] = c_UnsetIP;

	initWebserver();

	MON_MSGLN("HTTP started");

	if (mdns.begin("hx35", WiFi.localIP())) {
 		mdns.addService("http", "tcp", 80);
		MON_MSGLN("mDNS started");
	}

#ifndef DEBUG
    // auf Prompt von HX3.5 warten
	Serial1.println("Wait for HX3.5 prompt '>'");
	delay(250);
	wait_serial_byte('>', 10000);
	delay(500);
#endif
	Serial.setTimeout(50);
	MON_MSGLN("Ready.");

	if (temp_ap_mode) {
		Serial.println("");
		Serial.println("9100=\"WiFi AccessPoint\"");	// erste Zeile an HX3
		delay(50);
		Serial.println("9101=\"IP 192.168.4.1\"");		// zweite Zeile HX3
	} else {
		Serial.println("9100=\"WiFi Station, IP\"");	// erste Zeile HX3
		delay(50);
		Serial.print("9101=\"");		// zweite Zeile HX3
		Serial.print(WiFi.localIP());
		Serial.println("\"");
	}

	delay(50);
    last_millis_1s = millis();
    last_millis_10ms = last_millis_1s;
    last_millis_led_toggle = last_millis_1s;
	last_millis_led_gn_timeout = last_millis_1s;
	last_millis_led_or_on = last_millis_1s;

}


// #############################################################################
// ###                         UDP BUFFER UTILS                              ###
// #############################################################################

float udprcvbuf_extract_float(int idx) {
	// holt Float-Bytes aus UDP_rcv_buffer und wandelt sie in Integer
	union { byte b[4]; float f; } u;
	int n;
	// Reihenfolge umkehren
	for ( n=0 ; n<4 ; n++ ) u.b[n] = UDP_rcv_buffer[idx + 3 - n];
	return(u.f);	// in Integer 0..127 wandeln
}

int udprcvbuf_extract_int(int idx) {
	// holt Int-Bytes aus UDP_rcv_buffer und wandelt sie in Integer
	union { byte b[4]; int32 i; } u;
	int n;
	// Reihenfolge umkehren
	for ( n=0 ; n<4 ; n++ ) u.b[n] = UDP_rcv_buffer[idx + 3 - n];
	return(u.i);	// in Integer 0..127 wandeln
}


void int_to_udpsendbuf(int *idx, int val) {
	// zum Umsetzen von Int32 in Bytes, schreibt in UDP_send_buffer
	union { byte b[4]; int32 i32; } u;
	int i;
	u.i32 = val;
	// Reihenfolge umdrehen
	for ( i=0 ; i<4 ; i++ ) UDP_send_buffer[*idx + 3 - i] = u.b[i];
	*idx +=4;
}

void float_to_udpsendbuf(int *idx, float val) {
	// zum Umsetzen von Float in Bytes, schreibt in UDP_send_buffer
	union { byte b[4]; float f32; } u;
	int i;
	u.f32 = val;
	// Reihenfolge umdrehen
	for ( i=0 ; i<4 ; i++ ) UDP_send_buffer[*idx + 3 - i] = u.b[i];
	*idx +=4;
}

void format_to_udpsendbuf(int *idx, int count, char format_char) {
	// schreibt Format-Info in UDP_send_buffer
	if (count > 0) {
		UDP_send_buffer[*idx] = ',';
		UDP_send_buffer[*idx + 1] = format_char;
		if (count > 1)
			UDP_send_buffer[*idx + 2] = format_char;
		UDP_send_buffer[*idx + 3] = 0;
		*idx +=4;
	}
}

void clear_udpbuf() {
	int i;
	for (i = 0; i < 128; i++)
		UDP_send_buffer[i] = 0;
}

#ifdef DEBUG_HEXMSG
void serhex_udpbuf(int valid, int buf_end) {
	int i;
	if (valid) {
	    COM_MSG("cmd: ");
	    COM_MSGLN(UDP_send_buffer);
	    COM_MSG("HEX: ");
	    for (i = 0; i <= buf_end; i++) {
			COM_MSG(UDP_send_buffer[i], HEX);
		    COM_MSG(" ");
		}
		COM_MSGLN("");
	} else
		COM_MSGLN("Cmd: none");
}
#endif


// #############################################################################
// ###                                                                       ###
// ###                            U D P  PARSER                              ###
// ###                    eingehende Pakete von TouchOSC                     ###
// ###                                                                       ###
// ###        OSC Message Format bei HX3.5 Touchpad für TouchOSC             ###
// ###                                                                       ###
// ###  "/param/<param_nr>" und 2 Floats bei XY Pad                          ###
// ###  "/param/<param_nr>/<idx_1>" und 1 Float bei Multi-Fadern             ###
// ###  "/param/<param_nr>/<idx_1>/<idx_2>" und 1 Float bei MultiButtons     ###
// ###  "/param/<param_nr>" und 1 Float bei anderen Controllern oder         ###
// ###  "/param/<led_nr>" und 1 Float bei LED                                ###
// ###  "/page/<page_nr>" ohne Float bei Page-Wechsel                        ###
// ###  "/irgendeinname/<param_nr>" und 1 Float oder String                  ###
// ###                                                                       ###
// ###  idx_1 = Y-Pos, idx_2 = X-Pos bei Multi-Buttons                       ###
// ###                                                                       ###
// #############################################################################

// Control-Typ (enum) für jeden Parameter,
// 0 = t_none, 1 = t_page, 2 = t_label, 3 = t_led, 4 = t_toggle, 5 = t_push, 6 = t_fader,
// 7 = t_rotary, 8 = t_multitoggle, 9 = t_multipush, 10 = t_multifader, 11 = t_xypad
// Tabelleninhalt: Control; Type; Base; Idx1; Idx2; Max;

void parse_udp(int packet_size) {
	// liefert Anzahl der gültigen Bytes in UDP_rcv_buffer
	// und in param_ret, val_ret die gefundenen Parameter
	int i, s_ptr;
	String	temp;

	float val_1 = 0;
	float val_2 = 0;
	int param, arr_idx, arr_base;
	int arg_count_f = 0;
	int arg_count_i = 0;
	int idx_1 = 0;
	int idx_2 = 0;
	int ptype = t_none;

	// Position der Argumente, Komma suchen
	for (i = 0; (i <= packet_size) && (UDP_rcv_buffer[i] != 0x2C); i++);	// ","
	s_ptr = i;

	// Art des Controls bestimmen
	// Control-Typ (enum) für jeden Parameter,
	// 0 = t_none, 1 = t_page, 2 = t_label, 3 = t_led, 4 = t_toggle, 5 = t_push, 6 = t_fader,
	// 7 = t_rotary, 8 = t_multitoggle, 9 = t_multipush, 10 = t_multifader, 11 = t_xypad

	temp = extract_value(UDP_rcv_buffer,'/', 1);
	if (temp == "ping") {
		send_hx_binary(1611, 127);
		MON_MSGLN("UDPARSE ping: 1611=127");
	} else if (temp == "page") {
		// Sonderfall Pages: Direkt senden
		temp = extract_value(UDP_rcv_buffer,'/', 2);
		CurrentPage = temp.toInt();
		send_hx_binary(c_pages, CurrentPage);
		MON_MSGLN("UDPARSE page: 1650=" + temp);
		ptype = t_page;
		/*
		if (CurrentPage > 0) {
			resend_invalidate(0, 11);  // Drawbars später neu senden
			resend_invalidate(16, 27);
			resend_invalidate(32, 43);
			resend_invalidate(72, 75);
			resend_invalidate(80, 86);
			resend_invalidate(96, 107);
		}
		*/
	} else {
		if (UDP_rcv_buffer[s_ptr + 1] == 'f') arg_count_f++;
		if (UDP_rcv_buffer[s_ptr + 1] == 'i') arg_count_i++;
		if (UDP_rcv_buffer[s_ptr + 2] == 'f') arg_count_f++;	// "ff"
		if (UDP_rcv_buffer[s_ptr + 2] == 'i') arg_count_i++;	// "ii"
		// Tabelleninhalt: Control; Type; Base; Idx1; Idx2; Max;
		temp = extract_value(UDP_rcv_buffer,'/',2);	// Parameternummer 1000..1699
		param = temp.toInt();
		arr_idx = param - 1000;	// 0..699
		arr_base = ControlBaseArr[arr_idx] - 1000;
		ptype = ControlTypeArr[arr_idx];
		COM_MSG("UDPARSE arr_idx: ");
		COM_MSG(arr_idx);
		COM_MSG(", arr_base: ");
		COM_MSG(arr_base);
		COM_MSG(", type: ");
		COM_MSG(ptype);

		if (val_in_range(param, c_first_param, c_last_param)) {
			s_ptr +=4;
			if (arg_count_f)
				val_1 = udprcvbuf_extract_float(s_ptr); // Reihenfolge Bytes umkehren
			else
				val_1 = (float)udprcvbuf_extract_int(s_ptr);

			switch(ptype) {
				case t_multitoggle:
				case t_multipush:
					COM_MSG(" multibtn");
					temp = extract_value(UDP_rcv_buffer,'/',3);	// Index 1
					idx_1 = temp.toInt();
					temp = extract_value(UDP_rcv_buffer,'/',4);	// Index 2
					idx_2 = temp.toInt();
					if (idx_2 >= idx_1)
						OSC_values[arr_base + idx_2 - 1] = val_1;
					else
						OSC_values[arr_base + idx_1 - 1] = val_1;
					break;
				case t_multifader:
					COM_MSG(" multifader");
					temp = extract_value(UDP_rcv_buffer,'/',3);	// Index 1
					idx_1 = temp.toInt();
					OSC_values[arr_base + idx_1 - 1] = val_1;
					break;
				case t_xypad:
					COM_MSG(" xypad");
					s_ptr +=4;
					if (arg_count_f)
						val_2 = udprcvbuf_extract_float(s_ptr); // Reihenfolge Bytes umkehren
					else
						val_2 = (float)udprcvbuf_extract_int(s_ptr);
					// Y-Wert (Rotary) zuerst, ist in val_1
					// X-Wert (Master Volume) in val_2
					// bei XY-Pad gibt Idx_1 den Offset des ersten Werts zu arr_base an
					OSC_values[arr_base] = val_2;
					HX3_main_volume = (int)val_2;
					idx_1 = ControlIdx1Arr[arr_base];
					OSC_values[arr_base + idx_1] = val_1;
					HX3_rotary_volume = (int)val_1;

				default:
					COM_MSG(" param");
					OSC_values[arr_idx] = val_1;
					break;
			}
			if (param >= c_special_start)
				OSC_values_sent[arr_idx] = -1;	// auf "ungesendet" setzen
		}
		COM_MSG(", idx1: ");
		COM_MSG(idx_1);
		COM_MSG(", idx2: ");
		COM_MSG(idx_2);
		COM_MSG(", val_1: ");
		COM_MSG(val_1);
		COM_MSG(", val_2: ");
		COM_MSGLN(val_2);
	}

}


// #############################################################################
// ###                         UDP BUFFER SETUP                              ###
// #############################################################################

// Control-Typ (enum) für jeden Parameter,
// 0 = t_none, 1 = t_page, 2 = t_label, 3 = t_led, 4 = t_toggle, 5 = t_push, 6 = t_fader,
// 7 = t_rotary, 8 = t_multitoggle, 9 = t_multipush, 10 = t_multifader, 11 = t_xypad
// Tabelleninhalt: Control; Type; Base; Idx1; Idx2; Max;

int setup_udp_send_buffer(int param, int val) {
	// Buffer für UDP Send aufbereiten, liefert Länge des UDP-Buffers
	String sendstr;
	int len, idx_1, idx_2;
	int param_base, ptype;
	int arr_idx = 0;
	int buf_len = 0;
	int val_count = 1;

	// Sonderfall Volume/Gain; müssen zusammen gesendet werden, deshalb merken
	if (param == 1080)
		HX3_main_volume = val;
	if (param == 1081)
		HX3_rotary_volume = val;
	// Buffer UDP_send_buffer vorbereiten
	if (val_in_range(param, c_first_param, c_last_param)) {
		// param zwischen 1000 und 1699
		arr_idx = param - c_first_param;
	} else {
		MON_MSGLN("UDPSET PARAM ERROR!");
		return(0);
	}
	ptype = ControlTypeArr[arr_idx];
	if (ptype == t_none) {
		MON_MSGLN("UDPSET (none)");
		return(0);
	}
	param_base = ControlBaseArr[arr_idx];
	idx_1 = ControlIdx1Arr[arr_idx];
	idx_2 = ControlIdx2Arr[arr_idx];
	clear_udpbuf();
/*
	COM_MSG("UDP_setup arr_idx: ");
	COM_MSG(arr_idx);
	COM_MSG(", param_base: ");
	COM_MSG(param_base);
	COM_MSG(", type: ");
	COM_MSG(ptype);
	COM_MSG(", idx1: ");
	COM_MSG(idx_1);
	COM_MSG(", idx2: ");
	COM_MSGLN(idx_2);
*/
	sendstr = ControlNames[ptype];
	// ControlNames: {"/none",    "/page",   "/label",    "/led",     "/param",   "/param",    "/param",    "/switch",     "/param_mbh",    "/param_mbh",     "/param_mf",     "/param_xy" };
	//              0 = t_none, 1 = t_page, 2 = t_label, 3 = t_led, 4 = t_toggle, 5 = t_push, 6 = t_fader, 7 = t_rotary, 8 = t_multitoggle, 9 = t_multipush, 10 = t_multifader, 11 = t_xypad

	sendstr += "/";
	switch(ptype) {
		case t_page:
			// 1650, TouchOSC-Seite /page/<val> =127 aufrufen, darf nicht auf "auto" stehen!
			sendstr += String(val);
			val = 127;
			break;
		case t_xypad:
			val_count = 2;
			sendstr += String(param_base + idx_1) + "/" + String(param_base); // "/param_xy/1081/1080";
			break;
		case t_multifader:
			sendstr += String(param_base) + "/" + String(idx_1);
			break;
		case t_multipush:
		case t_multitoggle:
			if (idx_2 >= idx_1)
				sendstr += String(param_base) + "/1/" + String(idx_2);
			else
				sendstr += String(param_base) + "/" + String(idx_1) + "/1";
			break;
		default: // t_toggle, t_push, t_fader, t_rotary, t_led, t_label
			// nur String übernehmen
			sendstr += String(param);
			break;
	}
	COM_MSG("UDPSET: " + sendstr);

#ifdef DEBUG_HEXMSG
	DEBUG_MSG("UDPSET buf ");
	DEBUG_MSG(sendstr);
	DEBUG_MSG(", ");
	DEBUG_MSGLN(val);
#endif
	len = sendstr.length();
	sendstr.toCharArray(UDP_send_buffer, len + 1);
	// auf Long-Grenze bringen
	buf_len = len + 4 - (len % 4);
	// String ist im Buffer, nun ",i" und Integer einbauen
	format_to_udpsendbuf(&buf_len, val_count, 'i'); // 2 Werte wenn XY
	COM_MSG("=");
	if (ptype == t_xypad) {
		int_to_udpsendbuf(&buf_len, HX3_rotary_volume);	// Y-Wert (Rotary) zuerst!
		int_to_udpsendbuf(&buf_len, HX3_main_volume);	// X-Wert
		COM_MSG(HX3_rotary_volume);
		COM_MSG(",");
		COM_MSGLN(HX3_main_volume);
	} else {
		int_to_udpsendbuf(&buf_len, val); // umgek. Reihenfolge in Buffer
		COM_MSGLN(val);
	}
/*
	// String ist im Buffer, nun ",f" und Float einbauen
	format_to_udpsendbuf(&buf_len, val_count, 'f'); // 2 Werte wenn XY
	if (item_type == xypad) {
		float_to_udpsendbuf(&buf_len, (float)HX3_rotary_volume);
		float_to_udpsendbuf(&buf_len, (float)HX3_main_volume);
	} else {
		float_to_udpsendbuf(&buf_len, (float)val); // umgek. Reihenfolge in Buffer
	}
*/

#ifdef DEBUG_HEXMSG
	serhex_udpbuf(1, buf_len);
#endif
	return(buf_len);
}

// -----------------------------------------------------------------------------

int setup_udp_send_buffer_text(int text_buf_end, int *param_ret, int *val_ret) {
	// UDP_send_buffer nach Text-Befehl in HX3_text_buffer vorbereiten
	String	paramstr;
	int i, len;
	int param = -1;
	int val = 0;
	int comma_count = 0;
	int comma_pos = 0;
	int start_pos = -1;
	int string_pos = 0;
	int buf_len = 0;	// zeigt auf Ende von UDP-SendBuf
	int buf_char;
	char delimiter = ',';

	for (i = 0; i<=text_buf_end; i++)  // "/" enthalten?
		if (HX3_text_buffer[i] == '/') {
			start_pos = i;
			break;
		}

	COM_MSG("UDPSTEXT: ");
	COM_MSGLN(HX3_text_buffer);
	if (start_pos >= 0) {
		// direkter Befehl, z.B. Seitenauswahl, beginnt immer mit "/"
		if (start_pos > 0){ // wenn "/" nicht am Anfang, String zurechtrücken
			for (i = 0; i<=text_buf_end; i++)
		 		HX3_text_buffer[i] = HX3_text_buffer[i + start_pos];
			text_buf_end -= start_pos; // Ende um Startposition kleiner
		}
		// z.B. "/led_toggle,127"
		// UDP_send_buffer bis Delimiter einfach aus HX3_text_buffer übernehmen
		for (i = 0; i<=text_buf_end; i++)
			if ((HX3_text_buffer[i] == ',') || (HX3_text_buffer[i] == '=')) {
				comma_count++;
				comma_pos = i;
				delimiter = HX3_text_buffer[i];	// "," oder "="
			}

		if (comma_count)	// ist es ein String?
			for (i = comma_pos; i<=text_buf_end; i++) {
				if (!HX3_text_buffer[i])
					break;
				if ((HX3_text_buffer[i] == 34) || (HX3_text_buffer[i] == 39)) {
					string_pos = i + 1;	// Anführungszeichen weglassen
					break;
				}
			}
		clear_udpbuf();
		// String vor "," übernehmen
		paramstr = extract_value(HX3_text_buffer, delimiter, 0);
		len = paramstr.length();
		paramstr.toCharArray(UDP_send_buffer, len + 1);
		// auf Long-Grenze bringen
		buf_len = len + 4 - (len % 4);
		// nun ",s", ",f" oder ",ff" und Floats einbauen
		if (comma_count > 0) {
			if (string_pos)
				format_to_udpsendbuf(&buf_len, comma_count, 's'); // String
			else
				format_to_udpsendbuf(&buf_len, comma_count, 'i'); // 1 Wert oder 2 Werte wenn XY

			if (string_pos) {
				for (i=0; i<=text_buf_end; i++) {
					buf_char = HX3_text_buffer[string_pos];
					// Anführungszeichen oder Ende?
					if (!buf_char || (buf_char == 34) || (buf_char == 39))
						break;
					else {
						UDP_send_buffer[buf_len] = HX3_text_buffer[string_pos];
						buf_len++;
						string_pos++;
					}
				}
				// auf Long-Grenze bringen
				buf_len = buf_len + 4 - (buf_len % 4);
			} else {
		 		// 1 oder 2 Werte holen, so vorhanden
				paramstr = extract_value(HX3_text_buffer, delimiter, 1);
				val = paramstr.toInt();				// Wert
				int_to_udpsendbuf(&buf_len, val);	// umgek. Reihenfolge in Buffer
				if (comma_count > 1) {
					paramstr = extract_value(HX3_text_buffer, delimiter, 2);
					val = paramstr.toInt();			// Wert
					int_to_udpsendbuf(&buf_len, val);
				}
			}

		}
#ifdef DEBUG_HEXMSG
		serhex_udpbuf(1, buf_len);
#endif
		*param_ret = param;
		*val_ret = val;
	} else {
		paramstr = extract_value(HX3_text_buffer,'=', 0);
		param = paramstr.toInt();	// Parameter-Nummer
		paramstr = extract_value(HX3_text_buffer,'=', 1);
		val = paramstr.toInt();		// Wert
		buf_len = setup_udp_send_buffer(param, val);
		if (buf_len > 0) {
			*param_ret = param;
			*val_ret = val;
		}
	}
	return(buf_len);
}
// #############################################################################


void resend_udp_buffer_others(int bytes_to_send) {
	// Werte an angemeldete Clients zurücksenden
	int i;
	if ((eePromData.udp_fb_others) && (bytes_to_send > 0)) {
		for (i = 0; i<4; i++)
			if (ClientIPs_timer[i] && (ClientIPs[i] != CurrentClientIP)) {
				udp.beginPacket(ClientIPs[i], 9000);
				udp.write(UDP_send_buffer, bytes_to_send); // empfangene Daten zurück an Client
				udp.endPacket();
				delay(eePromData.udp_delay);	// Schleifenzeit
			}
	}
}

void resend_udp_buffer_self(int bytes_to_send) {
	// Werte an angemeldete Clients zurücksenden
	if ((eePromData.udp_fb_self) && (bytes_to_send > 0)){
		udp.beginPacket(CurrentClientIP, 9000);
		udp.write(UDP_send_buffer, bytes_to_send); // empfangene Daten zurück an Client
		udp.endPacket();
		delay(eePromData.udp_delay);	// Schleifenzeit
	}
}

void send_udp_buffer(int bytes_to_send) {
	int i;
	if (bytes_to_send > 0)
		for (i=0; i<4; i++)
			if (ClientIPs_timer[i]) {
				udp.beginPacket(ClientIPs[i], 9000);
				udp.write(UDP_send_buffer, bytes_to_send); // Send data to Client
				udp.endPacket();
				delay(eePromData.udp_delay);	// Schleifenzeit
			}
}

// -----------------------------------------------------------------------------

void check_for_new_client(IPAddress udp_client_ip) {
	// setzt ClientIPs_idx neu, wenn neuer Client gefunden
	int i, param, val_i, udp_buf_len;
	int found = 0;
	String sendstr;

	// War IP schon einmal verbunden?
	for (i=0; i<c_clients_max; i++)
		if (ClientIPs[i] == udp_client_ip) {
			found = 1;
			if (ClientIPs_timer[i] == 0) {
				MON_MSG("WIF: known client reconnect,  IP: ");
				MON_MSG(udp_client_ip);
				MON_MSG(", index ");
				MON_MSGLN(i);
		    Serial.println("");
				Serial.println("9100=\"Re-connect IP\"");
				delay(50);
				Serial.print("9101=\"");
				Serial.print(udp_client_ip);
				Serial.println("\"");
			}
			ClientIPs_timer[i]= eePromData.udp_timeout;
			break;
		}
	// IP noch nicht bekannt? Dann bis zu 4 Clients neu eintragen
	if (!found) {
		ClientIPs[ClientIPs_idx] = udp_client_ip;
		ClientIPs_timer[ClientIPs_idx]= eePromData.udp_timeout;
		MON_MSG("WIF: new client, IP: ");
		MON_MSG(udp_client_ip);
		MON_MSG(", index ");
		MON_MSGLN(ClientIPs_idx);
		Serial.println("");
		Serial.println("9100=\"New device IP\"");
		delay(50);
		Serial.print("9101=\"");
		Serial.print(udp_client_ip);
		Serial.println("\"");
		ClientIPs_idx++;
		if (ClientIPs_idx >= c_clients_max)
			ClientIPs_idx = 0;
		HX3_values[c_led_connected - 1000] = 64;	// LED 1649 blinkt

		sendstr = osc_version_str;	// Versionsnummer senden
		HX3_text_idx = sendstr.length();
		sendstr.toCharArray(HX3_text_buffer, HX3_text_idx);
		udp_buf_len = setup_udp_send_buffer_text(HX3_text_idx, &param, &val_i);
		send_udp_buffer(udp_buf_len);
	}
}

// #############################################################################

int handle_special_hx3commands(int param, int val) {
	int idx = param - 1000;
	if (val_in_range(param, c_version_request, c_wifi_reset)){
		HX3_values[idx] = 0;
		HX3_values_sent[idx] = 0;		// weiterhin reagieren
		switch(param) {
			case c_version_request:
				send_hx_binary(c_version_request, 0x03);
				COM_MSGLN("PAR: Version Requ");
				return(1);
				break;
			case c_param_inval:
				if (val) {
					param_invalidate(0, 274);	// invalidieren, alles an OSC senden
					param_invalidate(490, 491);	// GM/Organ Volumes
					COM_MSGLN("PAR: Invalidate");
				}
				return(1);
				break;
			case c_param_inval_ext:
				if (val) {
					param_invalidate(0, 274);	// invalidieren, alles an OSC senden
					param_invalidate(320, 334);
					param_invalidate(448, 460);
					param_invalidate(480, 494);
					COM_MSGLN("PAR: Invalidate Extd");
				}
				return(1);
				break;
			case c_eeprom_reset:
				if (val) {
					init_eeprom_ap();
					write_eeprom();
					COM_MSGLN("PAR: EEPROM AP Reset");
				}
				return(1);
				break;
			case c_param_apmode:
				if (val) {
					eePromData.ap_mode = 1;			// ESP8266 ist selbst Access Point
					COM_MSGLN("PAR: EEPROM AP Mode");
				} else {
					eePromData.ap_mode = 0;			// ESP8266 im Station Mode
					COM_MSGLN("PAR: EEPROM STA Mode");
				}
				write_eeprom();
				return(1);
				break;
			case c_wifi_reset:
				if (val) {
					COM_MSGLN("PAR: ESP8266 Reset");
					delay(10);
					ESP.reset();
				}
		}
	}
	return(0);
}

void handle_blink_leds() {
	// LEDs nach 333 ms abschalten
    int param, udp_buf_len;
	unsigned long current_millis = millis();
	if (current_millis >= last_millis_led_toggle + 333) {
		// wird ab hier jede 333 ms aufgerufen
		LED_toggle ^= 127;	// invertieren
		last_millis_led_toggle = current_millis;
#ifndef DEBUG_NOLED
		for (param=c_led_start; param<=c_led_end; param++) {
			if (HX3_values[param - 1000] == 64) {
				udp_buf_len = setup_udp_send_buffer(param, LED_toggle);
				send_udp_buffer(udp_buf_len);	// an alle bisher angemeldeten Clients senden
			}
		}
#endif
    }
	if (current_millis >= last_millis_led_gn_timeout)
		digitalWrite(LED_GN, HIGH);	// LED abschalten nach Timeout
	if (current_millis >= last_millis_led_or_on)
		digitalWrite(LED_OR, HIGH);	// LED abschalten nach Timeout
}

void led_gn_timed_on(int timeout) {
	// LEDs nach 333 ms abschalten
	digitalWrite(LED_GN, LOW);
	last_millis_led_gn_timeout = millis() + timeout;
}

void led_or_timed_on(int timeout) {
	// LEDs nach 333 ms abschalten
	digitalWrite(LED_OR, LOW);
	last_millis_led_or_on = millis() + timeout;
}


int collect_serdata() {
	int data_complete = not_complete;
	int s_byte;
	if (!esc_timer) {
		// ESC-Datensatz nicht eingetroffen
		HX3_binary_esc_received = 0; // Ende erreicht
		HX3_binary_idx = 0;
	}

	while (Serial.available()>0) {
		// Serielle Zeichen empfangen, alle verfügbaren sammeln
		s_byte = Serial.read();

		if ((s_byte == 27) && !HX3_binary_esc_received) {
			// Startzeichen Binary Mode,
			HX3_binary_idx = 0;	// leer
			HX3_binary_calculated_len = c_HX3_binary_min_len;	// mit LEN = 1
			// HX3_binary_esc_received wird nach vollständigem Empfang wieder auf 0 gesetzt
			HX3_binary_esc_received = 1;
			esc_timer = esc_timeout;
		}
		if (HX3_binary_esc_received)
			data_complete = collect_serial_binary(s_byte);	// Binary Mode sammeln
		else {
			if (s_byte == 62) {
				// Prompt
				HX3_binary_esc_received = 0; // Ende erreicht
				HX3_binary_idx = 0;
			} else {
				data_complete = collect_serial_text(s_byte);	// Text Mode sammeln
			}
		}

		// Zeichen lesen abbrechen, wenn komplett
		if (data_complete > not_complete) {
			HX3_binary_esc_received = 0; // Ende erreicht
			HX3_binary_idx = 0;
			break;
		}
	}
	return(data_complete);
}


// #############################################################################
// ###                                                                       ###
// ###                          M A I N   L O O P                            ###
// ###                                                                       ###
// #############################################################################

// Nach seriell empfangenem Datensatz (Textzeile oder ESC-Binärformat) wird
// setup_udp_send_buffer() bzw. setup_udp_send_buffer_text() aufgerufen
// und ein Buffer für den UDP-Versand an TouchOSC zusammengestellt.

// Empfangene UDP-Pakete werden nach Parametern und Werten durchsucht.
// Gültige OSC-Pakete gehen als Textbefehl oder im Binärformat an HX3.5 zurück,
// je nachdem, ob zuletzt ein Text- oder Binärbefehl empfangen wurde.

void loop() {
	int param,  val_i, idx;
	float  val_f;

	int packet_size, data_complete;
	int udp_buf_len;
	int control_type;

	chores_and_timeouts(); // Timouts etc.

	// UDP-Paket empfangen?
	packet_size = udp.parsePacket();
	if (packet_size) {
		// UDP Paket empfangen, vorrangig behandeln
		udp.read(UDP_rcv_buffer, packet_size); 	// read the packet into the buffer
		parse_udp(packet_size);

		CurrentClientIP = udp.remoteIP(); // IP der aktuellen Verbindung
		check_for_new_client(CurrentClientIP);

		resend_timer = resend_timeout;
		ResendIndex = 0;
		led_gn_timed_on(100);

	} else {
		data_complete = collect_serdata();
		if(data_complete) {
			resend_timer = resend_timeout;
			ResendIndex = 0;
			led_or_timed_on(100);
		} else {
			handle_blink_leds();
			// Configuration Service Webseite
			handleWebserverRequests();
			mdns.update();	// regelmäßig aufrufen!
		}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// Klartext-Befehle von HX3 behandeln
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

		if (data_complete == complete_text) {
			// Text eingetroffen, Klartext-Befehl
			if (HX3_text_idx > 3) {
				udp_buf_len = setup_udp_send_buffer_text(HX3_text_idx, &param, &val_i); // analysiert und setzt ggf. param, val!
				if (udp_buf_len > 0)
					if (!handle_special_hx3commands(param, val_i)) {
						send_udp_buffer(udp_buf_len);	// evt. color- oder visible-Befehl, immer senden
						delay(10);
						send_udp_buffer(udp_buf_len);	// nochmal
						delay(10);
						// Tabellenwert?
						if (val_in_range(param, c_first_param, c_last_param))
							HX3_values[param - 1000] = val_i;
					}
			}
			HX3_text_idx = 0;	// neue Zeile
		}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// Vom OSC-Client eingetroffene und bereits in OSC_values eingetragene
// Werte an HX3.5 senden
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

		control_type = ControlTypeArr[ValuesIndex];
		if (control_type > t_none) {		// benutzter Wert?
			if ((control_type == t_dec) || (control_type == t_inc)) {
				// INC/DEC-Funktionen, ControlBaseArr[] gibt Ziel-Parameter an,
				// control_type 12=DEC, 13=INC, auf ControlMaxArr[ValuesIndex]
				val_f = OSC_values[ValuesIndex];
				if (val_f != OSC_values_sent[ValuesIndex]) {
					// erneutes Senden der INC/DEC-Buttons verhindern
					OSC_values_sent[ValuesIndex] = val_f;
					OSC_values_resent[ValuesIndex] = val_f;
					val_i = (int)(val_f + 0.5);
					HX3_values[ValuesIndex] = val_i;
					HX3_values_sent[ValuesIndex] = val_i;
					if (val_i) {
						// zum INC/DEC-Befehl passenden Parameter-Idx berechnen
						idx = ControlBaseArr[ValuesIndex] - 1000;
						val_i = HX3_values[idx];
						if (control_type == t_inc) {
							if (val_i < ControlMaxArr[ValuesIndex])
								val_i++;
							COM_MSG("INC->HX3: ");
						} else {
							if (val_i > 0)
								val_i--;
							COM_MSG("DEC->HX3: ");
						}
						// ermittelten Parameter an HX3.5 senden
						param = idx + 1000;
						send_hx_binary(param, val_i);
						HX3_values[idx] = val_i;
						HX3_values_sent[idx] = val_i;
						// Preset/Voice-Einstellung wg. Label-Anzeige sofort zurücksenden
						COM_MSG("(INCDEC) ");
						udp_buf_len = setup_udp_send_buffer(param, val_i);
						send_udp_buffer(udp_buf_len);	// OSC an alle
						val_f = (float)val_i;
						OSC_values[idx] = val_f;
						OSC_values_sent[idx] = val_f;	// verhindert erneutes Senden des Parameters
						OSC_values_resent[idx] = -1;
					}
				}
			} else {
				// alle anderen Parameter nur an HX3 senden, re-send nach Wartezeit
				val_f = OSC_values[ValuesIndex];
				if (val_f != OSC_values_sent[ValuesIndex]) {
					// Vom Client eingetroffene und geänderte Werte an HX3 senden
					OSC_values_sent[ValuesIndex] = val_f;	// als an OSC gesendet markieren
					OSC_values_resent[ValuesIndex] = -1;	// re-send nach Wartezeit
					val_i = (int)(val_f + 0.5);

					// unnötigen UDP-Datenverkehr vermeiden: Werte synchronisieren
					// d.h. später nicht mehr an UDP senden
					HX3_values[ValuesIndex] = val_i;		// als empfangen und behandelt markieren
					HX3_values_sent[ValuesIndex] = val_i;
					// an sendenden Client wird später zurückgesendet!
					param = ValuesIndex + 1000;
					send_hx_binary(param, val_i);
					COM_MSG("WIF->HX3: ");
					COM_MSG(param);
					COM_MSG("=");
					COM_MSGLN(val_i);
				}

			}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// Vom HX3.5 eingetroffene Werte an OSC senden
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

			val_i = HX3_values[ValuesIndex];
			if (val_i != HX3_values_sent[ValuesIndex]) {
				// von HX3 geänderte Werte an UDP-Clients senden
				HX3_values_sent[ValuesIndex] = val_i;
				val_f = (float)val_i;
				// alle normalen Werte an Clients senden
				param = ValuesIndex + 1000;
				if (!handle_special_hx3commands(param, val_i)) {
					COM_MSG("(HX3PAR) ");
					udp_buf_len = setup_udp_send_buffer(param, val_i);
					send_udp_buffer(udp_buf_len);	// an alle bisher angemeldeten Clients senden
				}
				// unnötigen seriellen Datenverkehr vermeiden: Werte synchronisieren
				// d.h. nicht mehr an HX3 senden, falls von HX3 etwas zurückkommt
				OSC_values[ValuesIndex] = val_f;
				OSC_values_sent[ValuesIndex] = val_f;	// verhindert sofortiges erneutes Senden
				OSC_values_resent[ValuesIndex] = -1;	// re-send nach Wartezeit
			}
		} // param_used

		ValuesIndex++;
		if (ValuesIndex > 699)
			ValuesIndex = 0;

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// Verzögerte Synchronisierung der Clients und Senden der Knobs an HX3
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

		// Werte an alle Clients zurücksenden, wenn Timer auf 0 und aktiviert
		if (!resend_timer)  {
			// resend_timer abgelaufen, kann jetzt gesendet werden
			control_type = ControlTypeArr[ResendIndex];
			if (control_type > t_none) {		// benutzter Wert?
				param = ResendIndex + 1000;
				val_f = OSC_values[ResendIndex];
				if (val_f != OSC_values_resent[ResendIndex]) {
					OSC_values_resent[ResendIndex] = val_f;
					val_i = (int)(val_f + 0.5);
					COM_MSG("(RESEND) ");
					udp_buf_len = setup_udp_send_buffer(param, val_i);
					resend_udp_buffer_self(udp_buf_len);
					resend_udp_buffer_others(udp_buf_len);
				}
			}
			ResendIndex++;
			if (ResendIndex > 699) {
				ResendIndex = 0;
				resend_timer = -1;	// nach überlauf abschalten
			}
		}
	}
}
