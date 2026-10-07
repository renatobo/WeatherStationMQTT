// SPDX-License-Identifier: MIT
// Derived from the ThingPulse weather station; see LICENSE for copyright and attribution.
// Project modifications and modernization by Renato Bonomini (renatobo).
//
// Profile-specific configuration storage and the shared DST clock.
// settings.h supplies declarations and compile-time switches; this file supplies
// one definition so all modules use the same topics, units and clock.

#include "settings.h"

// One owner for configuration storage and the DST clock.
const char* mqtt_server PROGMEM = MY_MQTT_SERVER;
const char* MQTT_OUT_TOPIC_TEMP PROGMEM = "sensors/" DEVICEID "/temp";
const char* MQTT_OUT_TOPIC_HUM PROGMEM = "sensors/" DEVICEID "/hum";
const char* MQTT_OUT_SENSOR_TEMP PROGMEM = DEVICEID "_temp";
const char* MQTT_OUT_SENSOR_HUM PROGMEM = DEVICEID "_hum";
const char* MQTT_OUT_TOPIC_PRESENCE PROGMEM = "sensors/" DEVICEID "/presence";

#ifdef LA
struct dstRule StartRule = {"PDT", Second, Sun, Mar, 2, 3600};
struct dstRule EndRule = {"PST", First, Sun, Nov, 1, 0};
const String WDAY_NAMES[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
const String MONTH_NAMES[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
#endif
simpleDSTadjust dstAdjusted(StartRule, EndRule);

#ifdef METRIC
const char* MQTT_OUT_UNIT_TEMP = "celsius";
#else
const char* MQTT_OUT_UNIT_TEMP = "fahrenheit";
#endif
const char* MQTT_OUT_UNIT_HUM = "relhum";
