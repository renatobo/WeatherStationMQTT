#pragma once

// Project modifications by Renato Bonomini (renatobo); MIT, see LICENSE.
// Shared indoor sample and diagnostics. Consumers must check sampleFresh()
// before displaying/publishing; stored values can outlive a failed sensor read.
#include <Arduino.h>
#include <time.h>

// Read-only to consumers; acquisition is owned by IndoorSensor.cpp.
extern char FormattedTemperature[10], FormattedHumidity[10];
extern float humidity, temperature, lastValidCelsius;
extern bool dht_valid_temp, dht_valid_hum, haveSample;
extern uint32_t sensorReads, sensorErrors, lastValidSampleAt;
extern time_t lastValidSampleTime;
extern int lastSensorError;
void beginSensor();
void updateDHT();
bool sampleFresh();
void formatSampleTime(char* out, size_t size);
