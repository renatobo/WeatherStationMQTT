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

// Sign up here to get an API key:
// https://docs.thingpulse.com/how-tos/openweathermap-key/
#ifndef MYOPEN_WEATHER_MAP_APP_ID
#error "Sign up here to get an API key: https://docs.thingpulse.com/how-tos/openweathermap-key/ and define macro MYOPEN_WEATHER_MAP_APP_ID"
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
const char* mqtt_server PROGMEM = MY_MQTT_SERVER;
const char* MQTT_OUT_TOPIC_TEMP PROGMEM = "sensors/" DEVICEID "/temp";
const char* MQTT_OUT_TOPIC_HUM PROGMEM = "sensors/" DEVICEID "/hum";
const char* MQTT_OUT_SENSOR_TEMP PROGMEM = DEVICEID "_temp";
const char* MQTT_OUT_SENSOR_HUM PROGMEM = DEVICEID "_hum";
const char* MQTT_OUT_TOPIC_PRESENCE PROGMEM = "sensors/" DEVICEID "/presence";

// Setup
const int UPDATE_INTERVAL_SECS = 10 * 60; // Update every 10 minutes
const int UPDATE_MQTT_INTERVAL_SECS = 5 * 60; // Update every 5 minutes

// DHT Settings
// suggested read: https://github.com/RobTillaart/DHTNew
#define DHT22_MAX_READINGS 3 // some sensors give faulty readings first time
#define DHTTYPE DHT22   // DHT 22  (AM2302), AM2321
#define MIN_ALLOWED_TEMP_C -100 // any reading of temp lower than this temp in C is invalid
#define MIN_ALLOWED_HUM -1 // any reading of humidity lower than this value is invalid
#define DELAY_DHT22_READS 2500 // wait 2.5 seconds between readings to avoid swampinp DHt22

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
struct dstRule StartRule = {"PDT", Second, Sun, Mar, 2, 3600}; // Eastern Daylight time = UTC/GMT -4 hours
struct dstRule EndRule = {"PST", First, Sun, Nov, 1, 0};       // Eastern Standard time = UTC/GMT -5 hour

// Uncomment for 24 Hour style clock
//#define STYLE_24HR

#define NTP_SERVERS "us.pool.ntp.org", "time.nist.gov", "pool.ntp.org"

// OpenWeatherMap Settings
String OPEN_WEATHER_MAP_APP_ID = MYOPEN_WEATHER_MAP_APP_ID;
/*
Go to https://openweathermap.org/find?q= and search for a location. Go through the
result set and select the entry closest to the actual location you want to display 
data for. It'll be a URL like https://openweathermap.org/city/2657896. The number
at the end is what you assign to the constant below.
 */
String OPEN_WEATHER_MAP_LOCATION_ID = MYOPEN_WEATHER_MAP_LOCATION;

// Pick a language code from this list:
// Arabic - ar, Bulgarian - bg, Catalan - ca, Czech - cz, German - de, Greek - el,
// English - en, Persian (Farsi) - fa, Finnish - fi, French - fr, Galician - gl,
// Croatian - hr, Hungarian - hu, Italian - it, Japanese - ja, Korean - kr,
// Latvian - la, Lithuanian - lt, Macedonian - mk, Dutch - nl, Polish - pl,
// Portuguese - pt, Romanian - ro, Russian - ru, Swedish - se, Slovak - sk,
// Slovenian - sl, Spanish - es, Turkish - tr, Ukrainian - ua, Vietnamese - vi,
// Chinese Simplified - zh_cn, Chinese Traditional - zh_tw.
String OPEN_WEATHER_MAP_LANGUAGE = "en";
const uint8_t MAX_FORECASTS = 4;

const boolean IS_METRIC = false;

// Adjust according to your language
const String WDAY_NAMES[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
const String MONTH_NAMES[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
#endif

// Setup simpleDSTadjust Library rules
simpleDSTadjust dstAdjusted(StartRule, EndRule);

#ifdef METRIC
const char* MQTT_OUT_UNIT_TEMP = "celsius";
#else
const char* MQTT_OUT_UNIT_TEMP = "fahrenheit";
#endif

const char* MQTT_OUT_UNIT_HUM = "relhum";


/***************************
 * End Settings
 **************************/
 
