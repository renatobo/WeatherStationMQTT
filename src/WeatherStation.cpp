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

#include <Arduino.h>
// #include <ESPWiFi.h>
// #include <ESPHTTPClient.h>
#include <Ticker.h>
#include <simpleDSTadjust.h>
#include <dhtnew.h>
#include "settings.h"
#include "version.h"
#include "device_security.h"
#include "TelemetryState.h"
#include <ESP8266HTTPClient.h>
#include "WeatherJson.h"
#include "WeatherTransport.h"
#include "WeatherTrust.h"
#include <ArduinoOTA.h>
#include <ESP8266mDNS.h>
#include <time.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>

// Show how long it has been running for
#include <uptime_formatter.h>

#ifdef i2cOLED
#include <SSD1306Wire.h>
// Pin definitions for I2C OLED
const int I2C_DISPLAY_ADDRESS = 0x3c;
const int SDA_PIN = MYSDA_PIN;
const int SDC_PIN = MYSDC_PIN;
#endif
/* Broken as of 2022-12-24, so removing
#ifdef brzo_i2c
// #include <SSD1306Brzo.h>
// Pin definitions for I2C OLED
// const int I2C_DISPLAY_ADDRESS = 0x3c;
const int SDA_PIN = MYSDA_PIN;
const int SDC_PIN = MYSDC_PIN;
#endif
*/
#include <OLEDDisplayUi.h>

// #include "WundergroundClient.h"
// RB Added 2019-03-16

#include "WeatherStationFonts.h"
#include "WeatherStationImages.h"
#include "DSEG7Classic-BoldFont.h"
// #include "ThingspeakClient.h"

// Add MQTT
#include <PubSubClient.h>

/* Broken library as 2022-12-24
 #ifdef brzo_i2c
SSD1306Brzo display(I2C_DISPLAY_ADDRESS, SDA_PIN, SDC_PIN); // I2C OLED with Brzo
#endif
*/
#ifdef i2cOLED
SSD1306Wire display(I2C_DISPLAY_ADDRESS, SDA_PIN, SDC_PIN); // I2C OLED
#endif

OLEDDisplayUi ui(&display);

Weather::Snapshot currentWeather;
bool weatherHasSample = false, weatherInProgress = false, weatherAttempted = false;
uint32_t lastWifiRetryAt = 0;
Weather::Error weatherError = Weather::Error::None;
Weather::Deadline* activeWeatherDeadline = nullptr;
uint32_t weatherAttempts = 0, weatherSuccesses = 0, weatherFailures = 0;
uint32_t weatherLastAttemptAt = 0, weatherLastSuccessAt = 0, weatherLastDuration = 0;
uint32_t weatherMaxDuration = 0;
int weatherHttpStatus = 0;
int weatherTlsError = 0;
const char* weatherStage = "not started";
const char* weatherValidation = "none";
char weatherBody[2049];
uint32_t healthLastSampleAt = 0, heapFree = 0, heapLargestBlock = 0;
uint32_t heapMinimum = UINT32_MAX, heapMinimumLargestBlock = UINT32_MAX;
uint8_t heapFragmentation = 0;

void sampleSystemHealth() {
  ESP.getHeapStats(&heapFree, &heapLargestBlock, &heapFragmentation);
  if (heapFree < heapMinimum) heapMinimum = heapFree;
  if (heapLargestBlock < heapMinimumLargestBlock) heapMinimumLargestBlock = heapLargestBlock;
  healthLastSampleAt = millis();
}

bool weatherFresh() {
  return weatherHasSample && weatherError == Weather::Error::None &&
      !Telemetry::elapsed(millis(), weatherLastSuccessAt, 1200000);
}

// Initialize the temperature/ humidity sensor
#if DHTTYPE == DHT22
#define DHTTEXT "DHT22"
#elif DHTTYPE == DHT21
#define DHTTEXT "DHT21"
#elif DHTTYPE == DHT11
#define DHTTEXT "DHT11"
#endif
char FormattedTemperature[10];
char FormattedHumidity[10];

DHTNEW mySensor(DHTPIN);

float humidity = 0.0;
float temperature = 0.0;
bool dht_valid_temp = false;
bool dht_valid_hum = false;

// flag changed in the ticker function every 10 minutes
volatile bool readyForWeatherUpdate = true;

String lastUpdate = "--";

Ticker tickerweather;
Ticker tickerdisplayon;

String hostname(HOSTNAME);

// MQTT initialize
WiFiClient espClient;
PubSubClient client(espClient);
Telemetry::SensorCycle sensorCycle;
Telemetry::ReconnectPolicy mqttReconnect;
Telemetry::PendingPair pendingSample;
Telemetry::PublishStats publishStats;
uint32_t sensorReads = 0, sensorErrors = 0, mqttConnectAttempts = 0;
uint32_t mqttDisconnects = 0, mqttMaxServiceGap = 0, mqttLastServiceAt = 0;
uint32_t lastValidSampleAt = 0, lastMqttScheduleAt = 0;
uint32_t mqttSchedules = 0, mqttSkippedSamples = 0;
time_t lastValidSampleTime = 0;
float lastValidCelsius = 0;
int lastSensorError = 0;
bool haveSample = false, mqttServiced = false, mqttWasConnected = false;
bool presence = false, presencePending = false, desiredPresence = false;
uint32_t lastPresenceAttempt = 0;
bool presenceAttempted = false;
volatile bool presenceTimeoutDue = false, displayOffDue = false;

bool sampleFresh() {
  return haveSample && dht_valid_temp && dht_valid_hum &&
      !Telemetry::elapsed(millis(), lastValidSampleAt, 120000);
}

void formatSampleTime(char* out, size_t size) {
  if (!haveSample || !Telemetry::timestamp(out, size, localtime(&lastValidSampleTime)))
    snprintf(out, size, "%s", "unavailable");
}

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
  htmlbody += client.connected() ? F("yes") : F("no");
  htmlbody += F("</p><p>MQTT connect attempts/disconnects: ");
  htmlbody += String(mqttConnectAttempts) + "/" + String(mqttDisconnects);
  htmlbody += F("</p><p>MQTT publish attempts/successes/failures: ");
  htmlbody += String(publishStats.attempts) + "/" + String(publishStats.successes) + "/" + String(publishStats.failures);
  htmlbody += F("</p><p>Pending sample: ");
  htmlbody += pendingSample.active ? F("yes") : F("no");
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

//declaring prototypes
void configModeCallback(WiFiManager *myWiFiManager);
void drawProgress(OLEDDisplay *display, int percentage, String label);
void drawOtaProgress(unsigned int, unsigned int);
void updateData(OLEDDisplay *display);
void drawDateTime(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
void drawCurrentWeather(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
#if defined(forecast_enable) || defined(forecast_enable_long)
void drawForecast(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
void drawForecastDetails(OLEDDisplay *display, int x, int y, int dayIndex);
#endif
#ifdef forecast_enable_long
void drawForecast2(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
#endif
void drawIndoor(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
void drawHeaderOverlay(OLEDDisplay *display, OLEDDisplayUiState *state);
void setReadyForWeatherUpdate();
void updateMQTT();
void mqttcallback(char *topic, byte *payload, unsigned int length);
void serviceMQTT();
int8_t getWifiQuality();
void updateMQTTpresence(bool ispresent);

// Add frames
// this array keeps function pointers to all frames
// frames are the single views that slide from right to left

#ifdef forecast_enable_long
// Version with 6 days of forecast
FrameCallback frames[] = {drawDateTime, drawCurrentWeather, drawIndoor, drawForecast, drawForecast2};
int numberOfFrames = 5;
#elif defined(forecast_enable)
// show only 3 days of forecast
FrameCallback frames[] = {drawDateTime, drawIndoor, drawCurrentWeather, drawForecast};
int numberOfFrames = 4;
#else
// show only 3 days of forecast
FrameCallback frames[] = {drawDateTime, drawIndoor, drawCurrentWeather};
int numberOfFrames = 3;
#endif

OverlayCallback overlays[] = {drawHeaderOverlay};
int numberOfOverlays = 1;

void setPresenceOff();
void setDisplayOff();

void setup()
{
  // Initialize DHT22 pins
  // pinMode(DHTPIN, OUTPUT);
  // digitalWrite(DHTPIN , LOW);
  // digitalWrite(DHTPIN , HIGH);
  Serial.begin(115200);

  // initialize display
  display.init();
  display.clear();
  display.display();

  // display.flipScreenVertically();  // Comment out to flip display 180deg
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setContrast(255);
  pinMode(LED_BUILTIN, OUTPUT); // turn on and off builtin led
#ifdef PIR_PRESENCE_CONTROL
  pinMode(PIR_PIN, INPUT); // read PIR status
#endif

  // Credit where credit is due
  display.drawXbm(-6, 5, WiFi_Logo_width, WiFi_Logo_height, WiFi_Logo_bits);
  display.drawString(86, 10, "Weather Station\nBy Squix78\nmods by Neptune\n@renatobonomini");
  display.display();

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;

  // Uncomment for testing wifi manager
  // wifiManager.resetSettings();
  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setDebugOutput(false);
  wifiManager.setShowPassword(false);
  wifiManager.setConnectTimeout(20);
  WiFi.hostname(hostname);
  WiFi.setAutoReconnect(true);

  // Timeout after 5 minutes: in case of power failure, this prevents the device from being stuck on waiting for AP information
  wifiManager.setConfigPortalTimeout(300);

  //or use this for auto generated name ESP + ChipID
  if (!wifiManager.autoConnect(hostname.c_str(), PROVISIONING_PASSWORD)) {
    Serial.println("Provisioning window closed; retrying saved WiFi");
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.begin();
  }

  // Manual Wifi for debugging
  // WiFi.begin(SSID, PASSWORD);

  // hostname += String(ESP.getChipId(), HEX);
  WiFi.hostname(hostname);

  Serial.print("My hostname is: "+ hostname);

  lastWifiRetryAt = millis();

  ui.setTargetFPS(30);
  ui.setTimePerFrame(5 * 1000); // Setup frame display time to 10 sec

  //Hack until disableIndicator works:
  //Set an empty symbol
  ui.setActiveSymbol(emptySymbol);
  ui.setInactiveSymbol(emptySymbol);

  ui.disableIndicator();

  // Get NTP time
  configTime(UTC_OFFSET * 3600, 0, NTP_SERVERS);

  // You can change the transition that is used
  // SLIDE_LEFT, SLIDE_RIGHT, SLIDE_TOP, SLIDE_DOWN
  ui.setFrameAnimation(SLIDE_LEFT);

  ui.setFrames(frames, numberOfFrames);

  ui.setOverlays(overlays, numberOfOverlays);

// Setup OTA
#ifdef DEBUG
  Serial.println("Hostname: " + hostname);
#endif
  ArduinoOTA.setHostname((const char *)hostname.c_str());
  ArduinoOTA.setPasswordHash(OTA_PASSWORD_HASH);
  ArduinoOTA.onProgress(drawOtaProgress);
  ArduinoOTA.begin();

  // setup mqtt
  client.setServer(mqtt_server, 1883);
  client.setCallback(mqttcallback);
  // RB Added 2022-12
  client.setKeepAlive(60);
  client.setSocketTimeout(1); // bounded MQTT packet/CONNACK wait, seconds
  espClient.setTimeout(250); // bounded DNS, TCP connect and write waits, milliseconds

  // Issues with some DTH22 sensors, so disabling IRQ
  mySensor.setDisableIRQ(true);
  mySensor.setType(DHTTYPE == DHT11 ? 11 : 22);
  mySensor.setWaitForReading(false);
  mySensor.setReadDelay(2500);
  sampleSystemHealth();

  tickerweather.attach(UPDATE_INTERVAL_SECS, setReadyForWeatherUpdate);
  lastMqttScheduleAt = millis();
#ifdef PIR_PRESENCE_CONTROL
  presenceTimeoutDue = true;
#endif

#ifdef INTERNAL_WEBSERVER
  server.on("/", handleRoot);
  server.on("/temp",handleAPItemp);
  server.on("/info",handleInfo);
  server.begin();
#endif
}

void updateDHT();

void loop()
{
  if (WiFi.status() != WL_CONNECTED && Telemetry::elapsed(millis(), lastWifiRetryAt, 30000)) {
    lastWifiRetryAt = millis();
    WiFi.begin();
  }

  if (Telemetry::elapsed(millis(), healthLastSampleAt, 1000)) sampleSystemHealth();
  serviceMQTT();
  updateDHT();
  if (Telemetry::elapsed(millis(), lastMqttScheduleAt, UPDATE_MQTT_INTERVAL_SECS * 1000UL)) {
    lastMqttScheduleAt = millis();
    updateMQTT();
  }
  pendingSample.flush(client, millis(), MQTT_OUT_TOPIC_TEMP, MQTT_OUT_TOPIC_HUM, publishStats);
  if (presencePending && client.connected() &&
      (!presenceAttempted || Telemetry::elapsed(millis(), lastPresenceAttempt, 1000))) {
    presenceAttempted = true;
    lastPresenceAttempt = millis();
    ++publishStats.attempts;
    if (client.publish(MQTT_OUT_TOPIC_PRESENCE, desiredPresence ? "TRUE" : "FALSE")) {
      ++publishStats.successes;
      presencePending = false;
    } else ++publishStats.failures;
  }

  ArduinoOTA.handle();
#ifdef INTERNAL_WEBSERVER
  server.handleClient();
#endif

  bool weatherRetry = weatherAttempted && weatherError != Weather::Error::None &&
      Telemetry::elapsed(millis(), weatherLastAttemptAt, 60000);
  if (!weatherInProgress && (readyForWeatherUpdate || weatherRetry) && client.connected() &&
      !pendingSample.active) updateData(&display);

  int remainingTimeBudget = ui.update();

  if (remainingTimeBudget > 0)
  {
    delay(remainingTimeBudget < 5 ? remainingTimeBudget : 5);
  } else yield();
#ifdef PIR_PRESENCE_CONTROL
  if (presenceTimeoutDue) {
    presenceTimeoutDue = false;
    if (digitalRead(PIR_PIN) == HIGH) {
      tickerdisplayon.once_scheduled(10, setPresenceOff);
    } else {
      presence = false;
      digitalWrite(LED_BUILTIN, HIGH);
      display.setContrast(10, 5, 0);
      tickerdisplayon.once_scheduled(10, setDisplayOff);
    }
  }
  if (displayOffDue) {
    displayOffDue = false;
    if (!presence) {
      display.displayOff();
      updateMQTTpresence(false);
    }
  }
  if (!presence && digitalRead(PIR_PIN) == HIGH)
  {
    // presence detected
    presence = true;
    display.displayOn();
    display.setBrightness(255);
    digitalWrite(LED_BUILTIN, LOW);
    tickerdisplayon.once_scheduled(10, setPresenceOff);
    // send a MQTT message to signal presence
    updateMQTTpresence(true);
  }
#endif
}

void mqttcallback(char *topic, byte *payload, unsigned int length)
{
#ifdef DEBUG
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (unsigned int i = 0; i < length; i++)
  {
    Serial.print((char)payload[i]);
  }
  Serial.println();
#endif
}

void configModeCallback(WiFiManager *myWiFiManager)
{
#ifdef DEBUG
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  //if you used auto generated SSID, print it
  Serial.println(myWiFiManager->getConfigPortalSSID());
#endif
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_10);
  display.drawString(64, 10, "Wifi Manager");
  display.drawString(64, 20, "Please connect to AP");
  display.drawString(64, 30, myWiFiManager->getConfigPortalSSID());
  display.drawString(64, 40, "To setup Wifi Configuration");
  display.display();
}

void drawProgress(OLEDDisplay *display, int percentage, String label)
{
  display->clear();
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_10);
  display->drawString(64, 10, label);
  display->drawProgressBar(2, 28, 124, 12, percentage);
  display->display();
}

void drawOtaProgress(unsigned int progress, unsigned int total)
{
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_10);
  display.drawString(64, 10, "OTA Update");
  unsigned int percentage = total ? static_cast<uint64_t>(progress) * 100 / total : 0;
  display.drawProgressBar(2, 28, 124, 12, percentage > 100 ? 100 : percentage);
  display.display();
}

// Cooperative application service while HTTP headers/body are arriving.
void serviceDuringWeather()
{
  sampleSystemHealth();
  if (heapFree < 6144 && activeWeatherDeadline) {
    activeWeatherDeadline->error = Weather::Error::LowMemory;
    return;
  }
  serviceMQTT();
  updateDHT();
  if (Telemetry::elapsed(millis(), lastMqttScheduleAt, UPDATE_MQTT_INTERVAL_SECS * 1000UL)) {
    lastMqttScheduleAt = millis();
    updateMQTT();
  }
  pendingSample.flush(client, millis(), MQTT_OUT_TOPIC_TEMP, MQTT_OUT_TOPIC_HUM, publishStats);
  ArduinoOTA.handle();
#ifdef INTERNAL_WEBSERVER
  server.handleClient();
#endif
  ui.update();
  yield();
}

void updateData(OLEDDisplay*)
{
  readyForWeatherUpdate = false;
  weatherAttempted = true;
  weatherLastAttemptAt = millis();
  weatherHttpStatus = 0;
  if (!std::isfinite(static_cast<double>(WEATHER_LATITUDE)) ||
      !std::isfinite(static_cast<double>(WEATHER_LONGITUDE)) ||
      WEATHER_LATITUDE < -90 || WEATHER_LATITUDE > 90 || WEATHER_LONGITUDE < -180 || WEATHER_LONGITUDE > 180) {
    weatherError = Weather::Error::Schema;
    return;
  }
  if (time(nullptr) < 1577836800) {
    weatherError = Weather::Error::Clock;
    return;
  }
  sampleSystemHealth();
  if (heapFree < 26000 || heapLargestBlock < 10000) {
    weatherError = Weather::Error::LowMemory;
    return;
  }
  weatherInProgress = true;
  ++weatherAttempts;
  Weather::Deadline deadline(millis());
  activeWeatherDeadline = &deadline;
  static BearSSL::X509List trust(WEATHER_ROOT_CA);
  Weather::TlsClient secure(deadline, serviceDuringWeather, &trust);
  HTTPClient http;
  Weather::Snapshot candidate;
  bool candidateValid = false;
  String zone(WEATHER_TIMEZONE);
  for (const char* c = WEATHER_TIMEZONE; *c; ++c) {
    if (!std::isalnum(static_cast<unsigned char>(*c)) && *c != '/' && *c != '_' && *c != '+' && *c != '-') {
      deadline.error = Weather::Error::Schema;
      break;
    }
  }
  zone.replace("+", "%2B");
  zone.replace("/", "%2F");
  char url[384];
  int written = snprintf(url, sizeof(url),
      "https://api.open-meteo.com/v1/forecast?latitude=%.5f&longitude=%.5f"
      "&current=temperature_2m,weather_code,is_day&daily=weather_code,temperature_2m_max,temperature_2m_min"
      "&forecast_days=%u&temperature_unit=%s&timezone=%s", static_cast<double>(WEATHER_LATITUDE),
      static_cast<double>(WEATHER_LONGITUDE), MAX_FORECASTS, IS_METRIC ? "celsius" : "fahrenheit", zone.c_str());
  Weather::Error attemptError = Weather::Error::Connect;
  if (deadline.error == Weather::Error::None && written > 0 && static_cast<size_t>(written) < sizeof(url) && http.begin(secure, url)) {
    http.setReuse(false);
    http.useHTTP10(true);
    http.setTimeout(250);
    http.addHeader(F("Accept-Encoding"), F("identity"));
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http.setUserAgent(F("WeatherStationMQTT/" FIRMWARE_VERSION));
    weatherHttpStatus = http.GET();
    if (weatherHttpStatus == 200) {
      deadline.stage = "HTTP body";
      Weather::Body body(weatherBody, sizeof(weatherBody), deadline);
      int received = http.writeToStream(&body); // SDK decodes content-length/chunked framing
      sampleSystemHealth();
      if (deadline.error != Weather::Error::None) attemptError = deadline.error;
      else if (received < 0 || static_cast<size_t>(received) != body.used()) attemptError = Weather::Error::Truncated;
      else if (!Weather::decode(weatherBody, body.used(), IS_METRIC, candidate, MAX_FORECASTS, &weatherValidation)) {
        deadline.stage = "JSON validation";
        attemptError = Weather::Error::Json;
      }
      else {
        attemptError = Weather::Error::None;
        candidateValid = true;
      }
    } else attemptError = deadline.error != Weather::Error::None ? deadline.error :
        (weatherHttpStatus < 0 ? Weather::Error::Connect : Weather::Error::Http);
    http.end();
  }
  weatherError = deadline.error != Weather::Error::None ? deadline.error : attemptError;
  if (static_cast<uint32_t>(millis() - deadline.started) >= 8000) weatherError = Weather::Error::TotalTimeout;
  if (candidateValid && weatherError == Weather::Error::None) {
    currentWeather = candidate;
    weatherHasSample = true;
    weatherLastSuccessAt = millis();
    ++weatherSuccesses;
  }
  if (weatherError != Weather::Error::None) ++weatherFailures;
  activeWeatherDeadline = nullptr;
  weatherTlsError = deadline.tlsError;
  weatherStage = deadline.stage;
  weatherInProgress = false;
  weatherLastDuration = millis() - deadline.started;
  if (weatherLastDuration > weatherMaxDuration) weatherMaxDuration = weatherLastDuration;
  sampleSystemHealth();
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

// Retain one latest scheduled sample; its timestamp is acquisition time.
void updateMQTT()
{
  ++mqttSchedules;
  if (!sampleFresh()) {
    ++mqttSkippedSamples;
    return;
  }
  if (!pendingSample.stage(localtime(&lastValidSampleTime), lastValidSampleAt,
      lastValidCelsius, humidity, IS_METRIC, MQTT_OUT_SENSOR_TEMP, MQTT_OUT_SENSOR_HUM,
      MQTT_OUT_TOPIC_TEMP, MQTT_OUT_TOPIC_HUM, client.getBufferSize(), publishStats))
    ++mqttSkippedSamples;
}

// Presence events are queued; timer callbacks never touch sockets.
void updateMQTTpresence(bool ispresent)
{
  desiredPresence = ispresent;
  presencePending = true;
  presenceAttempted = false;
}

void serviceMQTT()
{
  uint32_t now = millis();
  if (mqttServiced) {
    uint32_t gap = now - mqttLastServiceAt;
    if (gap > mqttMaxServiceGap) mqttMaxServiceGap = gap;
  }
  mqttServiced = true;
  mqttLastServiceAt = now;
  bool wifi = WiFi.status() == WL_CONNECTED;
  if (!wifi) espClient.stop();
  if (client.connected()) client.loop();
  bool connected = client.connected();
  if (mqttWasConnected && !connected) ++mqttDisconnects;
  if (mqttReconnect.due(millis(), wifi, connected)) {
    ++mqttConnectAttempts;
    connected = client.connect(hostname.c_str());
    mqttReconnect.finish(millis(), connected, random(251));
  }
  mqttWasConnected = connected;
}

void drawDateTime(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
  char *dstAbbrev;
  char time_str[32];
  time_t now = dstAdjusted.time(&dstAbbrev);
  struct tm *timeinfo = localtime(&now);

  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_10);
  String date = ctime(&now);
  date = date.substring(0, 11) + String(1900 + timeinfo->tm_year);
  // int textWidth = display->getStringWidth(date);
  display->drawString(64 + x, 5 + y, date);
  display->setFont(DSEG7_Classic_Bold_21);
  display->setTextAlignment(TEXT_ALIGN_RIGHT);

#ifdef STYLE_24HR
  Telemetry::clockText(time_str, sizeof(time_str), timeinfo, true, false);
  display->drawString(108 + x, 19 + y, time_str);
#else
  Telemetry::clockText(time_str, sizeof(time_str), timeinfo, false, false);
  display->drawString(101 + x, 19 + y, time_str);
#endif

  display->setTextAlignment(TEXT_ALIGN_LEFT);
  display->setFont(ArialMT_Plain_10);
#ifdef STYLE_24HR
  snprintf(time_str, sizeof(time_str), "%s", dstAbbrev ? dstAbbrev : "?");
  display->drawString(108 + x, 27 + y, time_str); // Known bug: Cuts off 4th character of timezone abbreviation
#else
  snprintf(time_str, sizeof(time_str), "%s\n%s", dstAbbrev ? dstAbbrev : "?", timeinfo->tm_hour >= 12 ? "pm" : "am");
  display->drawString(102 + x, 18 + y, time_str);
#endif
}

void drawCurrentWeather(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
  display->setFont(ArialMT_Plain_10);
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  String description = weatherHasSample ? String(Weather::description(currentWeather.current.code)) : String("Weather unavailable");
  if (weatherHasSample && !weatherFresh()) description = "Stale: " + description;
  display->drawString(64 + x, 38 + y, description);
  display->setFont(ArialMT_Plain_24);
  display->setTextAlignment(TEXT_ALIGN_LEFT);
  String temp = weatherHasSample ? String(currentWeather.current.temperature, 1) + (IS_METRIC ? "°C" : "°F") : String("n/a");
  display->drawString(60 + x, 5 + y, temp);
  display->setFont(Meteocons_Plain_36);
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  if (weatherHasSample) display->drawString(32 + x, 0 + y, String(Weather::glyph(currentWeather.current.code, currentWeather.current.day)));
}

void drawIndoor(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_10);
  display->drawString(64 + x, 0, DHTTEXT " Indoor Sensor");
  display->setFont(ArialMT_Plain_16);
  display->drawString(64 + x, 12, "Temp: " + (sampleFresh() ? String(FormattedTemperature) : String("n/a")) + (IS_METRIC ? "°C" : "°F"));
  display->drawString(64 + x, 30, "Humidity: " + (sampleFresh() ? String(FormattedHumidity) : String("n/a")) + "%");
}

#if defined(forecast_enable) || defined(forecast_enable_long)
void drawForecast(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
  drawForecastDetails(display, x, y, 0);
  drawForecastDetails(display, x + 44, y, 1);
  drawForecastDetails(display, x + 88, y, 2);
}
void drawForecastDetails(OLEDDisplay *display, int x, int y, int dayIndex)
{
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_10);
  if (!weatherHasSample || dayIndex >= currentWeather.count) {
    display->drawString(x + 20, y + 34, "n/a");
    return;
  }
  const Weather::Forecast& forecast = currentWeather.days[dayIndex];
  display->drawString(x + 20, y, WDAY_NAMES[forecast.weekday]);
  display->setFont(Meteocons_Plain_21);
  display->drawString(x + 20, y + 12, String(Weather::glyph(forecast.code, true)));
  display->setFont(ArialMT_Plain_10);
  display->drawString(x + 20, y + 34, String(forecast.high, 0) + "/" + String(forecast.low, 0) + (IS_METRIC ? "C" : "F"));
  display->setTextAlignment(TEXT_ALIGN_LEFT);
}
#ifdef forecast_enable_long
void drawForecast2(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
  drawForecastDetails(display, x, y, 3);
  drawForecastDetails(display, x + 44, y, 4);
  drawForecastDetails(display, x + 88, y, 5);
}
#endif

#endif

void drawHeaderOverlay(OLEDDisplay *display, OLEDDisplayUiState *state)
{
  char time_str[32];
  time_t now = dstAdjusted.time(nullptr);
  struct tm *timeinfo = localtime(&now);

  display->setFont(ArialMT_Plain_10);

#ifdef STYLE_24HR
  Telemetry::clockText(time_str, sizeof(time_str), timeinfo, true, false);
#else
  Telemetry::clockText(time_str, sizeof(time_str), timeinfo, false, true);
#endif

  display->setTextAlignment(TEXT_ALIGN_LEFT);
  display->drawString(5, 52, time_str);

  display->setTextAlignment(TEXT_ALIGN_CENTER);
  String temp = (sampleFresh() ? String(FormattedTemperature) : String("n/a")) + (IS_METRIC ? "°C" : "°F");
  display->drawString(101, 52, temp);

  int8_t quality = getWifiQuality();
  for (int8_t i = 0; i < 4; i++)
  {
    for (int8_t j = 0; j < 2 * (i + 1); j++)
    {
      if (quality > i * 25 || j == 0)
      {
        display->setPixel(120 + 2 * i, 61 - j);
      }
    }
  }

  display->drawHorizontalLine(0, 51, 128);
}

// converts the dBm to a range between 0 and 100%
int8_t getWifiQuality()
{
  int32_t dbm = WiFi.RSSI();
  if (dbm <= -100)
  {
    return 0;
  }
  else if (dbm >= -50)
  {
    return 100;
  }
  else
  {
    return 2 * (dbm + 100);
  }
}

void setReadyForWeatherUpdate()
{
  readyForWeatherUpdate = true;
}

#ifdef PIR_PRESENCE_CONTROL
void setPresenceOff()
{
  presenceTimeoutDue = true;
}

void setDisplayOff()
{
  displayOffDue = true;
}
#endif
