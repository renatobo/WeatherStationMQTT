// SPDX-License-Identifier: MIT
// Derived from the ThingPulse weather station; see LICENSE for copyright and attribution.
// Project modifications and modernization by Renato Bonomini (renatobo).
//
// Scheduled, verified Open-Meteo requests and atomic weather caching.
// Owns the request deadline and cache. During header/body reads it invokes
// the application callback to keep MQTT, sensors, HTTP, OTA and frames serviced.

#include "WeatherService.h"
#include "settings.h"
#include "version.h"
#include "SystemHealth.h"
#include "MqttTelemetry.h"
#include "WeatherTransport.h"
#include "WeatherTrust.h"
#include "ESP8266HTTPClient.h"
#include "Ticker.h"

Weather::Snapshot currentWeather;
bool weatherHasSample = false, weatherInProgress = false, weatherAttempted = false;
Weather::Error weatherError = Weather::Error::None;
static Weather::Deadline* activeWeatherDeadline = nullptr;
uint32_t weatherAttempts = 0, weatherSuccesses = 0, weatherFailures = 0;
uint32_t weatherLastAttemptAt = 0, weatherLastSuccessAt = 0, weatherLastDuration = 0;
uint32_t weatherMaxDuration = 0;
int weatherHttpStatus = 0;
int weatherTlsError = 0;
const char* weatherStage = "not started";
const char* weatherValidation = "none";
static char weatherBody[2049];
static volatile bool readyForWeatherUpdate = true;
static Ticker tickerweather;
static void (*serviceApplicationDuringWeather)() = nullptr;

bool weatherFresh() {
  return weatherHasSample && weatherError == Weather::Error::None &&
      !Telemetry::elapsed(millis(), weatherLastSuccessAt, 1200000);
}

static void serviceDuringWeather() {
  sampleSystemHealth();
  if (heapFree < 6144 && activeWeatherDeadline) {
    activeWeatherDeadline->error = Weather::Error::LowMemory;
    return;
  }
  serviceApplicationDuringWeather();
}

static void updateWeather()
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
  // Certificate-date validation needs a synchronized clock. Retry later instead
  // of disabling TLS verification while NTP is still starting.
  if (time(nullptr) < 1577836800) {
    weatherError = Weather::Error::Clock;
    return;
  }
  sampleSystemHealth();
  // TLS needs both total free memory and a sufficiently large contiguous block.
  // Skip this attempt rather than risking allocation failure and a reset.
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
  // Decode into a candidate: failed/truncated requests must not partially
  // replace the last good forecast used by the display and HTTP diagnostics.
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
  // Recheck the total eight-second budget before committing. A blocking SDK
  // call may have completed after its preceding deadline check.
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

// Ticker callbacks only set a flag; network work stays in serviceWeather().
static void setReadyForWeatherUpdate()
{
  readyForWeatherUpdate = true;
}

void beginWeather() {
  tickerweather.attach(UPDATE_INTERVAL_SECS, setReadyForWeatherUpdate);
}

// Normal refreshes are ten minutes apart; failures retry after one minute.
// Give MQTT reconnection and queued samples priority over another TLS request.
void serviceWeather(void (*serviceApplication)()) {
  serviceApplicationDuringWeather = serviceApplication;
  bool weatherRetry = weatherAttempted && weatherError != Weather::Error::None &&
      Telemetry::elapsed(millis(), weatherLastAttemptAt, 60000);
  if (!weatherInProgress && (readyForWeatherUpdate || weatherRetry) && mqttConnected() &&
      !mqttSamplePending()) updateWeather();
}
