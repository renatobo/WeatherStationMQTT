#pragma once

// Project modifications by Renato Bonomini (renatobo); MIT, see LICENSE.
// Display service API. The OLED, frame callbacks and PIR timers stay
// private; networking delegates provisioning and OTA progress rendering here.
#include <Arduino.h>

class WiFiManager;
void beginDisplay();
void beginDisplayFrames();
int updateDisplayFrames();
void serviceDisplayPresence();
void configModeCallback(WiFiManager* manager);
void drawOtaProgress(unsigned int progress, unsigned int total);
