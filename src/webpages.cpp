#include <ESP8266WiFi.h>
#include <EEPROM.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>

#include "webpages.h"
#include "hx3_utils.h"
#include "global_vars.h"

static ESP8266WebServer server(80);
static ESP8266HTTPUpdateServer httpUpdater;

static char serial_buf[80];
static String form;

static const char HEADER[] PROGMEM = "HTTP/1.1 303 OK\r\nLocation:littlefs.html\r\nCache-Control: no-cache\r\n";
static const char HELPER[] PROGMEM = R"(<form method="POST" action="/upload" enctype="multipart/form-data">
	<input type="file" name="upload"><input type="submit" value="Upload"></form>Upload littlefs.html to start file service.)";

const String webpages_version_str = "Ver #1.07";
const String osc_version_str = "/label_wifi_version=\"WiFi Interface Version 1.07\"";
const String webpages_copyright_str = "(c) Keyboardpartner & Carsten Meyer 02/2020";

static const String webpages_header = "<!DOCTYPE html><html lang='de'>"
	"<meta charset='UTF-8'>"
	"<meta name='viewport' content='width=device-width, initial-scale=1'>"
	"<link rel='stylesheet' href='style.css'>"
	"<title>HX3.5 WiFi Configuration</title></head>\r\n"
	"<body>\r\n"
	"<table class='header'><tr><td width = '200px'><img src='kplogo_kl.gif' alt='kbp logo'></td><td><h1> HX3.5 WiFi<br>Configuration</h1></td></tr></table>\r\n";

static const String webpages_footer = "</p><li>HX3.5 Server " + webpages_version_str + "<br>" + webpages_copyright_str + "</li></body></html>\r\n";

static bool handleFile(String&& path);
static void handleOther();
static void handleRoot();
static void handleGet();
static void handleGetParam();
static void handlePost();
static void handlePostEEP();
static void handleFileList();
static void handlePresetList();
static void handleParamList();
static void handleCCsetList();
static void handleConfigList();
static void handleUpload();
static void handleFormatLittleFS();
static boolean isValidNumber(String str);
static String formatBytes(size_t const& bytes);
static String getContentType(String filename);

void initWebserver() {
	httpUpdater.setup(&server);
	server.on("/", handleRoot);
	server.on("/get", handleGet);
	server.on("/config_json", handleConfigList);
	server.on("/filelist_json", handleFileList);
	server.on("/presets_json", handlePresetList);
	server.on("/params_json", handleParamList);
	server.on("/ccset_json", handleCCsetList);
	server.on("/value_json", handleGetParam);
	server.on("/format", handleFormatLittleFS);
	server.on("/upload", HTTP_POST, []() {}, handleUpload);
	server.on("/post", handlePost);
	server.on("/post_eep", handlePostEEP);
	server.onNotFound(handleOther);
	server.begin();
}

void handleWebserverRequests() {
	if (ap_timeout)
		ap_timeout = c_webpage_timeout;
	server.handleClient();
}

static String getContentType(String filename) {
	if (filename.endsWith(".htm") || filename.endsWith(".html")) filename = "text/html";
	else if (filename.endsWith(".css")) filename = "text/css";
	else if (filename.endsWith(".js")) filename = "application/javascript";
	else if (filename.endsWith(".json")) filename = "application/json";
	else if (filename.endsWith(".png")) filename = "image/png";
	else if (filename.endsWith(".gif")) filename = "image/gif";
	else if (filename.endsWith(".jpg")) filename = "image/jpeg";
	else if (filename.endsWith(".ico")) filename = "image/x-icon";
	else if (filename.endsWith(".xml")) filename = "text/xml";
	else if (filename.endsWith(".pdf")) filename = "application/x-pdf";
	else if (filename.endsWith(".zip")) filename = "application/x-zip";
	else if (filename.endsWith(".gz")) filename = "application/x-gzip";
	else filename = "text/plain";
	return filename;
}

static void handleOther() {
	WEB_MSGLN("-> handleOther");
	if (!handleFile(server.urlDecode(server.uri() )))
		server.send(404, "text/plain", "File not found");
}

static void handleRoot() {
	WEB_MSGLN("-> handleRoot");
	PagePresetStart = 0;
	PageParamStart = 0;
	if (ap_timeout) {
		server.sendHeader("Location", String("config.html"), true);
		server.send(302, "text/plain", "");
	} else {
		server.sendHeader("Location", String("timeout.html"), true);
		server.send(302, "text/plain", "");
	}
}

static void handlePost() {
	String command;
	server.send(204, "text/plain", "OK");
	if (server.method() == HTTP_POST) {
		command = server.arg(0);
		WEB_MSGLN("-> handlePost: " + command);
		Serial.println(command);
		wait_serial_byte('>', 100);
	}
}

static void handlePostEEP() {
	WEB_MSGLN("-> EEPROM enabled");
	Serial.println("wen=1");
	wait_serial_byte('>', 25);
	handlePost();
	Serial.println("wen=0");
	wait_serial_byte('>', 25);
}

static void handleGet() {
	String arg;
	int i;
	WEB_MSGLN("-> handleGet");
	if (ap_timeout) {
		if (server.hasArg("reset")) {
			arg = server.arg("reset");
			server.send(200, "text/html", webpages_header + "<p><li><font style='color:red'><b>HX3.5 WiFi module reset. URL might be invalid on reload.</b><font style='color:black'></li>" + webpages_footer);
			WEB_MSGLN("Reset HX3/WIFI");
			for (i = 0; i < 10; i++) {
				server.handleClient();
				delay(50);
			}
			wait_serial_byte('>', 10);
			Serial.println("");
			i = wait_serial_byte('>', 100);
			if (i != '>')
				ESP.reset();
			else
				Serial.println(arg + "=1");
		}

		if (server.hasArg("preset_page")) {
			arg = server.arg("preset_page");
			WEB_MSGLN("Preset Direct " + arg);
			PagePresetStart = arg.toInt();
			server.sendHeader("Location", String("presets.html"), true);
		}
		if (server.hasArg("preset_prev")) {
			WEB_MSGLN("Previous 20 Presets");
			PagePresetStart -= 20;
			if (PagePresetStart <0)
				PagePresetStart = 0;
			server.sendHeader("Location", String("presets.html"), true);
		}
		if (server.hasArg("preset_next")) {
			WEB_MSGLN("Next 20 Presets");
			PagePresetStart += 20;
			if (PagePresetStart > 80)
				PagePresetStart = 80;
			server.sendHeader("Location", String("presets.html"), true);
		}

		if (server.hasArg("param_page")) {
			arg = server.arg("param_page");
			WEB_MSGLN("ParamPage Direct " + arg);
			PageParamStart = arg.toInt();
			server.sendHeader("Location", String("params.html"), true);
		}
		if (server.hasArg("param_prev")) {
			WEB_MSGLN("Previous Params");
			PageParamStart--;
			if (PageParamStart <0)
				PageParamStart = 0;
			server.sendHeader("Location", String("params.html"), true);
		}
		if (server.hasArg("param_next")) {
			WEB_MSGLN("Next Params");
			PageParamStart++;
			if (PageParamStart > 9)
				PageParamStart = 9;
			server.sendHeader("Location", String("params.html"), true);
		}

		if (server.hasArg("ccset_page")) {
			arg = server.arg("ccset_page");
			WEB_MSGLN("CCsetPage Direct " + arg);
			PageCCsetStart = arg.toInt();
			server.sendHeader("Location", String("ccset.html"), true);
		}
		if (server.hasArg("ccset_prev")) {
			WEB_MSGLN("Previous CCset");
			PageCCsetStart--;
			if (PageCCsetStart <0)
				PageCCsetStart = 0;
			server.sendHeader("Location", String("ccset.html"), true);
		}
		if (server.hasArg("ccset_next")) {
			WEB_MSGLN("Next CCset");
			PageCCsetStart++;
			if (PageCCsetStart > 2)
				PageCCsetStart = 2;
			server.sendHeader("Location", String("ccset.html"), true);
		}

		if (server.hasArg("ccset_number")) {
			arg = server.arg("ccset_number");
			WEB_MSGLN("Set CCset, 1370=" + arg);
			wait_serial_byte('>', 10);
			Serial.println("1370=" + arg);
			wait_serial_byte('>', 100);
			delay(100);
			server.sendHeader("Location", String("ccset.html"), true);
		}

		if (server.hasArg("ccset_store")) {
			arg = server.arg("ccset_store");
			i = 4900 + arg.toInt();
			wait_serial_byte('>', 10);
			WEB_MSG("Store CCset, 4921=1, ");
			WEB_MSG(i);
			WEB_MSGLN("=1");
			wait_serial_byte('>', 10);
			Serial.println("4921=1");
			wait_serial_byte('>', 50);
			Serial.print(i);
			Serial.println("=1");
			wait_serial_byte('>', 200);
			server.sendHeader("Location", String("ccset.html"), true);
		}

		if (server.hasArg("ssid")) {
			WEB_MSGLN("Set SSID/PASSW");
			arg = server.arg("ssid");
			int len = arg.length();
			arg.toCharArray(eePromData.ssid, len + 1);

			arg = server.arg("pass");
			len = arg.length();
			arg.toCharArray(eePromData.password, len + 1);

			arg = server.arg("udp_delay");
			eePromData.udp_delay = arg.toInt();
			if (eePromData.udp_delay < 0)
				eePromData.udp_delay = 0;
			if (eePromData.udp_delay > 50)
				eePromData.udp_delay = 50;

			arg = server.arg("udp_timeout");
			eePromData.udp_timeout = arg.toInt();

			eePromData.udp_fb_others = server.hasArg("fb_others");
			eePromData.udp_fb_self = server.hasArg("fb_self");
			eePromData.ap_mode = server.hasArg("force_ap");

			arg = server.arg("ssid_sta");
			len = arg.length();
			arg.toCharArray(eePromData.ssid_station, len + 1);
			arg = server.arg("pass_sta");
			len = arg.length();
			arg.toCharArray(eePromData.password_station, len + 1);
			write_eeprom();
			server.sendHeader("Location", String("../"), true);
		}
	} else {
		server.sendHeader("Location", String("timeout.html"), true);
	}
	server.send(302, "text/plain", "");
}

static boolean isValidNumber(String str){
	boolean found_digit = true;
	for(uint i=0; i<str.length();i++) {
		if(!isDigit(str.charAt(i))) found_digit = false;
	}
	return found_digit;
}

static String formatBytes(size_t const& bytes) {
	return bytes < 1024 ? static_cast<String>(bytes) + " Bytes" : bytes < 1048576 ? static_cast<String>(bytes / 1024.0) + " KBytes" : static_cast<String>(bytes / 1048576.0) + " MBytes";
}

static void handleGetParam() {
	String param_str, value_str, json_str;
	int len;
	WEB_MSGLN("-> handleGetParam");
	param_str = "0";
	if (server.hasArg("param")) {
		param_str = server.arg("param");
	}

	Serial.println(param_str + "?");
#ifdef DEBUG_WEB_NOHX3
	WEB_MSGLN(param_str + "?");
	value_str = "123";
#else
	len = Serial.readBytesUntil('\r', serial_buf, 25);
	wait_serial_byte('>', 50);
	serial_buf[len] = 0;
	value_str = serial_buf;
#endif
	if (!isValidNumber(value_str)) value_str = "0";
	json_str = "{\"param\":" + param_str + ",\"value\":" + value_str + "}";
#ifdef DEBUG_WEB_JSON
	WEB_MSGLN(json_str);
#endif
	WEB_MSGLN("JSON length: " + String(json_str.length()));
	server.send(200, "application/json", json_str);
}

static void handleFileList() {
	FSInfo fs_info;
	LittleFS.info(fs_info);
	WEB_MSGLN("-> handleFileList");
	Dir dir = LittleFS.openDir("/");
	String json_str = "[\r\n";
	int line = 0;
	while (dir.next()) {
		if (line) json_str += ",\r\n";
		json_str += "{\"name\":\"" + dir.fileName().substring(1) + "\",\"size\":\"" + formatBytes(dir.fileSize()) + "\"}";
		line++;
	}
	json_str += ",\r\n{\"usedBytes\":\"" + formatBytes(fs_info.usedBytes * 1.05) + "\"," +
		"\"totalBytes\":\"" + formatBytes(fs_info.totalBytes) + "\",\"freeBytes\":\"" +
		(fs_info.totalBytes - (fs_info.usedBytes * 1.05)) + "\"}\r\n]";
#ifdef DEBUG_WEB_JSON
	WEB_MSGLN(json_str);
#endif
	WEB_MSGLN("JSON length: " + String(json_str.length()));
	server.send(200, "application/json", json_str);
}

static void handlePresetList() {
	String json_str = "[\r\n";
	String preset_name;
	int i, byte_count;
	WEB_MSGLN("-> handlePresetList");
	int line = 0;
	for (i=PagePresetStart; i<PagePresetStart+20; i++) {
		Serial.println(String(9800 + i) + "?");
#ifdef DEBUG_WEB_NOHX3
		preset_name = "Debug Test " + String(i);
#else
		byte_count = Serial.readBytesUntil('\r', serial_buf, 25);
		serial_buf[byte_count] = 0;
		preset_name = extract_bracket_string(serial_buf);
		wait_serial_byte('>', 25);
#endif
		WEB_MSGLN(preset_name);
		if (line) json_str += ",\r\n";
		json_str += "{\"index\":" + String(i) + ",\"name\":\"" + preset_name + "\"}";
		line++;
	}
	json_str += "\r\n]";
#ifdef DEBUG_WEB_JSON
	WEB_MSGLN(json_str);
#endif
	WEB_MSGLN("JSON length: " + String(json_str.length()));
	server.send(200, "application/json", json_str);
}

static void handleParamList() {
	String param_str, desc_str, value_str, page_str, max_str, type_str;
	page_str = String(PageParamStart);
	String json_str = "[\r\n";
	String file_name = "/params_" + page_str + ".txt";
	int i, len;
	WEB_MSGLN("-> handleParamList");
	int line = 0;
	boolean is_valid;
	if (LittleFS.exists(file_name)) {
		File file = LittleFS.open(file_name, "r");
		wait_serial_byte('>', 25);
		while (file.available()) {
			len = file.readBytesUntil('\n', serial_buf, sizeof(serial_buf));
			serial_buf[len-1] = 0;
			param_str = extract_value(serial_buf, ';', 0);
			desc_str = extract_value(serial_buf, ';', 1);
			max_str = extract_value(serial_buf, ';', 2);
			type_str = extract_value(serial_buf, ';', 3);
			if (param_str == "#") {
				value_str = "0";
				param_str = "\"#\"";
			} else {
				for  (i=0; i<5; i++) {
					Serial.println(param_str + "?");
#ifdef DEBUG_WEB_NOHX3
					value_str = "0";
#else
					len = Serial.readBytesUntil('\r', serial_buf, 25);
					wait_serial_byte('>', 50);
					serial_buf[len] = 0;
					value_str = serial_buf;
#endif
					is_valid = isValidNumber(value_str);
					if (is_valid) break;
				}
				if (!is_valid) value_str = "0";
				WEB_MSGLN(param_str + "?=" + value_str);
			}
			if (line) json_str += ",\r\n";
			json_str += "{\"param\":" + param_str + ",\"desc\":\"" + desc_str + "\",\"value\":" + value_str + ",\"max\":" + max_str + ",\"type\":\"" + type_str + "\"}";
			line++;
		}
		file.close();
	}
	json_str += ",\r\n{\"page\":" + page_str + "}\r\n]";
#ifdef DEBUG_WEB_JSON
	WEB_MSGLN(json_str);
#endif
	WEB_MSGLN("JSON length: " + String(json_str.length()));
	server.send(200, "application/json", json_str);
}

static void handleCCsetList() {
	String param_str, desc_str, value_str, page_str;
	page_str = String(PageCCsetStart);
	String json_str = "[\r\n";
	String file_name = "/ccset_" + page_str + ".txt";
	int i, len, cc_param;
	WEB_MSGLN("-> handleCCsetList");
	int line = 0;
	wait_serial_byte('>', 25);
	Serial.println("1370?");
#ifdef DEBUG_WEB_NOHX3
	String ccset_str = "8";
#else
	len = Serial.readBytesUntil('\r', serial_buf, 25);
	wait_serial_byte('>', 50);
	serial_buf[len] = 0;
	String ccset_str = serial_buf;
#endif
	boolean is_valid;
	if (LittleFS.exists(file_name)) {
		File file = LittleFS.open(file_name, "r");
		wait_serial_byte('>', 25);
		while (file.available()) {
			len = file.readBytesUntil('\n', serial_buf, sizeof(serial_buf));
			serial_buf[len-1] = 0;
			param_str = extract_value(serial_buf, ';', 0);
			desc_str = extract_value(serial_buf, ';', 1);
			if (param_str == "#") {
				value_str = "0";
				param_str = "\"#\"";
			} else {
				cc_param = param_str.toInt() + 3000;
				for  (i=0; i<5; i++) {
					Serial.print(cc_param);
					Serial.println("?");
#ifdef DEBUG_WEB_NOHX3
					value_str = "0";
#else
					len = Serial.readBytesUntil('\r', serial_buf, 25);
					wait_serial_byte('>', 50);
					serial_buf[len] = 0;
					value_str = serial_buf;
#endif
					is_valid = isValidNumber(value_str);
					if (is_valid) break;
				}
				if (!is_valid) value_str = "0";
				WEB_MSG(cc_param);
				WEB_MSGLN("?=" + value_str);
			}
			if (line) json_str += ",\r\n";
			json_str += "{\"param\":" + param_str + ",\"desc\":\"" + desc_str + "\",\"value\":" + value_str + "}";
			line++;
		}
		file.close();
	}
	json_str += ",\r\n{\"page\":" + page_str + "},\r\n{\"ccset\":" + ccset_str + "}\r\n]";
#ifdef DEBUG_WEB_JSON
	WEB_MSGLN(json_str);
#endif
	WEB_MSGLN("JSON length: " + String(json_str.length()));
	server.send(200, "application/json", json_str);
}

static bool handleFile(String&& path) {
	WEB_MSGLN("-> handleFile: " + path);
	if (server.hasArg("delete")) {
		LittleFS.remove(server.arg("delete"));
		server.sendContent(HEADER);
		return true;
	}
	if (!LittleFS.exists("/littlefs.html"))
		server.send(200, "text/html", HELPER);
	if (path.endsWith("/"))
		path += "index.html";
	return LittleFS.exists(path) ? ({File f = LittleFS.open(path, "r"); server.streamFile(f, getContentType(path)); f.close(); true;}) : false;
}

static void handleConfigList() {
	const int bSize = sizeof(serial_buf) - 1;
	String temp;
	String json_str;
	int byte_count, i;
	int client_count = 0;

	WEB_MSGLN("-> handleConfig");
	PagePresetStart = 0;
	PageParamStart = 0;

	json_str = "{\r\n";
	temp = (eePromData.udp_fb_others) ? "true" : "false";
	json_str += "\"fb_others\":" + temp + ",\r\n";
	temp = (eePromData.udp_fb_self) ? "true" : "false";
	json_str += "\"fb_self\":" + temp + ",\r\n";
	temp = (eePromData.ap_mode) ? "true" : "false";
	json_str += "\"ap_mode\":" + temp + ",\r\n";

	for (i=0; i<c_clients_max; i++)
		if (ClientIPs_timer[i] > 0) client_count++;
	json_str += "\"client_count\":" + String(client_count) + ",\r\n";

	Serial.println("");
	wait_serial_byte('>', 10);

	Serial.println("idn?");
	byte_count = Serial.readBytesUntil('\r', serial_buf, bSize);
	serial_buf[byte_count] = 0;
	temp = extract_value(serial_buf, '\r', 0);
	wait_serial_byte('>', 25);
	json_str += "\"fw_version\":\"" + temp + "\",\r\n";

	Serial.println("3?");
	byte_count = Serial.readBytesUntil('\r', serial_buf, bSize);
	serial_buf[byte_count] = 0;
	temp = extract_value(serial_buf, ' ', 1);
	wait_serial_byte('>', 25);
	json_str += "\"fpga_version\":\"" + temp + "\",\r\n";

	Serial.println("242?");
	byte_count = Serial.readBytesUntil('\r', serial_buf, bSize);
	serial_buf[byte_count] = 0;
	temp = extract_value(serial_buf, ' ', 0);
	wait_serial_byte('>', 25);
	json_str += "\"sernum\":\"" + temp + "\",\r\n";

	temp = WiFi.localIP().toString();
	if (temp == "(IP unset)")
		temp = "192.168.4.1";
	json_str += "\"ip_info\":\"" + temp + "\",\r\n";
	json_str += "\"wifi_vers\":\"" + webpages_version_str + "\",\r\n";

	json_str += "\"ssid_ap\":\"" + String(eePromData.ssid) + "\",\r\n";
	json_str += "\"passw_ap\":\"" + String(eePromData.password) + "\",\r\n";
	json_str += "\"udp_delay\":" + String(eePromData.udp_delay) + ",\r\n";
	json_str += "\"udp_timeout\":" + String(eePromData.udp_timeout) + ",\r\n";
	json_str += "\"ssid_sta\":\"" + String(eePromData.ssid_station) + "\",\r\n";
	json_str += "\"passw_sta\":\"" + String(eePromData.password_station) + "\",\r\n";
	json_str += "\"ssid_list\":[" + ssid_list + "]\r\n}\r\n";
#ifdef DEBUG_WEB_JSON
	WEB_MSGLN(json_str);
#endif
	WEB_MSGLN("JSON length: " + String(json_str.length()));
	server.send(200, "application/json", json_str);
}

static void handleUpload() {
	WEB_MSGLN("-> handleUpload");
	static File fsUploadFile;
	HTTPUpload& upload = server.upload();
	if (upload.status == UPLOAD_FILE_START) {
		if (upload.filename.length() > 30) {
			upload.filename = upload.filename.substring(upload.filename.length() - 30, upload.filename.length());
		}
		Serial1.printf("handleFileUpload Name: /%s\n", upload.filename.c_str());
		fsUploadFile = LittleFS.open("/" + server.urlDecode(upload.filename), "w");
	} else if (upload.status == UPLOAD_FILE_WRITE) {
		if (fsUploadFile)
			fsUploadFile.write(upload.buf, upload.currentSize);
	} else if (upload.status == UPLOAD_FILE_END) {
		if (fsUploadFile)
			fsUploadFile.close();
		Serial1.printf("handleFileUpload Size: %u\n", upload.totalSize);
		server.sendContent(HEADER);
	}
}

static void handleFormatLittleFS() {
	LittleFS.format();
	server.sendContent(HEADER);
}