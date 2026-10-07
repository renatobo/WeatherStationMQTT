/**The MIT License (MIT)

Copyright (c) 2018 by Daniel Eichhorn - ThingPulse

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

See more at https://thingpulse.com
*/

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
  lastValidSampleTime = dstAdjusted.time(nullptr);
  lastValidCelsius = celsius;
  temperature = Telemetry::outputTemperature(celsius, IS_METRIC);
  humidity = rh;
  snprintf(FormattedTemperature, sizeof(FormattedTemperature), "%4.1f", temperature);
  snprintf(FormattedHumidity, sizeof(FormattedHumidity), "%4.1f", humidity);
}
