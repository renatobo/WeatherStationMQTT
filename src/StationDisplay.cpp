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

#include "StationDisplay.h"
#include "settings.h"
#include "IndoorSensor.h"
#include "WeatherService.h"
#include "DeviceNetwork.h"
#include "MqttTelemetry.h"
#include "SSD1306Wire.h"
#include "OLEDDisplayUi.h"
#include "Ticker.h"
#include "WiFiManager.h"
#include "WeatherStationFonts.h"
#include "WeatherStationImages.h"
#include "DSEG7Classic-BoldFont.h"

static SSD1306Wire display(0x3c, MYSDA_PIN, MYSDC_PIN);
static OLEDDisplayUi ui(&display);
static Ticker tickerdisplayon;
static bool presence = false;
static volatile bool presenceTimeoutDue = false, displayOffDue = false;

#if DHTTYPE == DHT22
#define DHTTEXT "DHT22"
#elif DHTTYPE == DHT21
#define DHTTEXT "DHT21"
#elif DHTTYPE == DHT11
#define DHTTEXT "DHT11"
#endif

//declaring prototypes
void configModeCallback(WiFiManager *myWiFiManager);
void drawOtaProgress(unsigned int, unsigned int);
static void drawDateTime(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
static void drawCurrentWeather(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
#if defined(forecast_enable) || defined(forecast_enable_long)
static void drawForecast(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
static void drawForecastDetails(OLEDDisplay *display, int x, int y, int dayIndex);
#endif
#ifdef forecast_enable_long
static void drawForecast2(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
#endif
static void drawIndoor(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
static void drawHeaderOverlay(OLEDDisplay *display, OLEDDisplayUiState *state);

// Add frames
// this array keeps function pointers to all frames
// frames are the single views that slide from right to left

#ifdef forecast_enable_long
// Version with 6 days of forecast
static FrameCallback frames[] = {drawDateTime, drawCurrentWeather, drawIndoor, drawForecast, drawForecast2};
static int numberOfFrames = 5;
#elif defined(forecast_enable)
// show only 3 days of forecast
static FrameCallback frames[] = {drawDateTime, drawIndoor, drawCurrentWeather, drawForecast};
static int numberOfFrames = 4;
#else
// show only 3 days of forecast
static FrameCallback frames[] = {drawDateTime, drawIndoor, drawCurrentWeather};
static int numberOfFrames = 3;
#endif

static OverlayCallback overlays[] = {drawHeaderOverlay};
static int numberOfOverlays = 1;

static void setPresenceOff();
static void setDisplayOff();

void beginDisplay() {
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
}

void beginDisplayFrames() {
  ui.setTargetFPS(30);
  ui.setTimePerFrame(5 * 1000); // Five seconds per frame

  //Hack until disableIndicator works:
  //Set an empty symbol
  ui.setActiveSymbol(emptySymbol);
  ui.setInactiveSymbol(emptySymbol);

  ui.disableIndicator();

  // You can change the transition that is used
  // SLIDE_LEFT, SLIDE_RIGHT, SLIDE_TOP, SLIDE_DOWN
  ui.setFrameAnimation(SLIDE_LEFT);

  ui.setFrames(frames, numberOfFrames);

  ui.setOverlays(overlays, numberOfOverlays);

#ifdef PIR_PRESENCE_CONTROL
  presenceTimeoutDue = true;
#endif
}

int updateDisplayFrames() { return ui.update(); }

void serviceDisplayPresence() {
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

static void drawDateTime(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
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

static void drawCurrentWeather(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
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

static void drawIndoor(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_10);
  display->drawString(64 + x, 0, DHTTEXT " Indoor Sensor");
  display->setFont(ArialMT_Plain_16);
  display->drawString(64 + x, 12, "Temp: " + (sampleFresh() ? String(FormattedTemperature) : String("n/a")) + (IS_METRIC ? "°C" : "°F"));
  display->drawString(64 + x, 30, "Humidity: " + (sampleFresh() ? String(FormattedHumidity) : String("n/a")) + "%");
}

#if defined(forecast_enable) || defined(forecast_enable_long)
static void drawForecast(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
  drawForecastDetails(display, x, y, 0);
  drawForecastDetails(display, x + 44, y, 1);
  drawForecastDetails(display, x + 88, y, 2);
}
static void drawForecastDetails(OLEDDisplay *display, int x, int y, int dayIndex)
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
static void drawForecast2(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
  drawForecastDetails(display, x, y, 3);
  drawForecastDetails(display, x + 44, y, 4);
  drawForecastDetails(display, x + 88, y, 5);
}
#endif

#endif

static void drawHeaderOverlay(OLEDDisplay *display, OLEDDisplayUiState *state)
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

#ifdef PIR_PRESENCE_CONTROL
static void setPresenceOff()
{
  presenceTimeoutDue = true;
}

static void setDisplayOff()
{
  displayOffDue = true;
}
#endif
