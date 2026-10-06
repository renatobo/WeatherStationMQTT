#pragma once
#include <ArduinoJson.h>
#include <cmath>
#include <cstring>
#include <ctime>
#include <cctype>
#include <initializer_list>

namespace Weather {
struct Point {
  float temperature = 0;
  uint16_t code = 0;
  bool day = true;
  char time[17] = {};
};
struct Forecast {
  float high = 0, low = 0;
  uint16_t code = 0;
  uint8_t weekday = 0;
};
struct Snapshot {
  Point current;
  Forecast days[6];
  uint8_t count = 0;
};

inline const char* description(uint16_t code) {
  switch (code) {
    case 0: return "Clear sky";
    case 1: return "Mainly clear";
    case 2: return "Partly cloudy";
    case 3: return "Overcast";
    case 45: case 48: return "Fog";
    case 51: case 53: case 55: return "Drizzle";
    case 56: case 57: return "Freezing drizzle";
    case 61: case 63: case 65: return "Rain";
    case 66: case 67: return "Freezing rain";
    case 71: case 73: case 75: case 77: return "Snow";
    case 80: case 81: case 82: return "Rain showers";
    case 85: case 86: return "Snow showers";
    case 95: return "Thunderstorm";
    case 96: case 99: return "Thunderstorm / hail";
    default: return nullptr;
  }
}

// Existing Meteocons font glyphs; day/night code mapping follows the WMO groups.
inline char glyph(uint16_t code, bool day) {
  if (code == 0) return day ? 'B' : 'C';
  if (code == 1) return day ? 'H' : '4';
  if (code == 2) return day ? 'N' : '5';
  if (code == 3) return day ? 'Y' : '%';
  if (code == 45 || code == 48) return 'M';
  if (code >= 95) return day ? 'P' : '6';
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return day ? 'W' : '#';
  if (code >= 80 && code <= 82) return day ? 'R' : '8';
  return day ? 'Q' : '7';
}

inline bool date(const char* text, uint8_t& weekday) {
  if (!text || std::strlen(text) < 10 || text[4] != '-' || text[7] != '-') return false;
  for (int i = 0; i < 10; ++i)
    if (i != 4 && i != 7 && !std::isdigit(static_cast<unsigned char>(text[i]))) return false;
  int year = (text[0]-'0')*1000 + (text[1]-'0')*100 + (text[2]-'0')*10 + text[3]-'0';
  int month = (text[5]-'0')*10 + text[6]-'0', day = (text[8]-'0')*10 + text[9]-'0';
  if (year < 2000 || year > 2100 || month < 1 || month > 12 || day < 1 || day > 31) return false;
  tm calendar{};
  calendar.tm_year = year - 1900; calendar.tm_mon = month - 1; calendar.tm_mday = day;
  calendar.tm_hour = 12; calendar.tm_isdst = -1;
  if (std::mktime(&calendar) == static_cast<time_t>(-1) || calendar.tm_year != year - 1900 ||
      calendar.tm_mon != month - 1 || calendar.tm_mday != day) return false;
  weekday = calendar.tm_wday;
  return true;
}

struct Input {
  const char* data;
  size_t size, position = 0;
  Input(const char* body, size_t length): data(body), size(length) {}
  int read() { return position < size ? static_cast<unsigned char>(data[position++]) : -1; }
  size_t readBytes(char* output, size_t length) {
    size_t count = length < size - position ? length : size - position;
    std::memcpy(output, data + position, count); position += count; return count;
  }
};

inline bool temperatureValid(float temperature, bool metric) {
  float celsius = metric ? temperature : (temperature - 32) / 1.8f;
  return std::isfinite(celsius) && celsius >= -100 && celsius <= 80;
}

inline bool decode(const char* body, size_t length, bool metric, Snapshot& output, uint8_t expectedDays = 4, const char** reason = nullptr) {
  auto fail = [reason](const char* message) { if (reason) *reason = message; return false; };
  if (reason) *reason = "none";
  // Fixed allocation; shared only by the single cooperative weather request.
  static StaticJsonDocument<2048> document;
  static StaticJsonDocument<512> filter;
  filter.clear();
  filter["current"] = true; filter["current_units"] = true;
  filter["daily"] = true; filter["daily_units"] = true;
  Input input(body, length);
  DeserializationError error = deserializeJson(document, input, DeserializationOption::Filter(filter),
                      DeserializationOption::NestingLimit(6));
  if (error) return fail(error.c_str());
  while (input.position < length)
    if (!std::isspace(static_cast<unsigned char>(body[input.position++]))) return fail("trailing JSON data");
  const char* unit = metric ? "\xC2\xB0" "C" : "\xC2\xB0" "F";
  const char* currentUnit = document["current_units"]["temperature_2m"] | "";
  const char* highUnit = document["daily_units"]["temperature_2m_max"] | "";
  const char* lowUnit = document["daily_units"]["temperature_2m_min"] | "";
  if (std::strcmp(currentUnit, unit) || std::strcmp(highUnit, unit) || std::strcmp(lowUnit, unit)) return fail("temperature units");
  JsonVariantConst current = document["current"];
  if (!current["temperature_2m"].is<float>() || !current["weather_code"].is<uint16_t>() ||
      !current["is_day"].is<int>() || !current["time"].is<const char*>()) return fail("current field types");
  Snapshot candidate;
  candidate.current.temperature = current["temperature_2m"];
  candidate.current.code = current["weather_code"];
  int day = current["is_day"];
  const char* observed = current["time"];
  uint8_t weekday;
  if (!temperatureValid(candidate.current.temperature, metric) || !description(candidate.current.code) ||
      (day != 0 && day != 1) || std::strlen(observed) != 16 || observed[10] != 'T' || observed[13] != ':' ||
      !date(observed, weekday)) return fail("current value/date");
  for (int i : {11, 12, 14, 15})
    if (!std::isdigit(static_cast<unsigned char>(observed[i]))) return fail("current clock digits");
  if ((observed[11]-'0')*10 + observed[12]-'0' > 23 ||
      (observed[14]-'0')*10 + observed[15]-'0' > 59) return fail("current clock range");
  candidate.current.day = day;
  std::memcpy(candidate.current.time, observed, 17);
  JsonArrayConst dates = document["daily"]["time"].as<JsonArrayConst>();
  JsonArrayConst codes = document["daily"]["weather_code"].as<JsonArrayConst>();
  JsonArrayConst highs = document["daily"]["temperature_2m_max"].as<JsonArrayConst>();
  JsonArrayConst lows = document["daily"]["temperature_2m_min"].as<JsonArrayConst>();
  if (expectedDays < 1 || expectedDays > 6 || dates.size() != expectedDays || dates.size() != codes.size() ||
      dates.size() != highs.size() || dates.size() != lows.size()) return fail("forecast array sizes");
  for (size_t i = 0; i < dates.size(); ++i) {
    if (!dates[i].is<const char*>() || std::strlen(dates[i].as<const char*>()) != 10 ||
        !date(dates[i], candidate.days[i].weekday) || !codes[i].is<uint16_t>() ||
        !highs[i].is<float>() || !lows[i].is<float>()) return fail("forecast field/date");
    candidate.days[i].code = codes[i]; candidate.days[i].high = highs[i]; candidate.days[i].low = lows[i];
    if (!description(candidate.days[i].code) || !temperatureValid(candidate.days[i].high, metric) ||
        !temperatureValid(candidate.days[i].low, metric) || candidate.days[i].low > candidate.days[i].high) return fail("forecast value range");
    if (i && std::strcmp(dates[i-1].as<const char*>(), dates[i].as<const char*>()) >= 0) return fail("forecast date order");
  }
  candidate.count = dates.size();
  output = candidate; // commit only after the complete response passes validation
  return true;
}
}  // namespace Weather
