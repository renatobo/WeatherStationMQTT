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

#include "HttpDiagnostics.h"
#include <ESP8266WiFi.h>
#include "settings.h"
#include "version.h"
#include "IndoorSensor.h"
#include "MqttTelemetry.h"
#include "DeviceNetwork.h"
#include "WeatherService.h"
#include "SystemHealth.h"
#include "ESP8266WebServer.h"
#include "uptime_formatter.h"

#ifdef INTERNAL_WEBSERVER
// Internal webserver to display data on demand
ESP8266WebServer server(80);

void handleRoot()
{
    //compute datestring
  char time_str[20];
  formatSampleTime(time_str, sizeof(time_str));

  String htmlbody((char *)0);
  htmlbody += "<h1>";
  htmlbody += hostname;
  htmlbody += "</h1><p>Temp: ";
  htmlbody += sampleFresh() ? String(temperature) : "n/a";
#ifdef METRIC
  htmlbody += " C<br> RelHum:";
#else
  htmlbody += " F<br> RelHum: ";
#endif
  htmlbody += sampleFresh() ? String(humidity) : "n/a";
  htmlbody += " %</br>Sample Time: ";
  htmlbody += time_str;
  htmlbody += "</p><p><a href=/info>Info</a></p>";
  server.send(200, F("text/html"), htmlbody);
}

void handleAPItemp()
{
  String jsonbody((char *)0);
  jsonbody += "{\"temp\": ";
  jsonbody += sampleFresh() ? String(temperature) : "\"n/a\"";
  jsonbody += "}";
  server.send(200, F("text/json"), jsonbody);
}

void handleInfo()
{
  sampleSystemHealth();
  String htmlbody((char *)0);
  htmlbody.reserve(4096);
  String mqtttemp = MQTT_OUT_TOPIC_TEMP;
  String mqtthum = MQTT_OUT_TOPIC_HUM;

  htmlbody += "<h1>";
  htmlbody += hostname;
  htmlbody += "</h1><p>MQTT topics:<ul><li>temperature: "+mqtttemp;
  htmlbody += "</li><li>humidity: "+mqtthum;
  #ifdef PIR_PRESENCE_CONTROL
  String mqttpresence = MQTT_OUT_TOPIC_PRESENCE;
  htmlbody += "</li><li>presence: "+mqttpresence;
  #else
  htmlbody += "</li><li>presence: not compiled";
  #endif
  htmlbody += "</li></ul><p>Device: " + String(ESP.getChipId(), HEX) + "</p><p>Device uptime: ";
  htmlbody += uptime_formatter::getUptime();
  htmlbody += F("</p><p>Firmware version: <a href=\"" FIRMWARE_TAG_URL "\">" FIRMWARE_TAG "</a>");
  htmlbody += F("</p><p>OTA authentication: enabled");
  htmlbody += F("</p><p>Provisioning AP: password protected, five-minute startup window");
  htmlbody += "</p><p>SW build date: ";
  htmlbody += F(__DATE__ " " __TIME__);
  htmlbody += F("</p><p>Device profile: ");
#if defined(office)
  htmlbody += F("office");
#elif defined(workshop)
  htmlbody += F("workshop");
#elif defined(Printer3d)
  htmlbody += F("Printer3d");
#else
  htmlbody += F("unknown");
#endif
  htmlbody += F("</p><p>Sketch MD5: ");
  String sketchMD5 = ESP.getSketchMD5();
  htmlbody += sketchMD5.length() == 32 ? sketchMD5 : String(F("unavailable"));
  htmlbody += F("</p><p>Reset reason: ");
  htmlbody += ESP.getResetReason();
  htmlbody += F("</p><p>Wi-Fi RSSI: ");
  if (WiFi.status() == WL_CONNECTED) {
    htmlbody += String(WiFi.RSSI());
    htmlbody += F(" dBm");
  } else {
    htmlbody += F("disconnected");
  }
  htmlbody += F("</p>");
  char sampleTime[20];
  formatSampleTime(sampleTime, sizeof(sampleTime));
  htmlbody += F("<p>Last valid sample time: ");
  htmlbody += sampleTime;
  htmlbody += F("</p><p>Sample age seconds: ");
  htmlbody += haveSample ? String(static_cast<uint32_t>(millis() - lastValidSampleAt) / 1000) : String(F("unavailable"));
  htmlbody += F("</p><p>Current sample valid: ");
  htmlbody += sampleFresh() ? F("yes") : F("no");
  htmlbody += F("</p><p>Sensor reads/errors: ");
  htmlbody += String(sensorReads) + "/" + String(sensorErrors);
  htmlbody += F("</p><p>Last sensor error: ");
  htmlbody += String(lastSensorError);
  htmlbody += F("</p><p>MQTT connected: ");
  htmlbody += mqttConnected() ? F("yes") : F("no");
  htmlbody += F("</p><p>MQTT connect attempts/disconnects: ");
  htmlbody += String(mqttConnectAttempts) + "/" + String(mqttDisconnects);
  htmlbody += F("</p><p>MQTT publish attempts/successes/failures: ");
  htmlbody += String(publishStats.attempts) + "/" + String(publishStats.successes) + "/" + String(publishStats.failures);
  htmlbody += F("</p><p>Pending sample: ");
  htmlbody += mqttSamplePending() ? F("yes") : F("no");
  htmlbody += F("</p><p>MQTT scheduled/skipped samples: ");
  htmlbody += String(mqttSchedules) + "/" + String(mqttSkippedSamples);
  htmlbody += F("</p><p>Pending replacements/expired/format errors: ");
  htmlbody += String(publishStats.replaced) + "/" + String(publishStats.expired) + "/" + String(publishStats.formatErrors);
  htmlbody += F("</p><p>MQTT maximum service gap ms: ");
  htmlbody += String(mqttMaxServiceGap);
  htmlbody += F("</p><p>Weather provider: <a href=\"https://open-meteo.com/\">Open-Meteo</a>");
  htmlbody += F("</p><p>Weather state: ");
  htmlbody += weatherInProgress ? F("updating") : (weatherFresh() ? F("fresh") : (weatherHasSample ? F("stale") : F("unavailable")));
  htmlbody += F("</p><p>Weather attempts/successes/failures: ");
  htmlbody += String(weatherAttempts) + "/" + String(weatherSuccesses) + "/" + String(weatherFailures);
  htmlbody += F("</p><p>Weather last error: ");
  htmlbody += Weather::errorName(weatherError);
  htmlbody += F("</p><p>Weather HTTP status: ");
  htmlbody += String(weatherHttpStatus);
  htmlbody += F("</p><p>Weather TLS error: ");
  htmlbody += String(weatherTlsError);
  htmlbody += F("</p><p>Weather request stage: ");
  htmlbody += weatherStage;
  htmlbody += F("</p><p>Weather validation: ");
  htmlbody += weatherValidation;
  htmlbody += F("</p><p>Weather last/max duration ms: ");
  htmlbody += String(weatherLastDuration) + "/" + String(weatherMaxDuration);
  htmlbody += F("</p><p>Weather last good age seconds: ");
  htmlbody += weatherHasSample ? String(static_cast<uint32_t>(millis() - weatherLastSuccessAt) / 1000) : String(F("unavailable"));
  htmlbody += F("</p><p>Weather observation local time: ");
  htmlbody += weatherHasSample ? currentWeather.current.time : "unavailable";
  htmlbody += F("</p><p>Weather time zone: ");
  htmlbody += WEATHER_TIMEZONE;
  htmlbody += F("</p><p>Heap free/minimum bytes: ");
  htmlbody += String(heapFree) + "/" + String(heapMinimum);
  htmlbody += F("</p><p>Heap largest/minimum block bytes: ");
  htmlbody += String(heapLargestBlock) + "/" + String(heapMinimumLargestBlock);
  htmlbody += F("</p><p>Heap fragmentation percent: ");
  htmlbody += String(heapFragmentation);
  htmlbody += F("</p><p>Free continuation stack bytes: ");
  htmlbody += String(ESP.getFreeContStack());
  htmlbody += F("</p><p>Last reset details: ");
  htmlbody += ESP.getResetInfo();
  htmlbody += F("</p>");
  server.send(200, F("text/html"), htmlbody);
}

#endif

void beginHTTP() {
#ifdef INTERNAL_WEBSERVER
  server.on("/", handleRoot);
  server.on("/temp",handleAPItemp);
  server.on("/info",handleInfo);
  server.begin();
#endif
}

void serviceHTTP() {
#ifdef INTERNAL_WEBSERVER
  server.handleClient();
#endif
}
