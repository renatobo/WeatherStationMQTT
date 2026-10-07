#pragma once

// Project modifications by Renato Bonomini (renatobo); MIT, see LICENSE.
// MQTT services and read-only diagnostic counters. Sample servicing also
// runs during weather reads; presence publishing remains in the normal loop.
#include <Arduino.h>
#include "TelemetryState.h"

// Diagnostic state is written only by the owning module.
extern Telemetry::PublishStats publishStats;
extern uint32_t mqttConnectAttempts, mqttDisconnects, mqttMaxServiceGap;
extern uint32_t mqttSchedules, mqttSkippedSamples;
bool mqttConnected();
bool mqttSamplePending();
void beginMQTT();
void serviceMQTT();
void serviceMqttSamples();
void serviceMqttPresence();
void updateMQTTpresence(bool ispresent);
