#ifndef WIFI_H
#define WIFI_H

#if ESP8266
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESPAsyncTCP.h>
#endif // ESP8266

#if ESP32
#include <WiFi.h>
#include <ESPmDNS.h>
#include <AsyncTCP.h>
#endif // ESP32

extern char host_name[];
extern char ssid[];
extern char password[];

#endif
