#include "WeatherDeadline.h"
#include "WeatherJson.h"
#include <cassert>
#include <climits>
#include <iostream>
#include <string>
#include <fstream>
#include <sstream>

int main(int argc, char** argv) {
  assert(argc == 2);
  std::ifstream input(argv[1]);
  std::stringstream buffer; buffer << input.rdbuf();
  std::string good = buffer.str();
  Weather::Snapshot weather;
  assert(Weather::decode(good.data(), good.size(), false, weather));
  assert(weather.count == 4 && Weather::description(weather.current.code));
  Weather::Snapshot previous = weather;
  for (const std::string& bad : {std::string("{"), std::string("{bad}"),
       good.substr(0, good.size()/2), good + "}", good + "{}", good + "garbage"}) {
    assert(!Weather::decode(bad.data(), bad.size(), false, weather));
    assert(weather.current.temperature == previous.current.temperature);
    assert(std::string(weather.current.time) == previous.current.time);
  }
  assert(!Weather::decode(good.data(), good.size(), true, weather)); // wrong unit policy
  StaticJsonDocument<4096> altered;
  assert(!deserializeJson(altered, good));
  altered["current_units"]["temperature_2m"] = "°C";
  altered["daily_units"]["temperature_2m_max"] = "°C";
  altered["daily_units"]["temperature_2m_min"] = "°C";
  altered["current"]["temperature_2m"] = 25;
  for (size_t i = 0; i < 4; ++i) {
    altered["daily"]["temperature_2m_max"][i] = 30;
    altered["daily"]["temperature_2m_min"][i] = 10;
  }
  std::string metric; serializeJson(altered, metric);
  assert(Weather::decode(metric.data(), metric.size(), true, weather));
  assert(weather.current.temperature == 25);
  for (const std::string& invalid : {std::string("999"), std::string("NaN"), std::string("1e99")}) {
    altered["current"]["temperature_2m"] = invalid;
    std::string text; serializeJson(altered, text);
    assert(!Weather::decode(text.data(), text.size(), true, weather));
    assert(weather.current.temperature == 25);
  }
  assert(Weather::decode(good.data(), good.size(), false, weather));
  std::string missing = good;
  size_t key = missing.find("temperature_2m\"");
  assert(key != std::string::npos); missing.replace(key, 14, "invalid_tempXX");
  assert(!Weather::decode(missing.data(), missing.size(), false, weather));
  uint8_t weekday;
  assert(Weather::date("2024-02-29", weekday) && weekday == 4);
  assert(!Weather::date("2025-02-29", weekday));
  assert(!Weather::date("2026-13-01", weekday));
  assert(!Weather::date("2026-01-32", weekday));
  assert(Weather::glyph(0, true) == 'B' && Weather::glyph(0, false) == 'C');
  assert(Weather::glyph(95, true) == 'P' && Weather::glyph(75, false) == '#');
  assert(!Weather::description(999));
  assert(!Weather::temperatureValid(std::numeric_limits<float>::infinity(), true));

  Weather::Deadline silent(0);
  assert(silent.received(1000, 0));
  assert(silent.lastData == 0); // empty reads must not reset idle timeout
  unsigned telemetryTicks = 0;
  for (uint32_t now = 0; silent.check(now); now += 10) ++telemetryTicks;
  assert(silent.error == Weather::Error::IdleTimeout && telemetryTicks == 150);
  Weather::Deadline dribble(0);
  for (uint32_t now = 0; now < 8000; now += 100) {
    assert(dribble.received(now, 1));
    ++telemetryTicks;
  }
  assert(!dribble.check(8000) && dribble.error == Weather::Error::TotalTimeout);
  Weather::Deadline oversized(0);
  assert(oversized.received(1, 8192));
  assert(!oversized.received(2, 1) && oversized.error == Weather::Error::TooLarge);
  Weather::Deadline rollover(UINT32_MAX - 1000);
  assert(rollover.received(1, 1));
  assert(rollover.check(1000));
  assert(!rollover.check(1501));
  char storage[4];
  Weather::Buffer body(storage, sizeof(storage));
  assert(body.append(reinterpret_cast<const uint8_t*>("abc"), 3));
  assert(!body.append(reinterpret_cast<const uint8_t*>("d"), 1));
  assert(std::string(storage) == "abc" && body.used == 3);
  std::cout << "Weather schema, atomic cache, units, codes, deadlines and rollover checks passed\n";
}
