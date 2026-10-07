// SPDX-License-Identifier: MIT
// Derived from the ThingPulse weather station; see LICENSE for copyright and attribution.
// Project modifications and modernization by Renato Bonomini (renatobo).
//
// DHT acquisition, validation and last-good indoor sample storage.
// Only this module reads the sensor or changes sample state. Display, HTTP
// and MQTT consume the same reading, units and acquisition timestamp.

#include "IndoorSensor.h"
#include "settings.h"
#include "TelemetryState.h"
#include "dhtnew.h"

// Initialize the temperature/ humidity sensor
char FormattedTemperature[10];
char FormattedHumidity[10];

static DHTNEW mySensor(DHTPIN);

float humidity = 0.0;
float temperature = 0.0;
bool dht_valid_temp = false;
bool dht_valid_hum = false;

static Telemetry::SensorCycle sensorCycle;
uint32_t sensorReads = 0, sensorErrors = 0;
uint32_t lastValidSampleAt = 0;
time_t lastValidSampleTime = 0;
float lastValidCelsius = 0;
int lastSensorError = 0;
bool haveSample = false;

// A failed current read invalidates display/publishing immediately. The stored
// last-good values remain available for diagnostics; use expires after two minutes.
bool sampleFresh() {
  return haveSample && dht_valid_temp && dht_valid_hum &&
      !Telemetry::elapsed(millis(), lastValidSampleAt, 120000);
}

void formatSampleTime(char* out, size_t size) {
  if (!haveSample || !Telemetry::timestamp(out, size, localtime(&lastValidSampleTime)))
    snprintf(out, size, "%s", "unavailable");
}

void beginSensor() {
  // Issues with some DTH22 sensors, so disabling IRQ
  mySensor.setDisableIRQ(true);
  mySensor.setType(DHTTYPE == DHT11 ? 11 : 22);
  mySensor.setWaitForReading(false);
  mySensor.setReadDelay(2500);
}

// One bounded read attempt; failed reads are spaced by SensorCycle.
void updateDHT()
{
  uint32_t now = millis();
  // One cycle per minute, up to three attempts at least 2.5 seconds apart.
  // Never spin waiting for a sensor retry: other loop services must keep running.
  if (!sensorCycle.due(now)) return;
  ++sensorReads;
  lastSensorError = mySensor.read();
  float celsius = mySensor.getTemperature();
  float rh = mySensor.getHumidity();
  bool valid = lastSensorError == DHTLIB_OK && Telemetry::validReading(celsius, rh);
  if (lastSensorError == DHTLIB_OK && !valid) lastSensorError = -100;
  sensorCycle.finish(millis(), valid);
  dht_valid_temp = dht_valid_hum = valid;
  if (!valid) {
    ++sensorErrors;
    return;
  }
  haveSample = true;
  lastValidSampleAt = millis();
  // Capture acquisition time, not the later MQTT transmission time. Keep Celsius
  // alongside converted display temperature so pending payload units stay correct.
  lastValidSampleTime = dstAdjusted.time(nullptr);
  lastValidCelsius = celsius;
  temperature = Telemetry::outputTemperature(celsius, IS_METRIC);
  humidity = rh;
  snprintf(FormattedTemperature, sizeof(FormattedTemperature), "%4.1f", temperature);
  snprintf(FormattedHumidity, sizeof(FormattedHumidity), "%4.1f", humidity);
}
