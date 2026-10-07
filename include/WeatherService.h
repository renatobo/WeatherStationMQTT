#pragma once

// Project modifications by Renato Bonomini (renatobo); MIT, see LICENSE.
// Weather cache and read-only request diagnostics. serviceWeather() may
// run one bounded request and uses its callback to service the rest of the app.
#include <Arduino.h>
#include "WeatherJson.h"
#include "WeatherDeadline.h"

// Diagnostic state is written only by the owning module.
extern Weather::Snapshot currentWeather;
extern bool weatherHasSample, weatherInProgress, weatherAttempted;
extern Weather::Error weatherError;
extern uint32_t weatherAttempts, weatherSuccesses, weatherFailures;
extern uint32_t weatherLastAttemptAt, weatherLastSuccessAt, weatherLastDuration, weatherMaxDuration;
extern int weatherHttpStatus, weatherTlsError;
extern const char* weatherStage;
extern const char* weatherValidation;
void beginWeather();
void serviceWeather(void (*serviceApplication)());
bool weatherFresh();
