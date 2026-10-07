#pragma once
#include <Arduino.h>

extern String hostname;
void beginWiFi();
void beginOTA();
void serviceWiFi();
void serviceLocalNetwork();
int8_t getWifiQuality();
