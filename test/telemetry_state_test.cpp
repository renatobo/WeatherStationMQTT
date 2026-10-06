#include "TelemetryState.h"
#include <cassert>
#include <climits>
#include <limits>
#include <string>
#include <vector>
#include <iostream>

struct FakeClient {
  bool online = true;
  std::vector<bool> results;
  std::vector<std::string> sent;
  bool connected() const { return online; }
  bool publish(const char* topic, const char* payload) {
    sent.push_back(std::string(topic) + " " + payload);
    if (results.empty()) return online;
    bool result = results.front();
    results.erase(results.begin());
    return result;
  }
};

int main() {
  tm time{};
  time.tm_year = 126; time.tm_mon = 9; time.tm_mday = 6;
  time.tm_hour = 23; time.tm_min = 59; time.tm_sec = 59;
  char date[20];
  assert(Telemetry::timestamp(date, sizeof(date), &time));
  assert(std::string(date) == "2026-10-06 23:59:59");
  char shortDate[18];
  assert(!Telemetry::timestamp(shortDate, sizeof(shortDate), &time));
  assert(shortDate[0] == '\0');
  time.tm_year = 8099;
  assert(Telemetry::timestamp(date, sizeof(date), &time));
  time.tm_year = 8100;
  assert(!Telemetry::timestamp(date, sizeof(date), &time));
  time.tm_year = 126;
  char clock[12];
  assert(Telemetry::clockText(clock, sizeof(clock), &time, false, true));
  assert(std::string(clock) == "11:59:59pm");
  assert(Telemetry::clockText(clock, sizeof(clock), &time, true, true));
  assert(std::string(clock) == "23:59:59");
  time.tm_hour = 0;
  assert(Telemetry::clockText(clock, sizeof(clock), &time, false, true));
  assert(std::string(clock) == "12:59:59am");
  time.tm_hour = 12;
  assert(Telemetry::clockText(clock, sizeof(clock), &time, false, true));
  assert(std::string(clock) == "12:59:59pm");
  time.tm_hour = INT_MAX;
  assert(!Telemetry::clockText(clock, sizeof(clock), &time, false, true));
  time.tm_hour = 23;
  assert(Telemetry::validReading(-40, 0));
  assert(Telemetry::validReading(80, 100));
  assert(!Telemetry::validReading(80.1f, 50));
  assert(!Telemetry::validReading(20, 100.1f));
  assert(!Telemetry::validReading(std::numeric_limits<float>::quiet_NaN(), 50));
  assert(!Telemetry::validReading(20, std::numeric_limits<float>::infinity()));
  assert(Telemetry::outputTemperature(0, true) == 0);
  assert(Telemetry::outputTemperature(0, false) == 32);

  Telemetry::SensorCycle sensor;
  assert(!sensor.due(0));
  assert(!sensor.due(2499));
  assert(sensor.due(2500)); sensor.finish(2500, false);
  assert(!sensor.due(4999));
  assert(sensor.due(5000)); sensor.finish(5000, false);
  assert(sensor.due(7500)); sensor.finish(7500, false);
  assert(!sensor.due(10000));
  assert(sensor.due(60000)); sensor.finish(60000, true);
  assert(!sensor.due(60001)); // a successful cycle cannot turn into continuous reads
  sensor.lastCycle = sensor.lastAttempt = UINT32_MAX - 1000;
  sensor.started = true; sensor.remaining = 1;
  assert(!sensor.due(1498));
  assert(sensor.due(1500)); sensor.finish(1500, true);

  Telemetry::ReconnectPolicy retry;
  assert(!retry.due(0, false, false));
  assert(!retry.due(0, true, true));
  assert(retry.due(0, true, false)); retry.finish(0, false, 0);
  assert(!retry.due(1999, true, false));
  assert(retry.due(2000, true, false)); retry.finish(2000, true, 0);
  retry.lastAttempt = UINT32_MAX - 1000;
  assert(retry.due(1, true, false));

  Telemetry::PublishStats stats;
  Telemetry::PendingPair pending;
  FakeClient client;
  assert(pending.stage(&time, 0, 0, 50, false, "room_temp", "room_hum", "temp", "hum", 256, stats));
  assert(std::string(pending.temperature) == "2026-10-06 23:59:59;room_temp;fahrenheit;32.0");
  client.results = {true, false, true};
  pending.flush(client, 0, "temp", "hum", stats);
  assert(pending.active && pending.temperatureSent && !pending.humiditySent);
  pending.flush(client, 999, "temp", "hum", stats);
  assert(client.sent.size() == 2);
  pending.flush(client, 1000, "temp", "hum", stats);
  assert(!pending.active && client.sent.size() == 3);
  assert(client.sent[2].find("hum ") == 0); // never repeat the successful temperature half
  assert(stats.attempts == 3 && stats.successes == 2 && stats.failures == 1);
  Telemetry::PublishStats boundaryStats;
  std::string maximumName(91, 'x');
  assert(pending.stage(&time, 0, 0, 50, false, maximumName.c_str(), maximumName.c_str(), "temp", "hum", 256, boundaryStats));
  maximumName += 'x';
  assert(!pending.stage(&time, 0, 0, 50, false, maximumName.c_str(), "hum", "temp", "hum", 256, boundaryStats));
  std::string longName(200, 'x');
  assert(!pending.stage(&time, 0, 0, 50, true, longName.c_str(), "hum", "temp", "hum", 256, stats));
  assert(!pending.active && stats.formatErrors == 1);
  assert(!pending.stage(&time, 0, 0, 50, true, "temp", "hum", "temp", "hum", 20, stats));
  assert(stats.formatErrors == 2);
  assert(pending.stage(&time, UINT32_MAX - 100, 0, 50, true, "temp", "hum", "temp", "hum", 256, stats));
  assert(std::string(pending.temperature).find(";celsius; 0.0") != std::string::npos);
  pending.flush(client, 10, "temp", "hum", stats);
  assert(!pending.active); // sample TTL handles uptime rollover

  // Simulate 30 minutes without a broker, followed by 10 minutes without Wi-Fi.
  // The exact firmware policies run while other services get every simulated tick.
  retry = {}; sensor = {}; pending = {}; stats = {}; client = {};
  unsigned connects = 0, reads = 0, httpTicks = 0, otaTicks = 0;
  for (uint32_t now = 0; now <= 2400000; now += 10) {
    bool wifi = !(now >= 1800000 && now < 2400000);
    client.online = now >= 2400000;
    if (retry.due(now, wifi, client.online)) {
      ++connects;
      retry.finish(now, false, 250);
    }
    if (sensor.due(now)) { ++reads; sensor.finish(now, true); }
    if (now % 300000 == 0)
      assert(pending.stage(&time, now, 20, 50, false, "temp", "hum", "temp", "hum", 256, stats));
    pending.flush(client, now, "temp", "hum", stats);
    ++httpTicks; ++otaTicks;
  }
  assert(connects > 50 && connects < 100); // bounded backoff, no busy retry loop
  assert(reads == 41 && httpTicks == 240001 && otaTicks == 240001);
  assert(client.sent.size() == 2 && !pending.active); // only the latest pair survives
  assert(stats.replaced == 8 && stats.attempts == 2 && stats.failures == 0);
  assert(pending.stage(&time, 0, 20, 50, false, "temp", "hum", "temp", "hum", 256, stats));
  client.online = false;
  pending.flush(client, 300000, "temp", "hum", stats);
  assert(!pending.active && stats.expired == 1);
  std::cout << "Telemetry policy, buffer, unit, rollover and outage checks passed\n";
}
