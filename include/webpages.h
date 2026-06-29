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
	Creates Access Point on 192.168.4.1
	Interpreter for OSC messages intended for HX3.5
	Carsten Meyer & KeyboardPartner 02/2020

	Web Server und Hilfsfunktionen, auch in main gebraucht

	Configuration service on http://192.168.4.1

*/

#ifndef WEBPAGES_H
#define WEBPAGES_H

#include <Arduino.h>

extern const String webpages_version_str;
extern const String osc_version_str;
extern const String webpages_copyright_str;

void initWebserver();
void handleWebserverRequests();

#endif // WEBPAGES_H