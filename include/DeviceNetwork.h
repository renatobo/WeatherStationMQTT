#pragma once

// Project modifications by Renato Bonomini (renatobo); MIT, see LICENSE.
// Wi-Fi/OTA service API. Initialize once; pump local services in the main
// loop and during weather reads. The hostname comes from the selected profile.
#include <Arduino.h>

extern String hostname;
void beginWiFi();
void beginOTA();
void serviceWiFi();
void serviceLocalNetwork();
int8_t getWifiQuality();
