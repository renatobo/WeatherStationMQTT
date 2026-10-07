#pragma once
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
