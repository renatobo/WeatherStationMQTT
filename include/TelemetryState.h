#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace Telemetry {
inline bool elapsed(uint32_t now, uint32_t then, uint32_t interval) {
  return static_cast<uint32_t>(now - then) >= interval;
}

inline bool validReading(float celsius, float humidity) {
  return std::isfinite(celsius) && std::isfinite(humidity) &&
         celsius >= -40 && celsius <= 80 && humidity >= 0 && humidity <= 100;
}

inline float outputTemperature(float celsius, bool metric) {
  return metric ? celsius : celsius * 1.8f + 32;
}

inline bool timestamp(char* out, size_t size, const tm* time) {
  if (!size) return false;
  out[0] = '\0';
  if (!time || size < 20 || time->tm_year < 100 || time->tm_year > 8099 ||
      time->tm_mon < 0 || time->tm_mon > 11 || time->tm_mday < 1 || time->tm_mday > 31 ||
      time->tm_hour < 0 || time->tm_hour > 23 || time->tm_min < 0 || time->tm_min > 59 ||
      time->tm_sec < 0 || time->tm_sec > 60) return false;
  if (std::strftime(out, size, "%Y-%m-%d %H:%M:%S", time) == 19) return true;
  out[0] = '\0';
  return false;
}

inline bool clockText(char* out, size_t size, const tm* time, bool twentyFour, bool suffix) {
  if (!size) return false;
  out[0] = '\0';
  if (!time || time->tm_hour < 0 || time->tm_hour > 23 ||
      time->tm_min < 0 || time->tm_min > 59 || time->tm_sec < 0 || time->tm_sec > 60) return false;
  int hour = twentyFour ? time->tm_hour : (time->tm_hour + 11) % 12 + 1;
  int written = std::snprintf(out, size, "%02d:%02d:%02d%s", hour, time->tm_min,
      time->tm_sec, !twentyFour && suffix ? (time->tm_hour >= 12 ? "pm" : "am") : "");
  if (written >= 0 && static_cast<size_t>(written) < size) return true;
  out[0] = '\0';
  return false;
}

struct SensorCycle {
  uint32_t lastCycle = 0;
  uint32_t lastAttempt = 0;
  uint8_t remaining = 0;
  bool started = false;

  bool due(uint32_t now) {
    if (!remaining) {
      if (started && !elapsed(now, lastCycle, 60000)) return false;
      started = true;
      lastCycle = now;
      remaining = 3;
    }
    return elapsed(now, lastAttempt, 2500);
  }

  void finish(uint32_t now, bool success) {
    lastAttempt = now;
    remaining = success || !remaining ? 0 : remaining - 1;
  }
};

struct ReconnectPolicy {
  uint32_t lastAttempt = 0;
  uint32_t wait = 1000;
  bool attempted = false;

  bool due(uint32_t now, bool wifi, bool connected) const {
    return wifi && !connected && (!attempted || elapsed(now, lastAttempt, wait));
  }

  void finish(uint32_t now, bool success, uint32_t jitter) {
    lastAttempt = now;
    attempted = true;
    if (success) wait = 1000;
    else wait = (wait < 15000 ? wait * 2 : 30000) + (jitter % 251);
  }
};

struct PublishStats {
  uint32_t attempts = 0;
  uint32_t successes = 0;
  uint32_t failures = 0;
  uint32_t replaced = 0;
  uint32_t expired = 0;
  uint32_t formatErrors = 0;
};

struct PendingPair {
  char temperature[128] = {};
  char humidity[128] = {};
  uint32_t acquiredAt = 0;
  uint32_t lastAttempt = 0;
  bool attempted = false;
  bool active = false;
  bool temperatureSent = false;
  bool humiditySent = false;

  bool stage(const tm* time, uint32_t sampleAt, float celsius, float rh, bool metric,
      const char* temperatureSource, const char* humiditySource,
      const char* temperatureTopic, const char* humidityTopic, size_t packetSize,
      PublishStats& stats) {
    if (active) ++stats.replaced;
    active = false;
    char date[20];
    if (!validReading(celsius, rh) || !timestamp(date, sizeof(date), time)) return false;
    int t = std::snprintf(temperature, sizeof(temperature), "%s;%s;%s;%4.1f", date,
        temperatureSource, metric ? "celsius" : "fahrenheit", outputTemperature(celsius, metric));
    int h = std::snprintf(humidity, sizeof(humidity), "%s;%s;relhum;%4.1f", date, humiditySource, rh);
    if (t < 0 || h < 0 || static_cast<size_t>(t) >= sizeof(temperature) ||
        static_cast<size_t>(h) >= sizeof(humidity) ||
        7 + std::strlen(temperatureTopic) + static_cast<size_t>(t) > packetSize ||
        7 + std::strlen(humidityTopic) + static_cast<size_t>(h) > packetSize) {
      ++stats.formatErrors;
      return false;
    }
    acquiredAt = sampleAt;
    temperatureSent = humiditySent = attempted = false;
    active = true;
    return true;
  }

  template <class Client>
  void flush(Client& client, uint32_t now, const char* temperatureTopic,
      const char* humidityTopic, PublishStats& stats) {
    if (!active) return;
    if (elapsed(now, acquiredAt, 300000)) {
      active = false;
      ++stats.expired;
      return;
    }
    if (!client.connected() || (attempted && !elapsed(now, lastAttempt, 1000))) return;
    attempted = true;
    lastAttempt = now;
    if (!temperatureSent) {
      ++stats.attempts;
      temperatureSent = client.publish(temperatureTopic, temperature);
      temperatureSent ? ++stats.successes : ++stats.failures;
    }
    if (!humiditySent) {
      ++stats.attempts;
      humiditySent = client.publish(humidityTopic, humidity);
      humiditySent ? ++stats.successes : ++stats.failures;
    }
    active = !(temperatureSent && humiditySent);
  }
};
}  // namespace Telemetry
