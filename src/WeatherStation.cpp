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
#include <JsonListener.h>
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
#include "OpenWeatherMapCurrent.h"
#include "OpenWeatherMapForecast.h"

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

// RB removed 2019-03-16
// // Initialize Wunderground client with METRIC setting
// WundergroundClient wunderground(IS_METRIC);
// RB added 2019-03-16
OpenWeatherMapCurrentData currentWeather;
OpenWeatherMapCurrent currentWeatherClient;

OpenWeatherMapForecastData forecasts[MAX_FORECASTS];
OpenWeatherMapForecast forecastClient;

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
bool readyForWeatherUpdate = false;
// flag changed in the ticker function every 1 minute
bool readyForDHTUpdate = false;
// flag changed in the ticker function every 5 minutes
bool readyForMQTTUpdate = false;

String lastUpdate = "--";

Ticker tickerdht;
Ticker tickerweather;
Ticker tickermqtt;
Ticker tickerdisplayon;

String hostname(HOSTNAME);

// MQTT initialize
WiFiClient espClient;
PubSubClient client(espClient);
long lastMsg = 0;
char mqttmsg[50];
int mqttcounter = 0;

#ifdef INTERNAL_WEBSERVER
// Internal webserver to display data on demand
ESP8266WebServer server(80);

void handleRoot()
{
    //compute datestring
  char time_str[18];
  time_t now = dstAdjusted.time(nullptr);
  struct tm *timeinfo = localtime(&now);
  snprintf(time_str, 20, "%04d-%02d-%02d %02d:%02d:%02d", timeinfo->tm_year + 1900,
           timeinfo->tm_mon + 1, timeinfo->tm_mday, timeinfo->tm_hour, timeinfo->tm_min,
           timeinfo->tm_sec);
  
  String htmlbody((char *)0);
  htmlbody += "<h1>";
  htmlbody += hostname;
  htmlbody += "</h1><p>Temp: ";
  htmlbody += dht_valid_temp ? String(temperature) : "n/a";
#ifdef METRIC
  htmlbody += " C<br> RelHum:";
#else
  htmlbody += " F<br> RelHum: ";
#endif
  htmlbody += dht_valid_hum ? String(humidity) : "n/a";
  htmlbody += " %</br>Sample Time: ";
  htmlbody += time_str;
  htmlbody += "</p><p><a href=/info>Info</a></p>";
  server.send(200, F("text/html"), htmlbody);
}

void handleAPItemp()
{
  String jsonbody((char *)0);
  jsonbody += "{\"temp\": ";
  jsonbody += dht_valid_temp ? String(temperature) : "\"n/a\"";
  jsonbody += "}";
  server.send(200, F("text/json"), jsonbody);
}

void handleInfo()
{
  String htmlbody((char *)0);
  String mqtttemp = MQTT_OUT_TOPIC_TEMP;
  String mqtthum = MQTT_OUT_TOPIC_HUM;

  htmlbody += "<h1>";
  htmlbody += hostname;
  htmlbody += "</h1><p>MQTT topics:<ul><li>temperature: "+mqtttemp;
  htmlbody += "</li><li>humidity: "+mqtthum;
  #ifdef PIR_PRESENCE_CONTROL
  String mqttpresence = MQTT_OUT_TOPIC_PRESENCE;
  htmlbody += "</li><li>presence: "+mqttpresence;
  #elif
  htmlbody += "</li><li>presence: not compiled";
  #endif
  htmlbody += "</li></ul><p>Device: " + String(ESP.getChipId(), HEX) + "</p><p>Device uptime: ";
  htmlbody += uptime_formatter::getUptime();
  htmlbody += "</p><p>SW build date: ";
  htmlbody += __TIMESTAMP__;
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
#ifdef forecast_enable
void drawForecast(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
void drawForecastDetails(OLEDDisplay *display, int x, int y, int dayIndex);
#endif
#ifdef forecast_enable_long
void drawForecast2(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
#endif
void drawIndoor(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
void drawHeaderOverlay(OLEDDisplay *display, OLEDDisplayUiState *state);
void setReadyForWeatherUpdate();
void setReadyForDHTUpdate();
void setReadyForMQTTUpdate();
void updateMQTT();
void mqttcallback(char *topic, byte *payload, unsigned int length);
void reconnectMQTT();
int8_t getWifiQuality();
void updateMQTTpresence(bool ispresent);

// Add frames
// this array keeps function pointers to all frames
// frames are the single views that slide from right to left

#ifdef forecast_enable_long
// Version with 6 days of forecast
FrameCallback frames[] = {drawDateTime, drawCurrentWeather, drawIndoor, drawForecast, drawForecast2};
int numberOfFrames = 5;
#endif

#ifdef forecast_enable
// show only 3 days of forecast
FrameCallback frames[] = {drawDateTime, drawIndoor, drawCurrentWeather, drawForecast};
int numberOfFrames = 4;
#endif

#ifndef forecast_enable
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
  // delay(DELAY_DHT22_READS);
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

  // Timeout after 5 minutes: in case of power failure, this prevents the device from being stuck on waiting for AP information
  wifiManager.setConfigPortalTimeout(300);

  //or use this for auto generated name ESP + ChipID
  if (!wifiManager.autoConnect()) {
    Serial.println("failed to connect and hit timeout");
    //reset and try again
    ESP.restart();
    delay(1000);
  }

  // Manual Wifi for debugging
  // WiFi.begin(SSID, PASSWORD);

  // hostname += String(ESP.getChipId(), HEX);
  WiFi.hostname(hostname);

  Serial.print("My hostname is: "+ hostname);

  int counter = 0;
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
    display.clear();
    display.drawString(64, 10, "Connecting to WiFi");
    display.drawXbm(46, 30, 8, 8, counter % 3 == 0 ? activeSymbol : inactiveSymbol);
    display.drawXbm(60, 30, 8, 8, counter % 3 == 1 ? activeSymbol : inactiveSymbol);
    display.drawXbm(74, 30, 8, 8, counter % 3 == 2 ? activeSymbol : inactiveSymbol);
    display.display();

    counter++;
  }

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
  ArduinoOTA.onProgress(drawOtaProgress);
  ArduinoOTA.begin();

  // setup mqtt
  client.setServer(mqtt_server, 1883);
  client.setCallback(mqttcallback);
  // RB Added 2022-12
  client.setKeepAlive(60);

  // Issues with some DTH22 sensors, so disabling IRQ
  mySensor.setDisableIRQ(true);
  // we just started, so we need to give about 2 seconds (DELAY_DHT22_READS) to DHT22 for the first reading
  if (millis() < (unsigned long)DELAY_DHT22_READS)
  {
    delay((unsigned long)DELAY_DHT22_READS - millis());
  }
  updateData(&display);

  tickerweather.attach(UPDATE_INTERVAL_SECS, setReadyForWeatherUpdate);
  tickerdht.attach(60, setReadyForDHTUpdate);
  tickermqtt.attach(UPDATE_MQTT_INTERVAL_SECS, setReadyForMQTTUpdate);
#ifdef PIR_PRESENCE_CONTROL
  setPresenceOff();
  tickerdisplayon.once_scheduled(10, setPresenceOff);
#endif

#ifdef INTERNAL_WEBSERVER
  server.on("/", handleRoot);
  server.on("/temp",handleAPItemp);
  server.on("/info",handleInfo);
  server.begin();
#endif
}

bool presence = true;
void updateDHT();

void loop()
{

  if (readyForWeatherUpdate && ui.getUiState()->frameState == FIXED)
  {
    updateData(&display);
  }

  if (readyForDHTUpdate && ui.getUiState()->frameState == FIXED)
  {
    updateDHT();
  }

  if (readyForMQTTUpdate && ui.getUiState()->frameState == FIXED)
  {
    updateMQTT();
  }

  int remainingTimeBudget = ui.update();

  if (remainingTimeBudget > 0)
  {
    // You can do some work here
    // Don't do stuff if you are below your
    // time budget.
    ArduinoOTA.handle();
    delay(remainingTimeBudget);
#ifdef INTERNAL_WEBSERVER
    server.handleClient();
#endif
  }
#ifdef PIR_PRESENCE_CONTROL
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
  display.drawProgressBar(2, 28, 124, 12, progress / (total / 100));
  display.display();
}

void updateData(OLEDDisplay *display)
{
  drawProgress(display, 10, "Updating time...");
  configTime(UTC_OFFSET * 3600, 0, NTP_SERVERS);
  // 2019-03-16
  drawProgress(display, 30, "Updating weather...");
  currentWeatherClient.setMetric(IS_METRIC);
  currentWeatherClient.setLanguage(OPEN_WEATHER_MAP_LANGUAGE);
  currentWeatherClient.updateCurrentById(&currentWeather, OPEN_WEATHER_MAP_APP_ID, OPEN_WEATHER_MAP_LOCATION_ID);

#ifdef forecast_enable
  drawProgress(display, 50, "Updating forecasts...");
  forecastClient.setMetric(IS_METRIC);
  forecastClient.setLanguage(OPEN_WEATHER_MAP_LANGUAGE);
  uint8_t allowedHours[] = {12};
  forecastClient.setAllowedHours(allowedHours, sizeof(allowedHours));
  forecastClient.updateForecastsById(forecasts, OPEN_WEATHER_MAP_APP_ID, OPEN_WEATHER_MAP_LOCATION_ID, MAX_FORECASTS);
#endif

  drawProgress(display, 80, "Upd. DHT Sensor...");

  updateDHT();

  readyForWeatherUpdate = false;
  drawProgress(display, 100, "Done...");
  delay(1000);
}

// Called every 1 minute
void updateDHT()
{

  int trials = DHT22_MAX_READINGS;
  dht_valid_temp = false;
  dht_valid_hum = false;

  while (!(dht_valid_temp && dht_valid_hum) && trials > 0)
  {
    trials--;
    // if we read less than DELAY_DHT22_READS ms, we wait
    uint64_t lastread = mySensor.lastRead();
    if (lastread < DELAY_DHT22_READS)
    {
      delay(DELAY_DHT22_READS - lastread);
    }
    mySensor.read();
    // temperature
    float temp_c_tmp = mySensor.getTemperature();
    if (temp_c_tmp > MIN_ALLOWED_TEMP_C && !dht_valid_temp)
    {
      dht_valid_temp = true;
      // store in F
      temperature = temp_c_tmp * 1.8 + 32;
      dtostrf(temperature, 4, 1, FormattedTemperature);
    }
    // humidity
    float humidity_tmp = mySensor.getHumidity();
    if (humidity_tmp > MIN_ALLOWED_HUM && !dht_valid_hum)
    {
      dht_valid_hum = true;
      humidity = humidity_tmp;
      dtostrf(humidity, 4, 1, FormattedHumidity);
    }
  }
}

// Called every 5 minuteS
void updateMQTT()
{
  if (!client.connected())
  {
    reconnectMQTT();
    client.loop();
  }

  snprintf(mqttmsg, 75, "MQTT message #%d", ++mqttcounter);
#ifdef DEBUG
  Serial.print("Publish message: ");
  Serial.println(mqttmsg);
#endif

  //compute datestring
  char time_str[18];
  time_t now = dstAdjusted.time(nullptr);
  struct tm *timeinfo = localtime(&now);
  snprintf(time_str, 20, "%04d-%02d-%02d %02d:%02d:%02d", timeinfo->tm_year + 1900,
           timeinfo->tm_mon + 1, timeinfo->tm_mday, timeinfo->tm_hour, timeinfo->tm_min,
           timeinfo->tm_sec);

  // client.publish("outTopic", mqttmsg);
  // Format of message is: ts;source;unit;value
  // Example: 2016-02-28 17:56:28;dht22_hum;relhum;37.4

  if (dht_valid_temp == true)
  {
    snprintf(mqttmsg, 75, "%s;%s;%s;%s", time_str, MQTT_OUT_SENSOR_TEMP, MQTT_OUT_UNIT_TEMP, FormattedTemperature);
#ifdef DEBUG
    Serial.println(mqttmsg);
#endif
    client.publish(MQTT_OUT_TOPIC_TEMP, mqttmsg);
  }

  if (dht_valid_hum == true)
  {
    snprintf(mqttmsg, 75, "%s;%s;%s;%s", time_str, MQTT_OUT_SENSOR_HUM, MQTT_OUT_UNIT_HUM, FormattedHumidity);
#ifdef DEBUG
    Serial.println(mqttmsg);
#endif
    client.publish(MQTT_OUT_TOPIC_HUM, mqttmsg);
  }

  readyForMQTTUpdate = false;
}

void updateMQTTpresence(bool ispresent) {
  if (!client.connected())
  {
    reconnectMQTT();
    client.loop();
  }
  if (ispresent) {
    client.publish(MQTT_OUT_TOPIC_PRESENCE, "TRUE");
  } else {
    client.publish(MQTT_OUT_TOPIC_PRESENCE, "FALSE");
  }
}

void reconnectMQTT()
{
  while (!client.connected())
  {
    Serial.print("Attempting MQTT connection to:");
    Serial.print(mqtt_server);
    // Attempt to connect
    if (client.connect(hostname.c_str()))
    {
#ifdef DEBUG
      Serial.println(" connected");
#endif

      // Once connected, publish an announcement...
      // client.publish(MQTT_OUT_TOPIC_TEMP, mqttmsg);
      // ... and resubscribe
      // client.subscribe("inTopic");
    }
    else
    {
#ifdef DEBUG
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
#endif
      // Wait 3 seconds before retrying
      delay(3000);
    }
  }
}

void drawDateTime(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
  char *dstAbbrev;
  char time_str[11];
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
  sprintf(time_str, "%02d:%02d:%02d\n", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
  display->drawString(108 + x, 19 + y, time_str);
#else
  int hour = (timeinfo->tm_hour + 11) % 12 + 1; // take care of noon and midnight
  sprintf(time_str, "%2d:%02d:%02d\n", hour, timeinfo->tm_min, timeinfo->tm_sec);
  display->drawString(101 + x, 19 + y, time_str);
#endif

  display->setTextAlignment(TEXT_ALIGN_LEFT);
  display->setFont(ArialMT_Plain_10);
#ifdef STYLE_24HR
  sprintf(time_str, "%s", dstAbbrev);
  display->drawString(108 + x, 27 + y, time_str); // Known bug: Cuts off 4th character of timezone abbreviation
#else
  sprintf(time_str, "%s\n%s", dstAbbrev, timeinfo->tm_hour >= 12 ? "pm" : "am");
  display->drawString(102 + x, 18 + y, time_str);
#endif
}

void drawCurrentWeather(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
  display->setFont(ArialMT_Plain_10);
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->drawString(64 + x, 38 + y, currentWeather.description);

  display->setFont(ArialMT_Plain_24);
  display->setTextAlignment(TEXT_ALIGN_LEFT);
  String temp = String(currentWeather.temp, 1) + (IS_METRIC ? "°C" : "°F");
  display->drawString(60 + x, 5 + y, temp);

  display->setFont(Meteocons_Plain_36);
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->drawString(32 + x, 0 + y, currentWeather.iconMeteoCon);
}

void drawIndoor(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_10);
  display->drawString(64 + x, 0, DHTTEXT " Indoor Sensor");
  display->setFont(ArialMT_Plain_16);
  dtostrf(temperature, 4, 1, FormattedTemperature);
  display->drawString(64 + x, 12, "Temp: " + String(FormattedTemperature) + (IS_METRIC ? "°C" : "°F"));
  dtostrf(humidity, 4, 1, FormattedHumidity);
  display->drawString(64 + x, 30, "Humidity: " + String(FormattedHumidity) + "%");
}

#ifdef forecast_enable
void drawForecast(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
  drawForecastDetails(display, x, y, 0);
  drawForecastDetails(display, x + 44, y, 1);
  drawForecastDetails(display, x + 88, y, 2);
}
void drawForecastDetails(OLEDDisplay *display, int x, int y, int dayIndex)
{
  time_t observationTimestamp = forecasts[dayIndex].observationTime;
  struct tm *timeInfo;
  timeInfo = localtime(&observationTimestamp);
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_10);
  display->drawString(x + 20, y, WDAY_NAMES[timeInfo->tm_wday]);

  display->setFont(Meteocons_Plain_21);
  display->drawString(x + 20, y + 12, forecasts[dayIndex].iconMeteoCon);
  String temp = String(forecasts[dayIndex].temp, 0) + (IS_METRIC ? "°C" : "°F");
  display->setFont(ArialMT_Plain_10);
  display->drawString(x + 20, y + 34, temp);
  display->setTextAlignment(TEXT_ALIGN_LEFT);
}
#endif

void drawHeaderOverlay(OLEDDisplay *display, OLEDDisplayUiState *state)
{
  char time_str[11];
  time_t now = dstAdjusted.time(nullptr);
  struct tm *timeinfo = localtime(&now);

  display->setFont(ArialMT_Plain_10);

#ifdef STYLE_24HR
  sprintf(time_str, "%02d:%02d:%02d\n", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
#else
  int hour = (timeinfo->tm_hour + 11) % 12 + 1; // take care of noon and midnight
  sprintf(time_str, "%2d:%02d:%02d%s\n", hour, timeinfo->tm_min, timeinfo->tm_sec, timeinfo->tm_hour >= 12 ? "pm" : "am");
#endif

  display->setTextAlignment(TEXT_ALIGN_LEFT);
  display->drawString(5, 52, time_str);

  display->setTextAlignment(TEXT_ALIGN_CENTER);
  String temp = String(FormattedTemperature) + (IS_METRIC ? "°C" : "°F");
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
#ifdef DEBUG
  Serial.println("Setting readyForUpdate to true");
#endif
  readyForWeatherUpdate = true;
}

void setReadyForDHTUpdate()
{
#ifdef DEBUG
  Serial.println("Setting readyForDHTUpdate to true");
#endif
  readyForDHTUpdate = true;
}

void setReadyForMQTTUpdate()
{
#ifdef DEBUG
  Serial.println("Setting readyForMQTTUpdate to true");
#endif
  readyForMQTTUpdate = true;
}

#ifdef PIR_PRESENCE_CONTROL
void setPresenceOff()
{
  presence = false;
  // presence not detected
  digitalWrite(LED_BUILTIN, HIGH);
  display.setContrast(10, 5, 0);
  tickerdisplayon.once_scheduled(10, setDisplayOff);
}

void setDisplayOff()
{
  display.displayOff(); // drastic off
   // Send an MQTT message to signal presence is off
   updateMQTTpresence(false);
}
#endif
