#ifndef WEB_H
#define WEB_H

#if ESP8266
#include <ESP8266mDNS.h>
#endif // ESP8266

#if ESP32
#include <ESPmDNS.h>
#endif // ESP32

#include <ESPAsyncWebServer.h>
#include <SPIFFSEditor.h>
#include <ArduinoJson.h>

extern AsyncWebServer server;
extern AsyncWebSocket ws;
extern AsyncEventSource events;

extern char http_username[];
extern char http_password[];

void web_server_init();

#endif
