#pragma once
#include <Arduino.h>
#include <simpleDSTadjust.h>

/**The MIT License (MIT)

Copyright (c) 2016 by Daniel Eichhorn

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

See more at http://blog.squix.ch
*/

// TODO: update for https://github.com/esp8266/Arduino/blob/master/libraries/esp8266/examples/NTP-TZ-DST/NTP-TZ-DST.ino 

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
 
// >>> Uncomment one of the following 2 lines to define which OLED display interface type you are using
// https://github.com/ThingPulse/esp8266-oled-ssd1306


// Please read http://blog.squix.org/weatherstation-getting-code-adapting-it
// for setup instructions
#include "mysecrets.h"

#if !defined(WEATHER_LATITUDE) || !defined(WEATHER_LONGITUDE) || !defined(WEATHER_TIMEZONE)
#error "Configure WEATHER_LATITUDE, WEATHER_LONGITUDE and WEATHER_TIMEZONE in mysecrets.h"
#endif

// enable web server to show temp and hum
#define INTERNAL_WEBSERVER

// enable pir presence
#define PIR_PRESENCE_CONTROL
#ifndef PIR_PIN
#define PIR_PIN D7
#endif

// display check: have we defined what display type?
#if !defined(i2cOLED) && !defined(brzo_i2c)
#error "you need to define either i2cOLED or brzo_i2c"
#endif

#ifndef DEVICEID
#define DEVICEID "esp8266"
#endif

#ifndef HOSTNAME
#define HOSTNAME "esp8266"
#endif

#ifndef MYSDA_PIN
#define MYSDA_PIN D2
#endif

#ifndef MYSDC_PIN
#define MYSDC_PIN D1
#endif

// As DHT PIN, if possible do not use D3, D4, D8
// D4 seems a very common choice in any case
// Reference: https://github.com/RobTillaart/DHTNew/issues/31#issuecomment-753596340
// After a search it turns out that GPIO0, GPIO2 and GPIO15 from the ESP8266 do something special during boot time which seems to confuse the DHT sensor.
#ifndef DHTPIN
#define DHTPIN D4
#endif

#if !defined(PIR_PRESENCE_CONTROL) && defined(PIR_PIN)
#error "You defined the PIN for PIR but PIR_PRESENCE_CONTROL is not defined"
#endif

#if defined(PIR_PRESENCE_CONTROL) && !defined(PIR_PIN)
#error "You defined PIR_PRESENCE_CONTROL but the PIN for PIR is not defined"
#endif


// MQTT settings for library PubSubClient
extern const char* mqtt_server;
extern const char* MQTT_OUT_TOPIC_TEMP;
extern const char* MQTT_OUT_TOPIC_HUM;
extern const char* MQTT_OUT_SENSOR_TEMP;
extern const char* MQTT_OUT_SENSOR_HUM;
extern const char* MQTT_OUT_TOPIC_PRESENCE;

// Setup
constexpr int UPDATE_INTERVAL_SECS = 10 * 60; // Update every 10 minutes
constexpr int UPDATE_MQTT_INTERVAL_SECS = 5 * 60; // Update every 5 minutes

// DHT Settings
// suggested read: https://github.com/RobTillaart/DHTNew
#define DHT11 11
#define DHT21 21
#define DHT22 22
#define DHTTYPE DHT22   // DHT 22  (AM2302), AM2321

// -----------------------------------
// Example Locales (uncomment only 1) or pass via command line define
// #define LA
// #define Zurich
// #define Boston
// #define Sydney
//------------------------------------

#ifdef LA
//DST rules for US Pacific Time Zone (Los Angeles)
#define UTC_OFFSET -8
extern struct dstRule StartRule;
extern struct dstRule EndRule;

// Uncomment for 24 Hour style clock
//#define STYLE_24HR

#define NTP_SERVERS "us.pool.ntp.org", "time.nist.gov", "pool.ntp.org"

// Open-Meteo is key-free; private coordinates preserve the configured weather city.
#ifdef forecast_enable_long
constexpr uint8_t MAX_FORECASTS = 6;
#else
constexpr uint8_t MAX_FORECASTS = 4;
#endif

#ifdef METRIC
constexpr boolean IS_METRIC = true;
#else
constexpr boolean IS_METRIC = false;
#endif

// Adjust according to your language
extern const String WDAY_NAMES[];
extern const String MONTH_NAMES[];
#endif

// Setup simpleDSTadjust Library rules
extern simpleDSTadjust dstAdjusted;

extern const char* MQTT_OUT_UNIT_TEMP;

extern const char* MQTT_OUT_UNIT_HUM;


/***************************
 * End Settings
 **************************/
 
