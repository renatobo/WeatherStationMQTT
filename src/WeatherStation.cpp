// SPDX-License-Identifier: MIT
// Derived from the ThingPulse weather station; see LICENSE for copyright and attribution.
// Project modifications and modernization by Renato Bonomini (renatobo).
//
// Application startup and cooperative service orchestration.
// Services run in a fixed order on the main loop. Weather requests reuse
// that service sequence without starting another request or processing PIR display events.

/* Customizations by Neptune (NeptuneEng on Twitter, Neptune2 on Github)
 *
 *  Added Wifi Splash screen and credit to Squix78
 *  Modified progress bar to a thicker and symmetrical shape
 *  Replaced TimeClient with built-in lwip sntp client (no need for external ntp client library)
 *  Added Daylight Saving Time Auto adjuster with DST rules using simpleDSTadjust library
 *  https://github.com/neptune2/simpleDSTadjust
 *  Added Setting examples for Boston, Zurich and Sydney
  *  Selectable NTP servers for each locale
  *  DST rules and timezone settings customizable for each locale
   *  See https://www.timeanddate.com/time/change/ for DST rules
  *  Added AM/PM or 24-hour option for each locale
 *  Changed to 7-segment Clock font from http://www.keshikan.net/fonts-e.html
 *  Added Forecast screen for days 4-6 (requires 1.1.3 or later version of esp8266_Weather_Station library)
 *  Added support for DHT22, DHT21 and DHT11 Indoor Temperature and Humidity Sensors
 *  Fixed bug preventing display.flipScreenVertically() from working
 *  Slight adjustment to overlay
 */

/* Additions by Renato Bonomini, renatobo on github
*
* Added MQTT client
* Added simple web page with temp and humidity
* Added PIR sensor so that display is off unless someone is in front of it
*/

#include "settings.h"
#include "TelemetryState.h"
#include "SystemHealth.h"
#include "IndoorSensor.h"
#include "MqttTelemetry.h"
#include "DeviceNetwork.h"
#include "HttpDiagnostics.h"
#include "WeatherService.h"
#include "StationDisplay.h"

void setup() {
  Serial.begin(115200);
  beginDisplay();
  beginWiFi();
  beginDisplayFrames();
  configTime(UTC_OFFSET * 3600, 0, NTP_SERVERS);
  beginOTA();
  beginMQTT();
  beginSensor();
  sampleSystemHealth();
  beginWeather();
  beginHTTP();
}

// Cooperative service order retained during HTTP headers/body reads.
void serviceDuringWeatherRequest() {
  serviceMQTT();
  updateDHT();
  serviceMqttSamples();
  serviceLocalNetwork();
  updateDisplayFrames();
  yield();
}

void loop() {
  serviceWiFi();
  if (Telemetry::elapsed(millis(), healthLastSampleAt, 1000)) sampleSystemHealth();
  serviceMQTT();
  updateDHT();
  serviceMqttSamples();
  serviceMqttPresence();
  serviceLocalNetwork();
  serviceWeather(serviceDuringWeatherRequest);

  int remainingTimeBudget = updateDisplayFrames();
  if (remainingTimeBudget > 0) delay(remainingTimeBudget < 5 ? remainingTimeBudget : 5);
  else yield();
  serviceDisplayPresence();
}
