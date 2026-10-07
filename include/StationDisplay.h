#pragma once
#include <Arduino.h>

class WiFiManager;
void beginDisplay();
void beginDisplayFrames();
int updateDisplayFrames();
void serviceDisplayPresence();
void configModeCallback(WiFiManager* manager);
void drawOtaProgress(unsigned int progress, unsigned int total);
