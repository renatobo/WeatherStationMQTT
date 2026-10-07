// SPDX-License-Identifier: MIT
// Derived from the ThingPulse weather station; see LICENSE for copyright and attribution.
// Project modifications and modernization by Renato Bonomini (renatobo).
//
// MQTT connection maintenance and queued sensor/presence publishing.
// Owns the socket, reconnect policy and pending pair. Consumers inspect
// connection/queue status through functions rather than accessing the client.

#include "MqttTelemetry.h"
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include "settings.h"
#include "IndoorSensor.h"
#include "DeviceNetwork.h"

// MQTT initialize
static WiFiClient espClient;
static PubSubClient client(espClient);
static Telemetry::ReconnectPolicy mqttReconnect;
static Telemetry::PendingPair pendingSample;
Telemetry::PublishStats publishStats;
uint32_t mqttConnectAttempts = 0;
uint32_t mqttDisconnects = 0, mqttMaxServiceGap = 0;
static uint32_t mqttLastServiceAt = 0;
uint32_t mqttSchedules = 0, mqttSkippedSamples = 0;
static bool mqttServiced = false, mqttWasConnected = false;
static bool presencePending = false, desiredPresence = false;
static uint32_t lastPresenceAttempt = 0;
static bool presenceAttempted = false;

static uint32_t lastMqttScheduleAt = 0;

static void updateMQTT();
static void mqttcallback(char *topic, byte *payload, unsigned int length)
{
#ifdef DEBUG
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (unsigned int i = 0; i < length; i++)
  {
    Serial.print((char)payload[i]);
  }
  Serial.println();
#endif
}

void beginMQTT() {
  // setup mqtt
  client.setServer(mqtt_server, 1883);
  client.setCallback(mqttcallback);
  // RB Added 2022-12
  client.setKeepAlive(60);
  client.setSocketTimeout(1); // bounded MQTT packet/CONNACK wait, seconds
  espClient.setTimeout(250); // bounded DNS, TCP connect and write waits, milliseconds

  lastMqttScheduleAt = millis();
}

// Retain one latest scheduled sample; its timestamp is acquisition time.
static void updateMQTT()
{
  ++mqttSchedules;
  if (!sampleFresh()) {
    ++mqttSkippedSamples;
    return;
  }
  if (!pendingSample.stage(localtime(&lastValidSampleTime), lastValidSampleAt,
      lastValidCelsius, humidity, IS_METRIC, MQTT_OUT_SENSOR_TEMP, MQTT_OUT_SENSOR_HUM,
      MQTT_OUT_TOPIC_TEMP, MQTT_OUT_TOPIC_HUM, client.getBufferSize(), publishStats))
    ++mqttSkippedSamples;
}

// Presence events are queued; timer callbacks never touch sockets.
void updateMQTTpresence(bool ispresent)
{
  desiredPresence = ispresent;
  presencePending = true;
  presenceAttempted = false;
}

void serviceMQTT()
{
  uint32_t now = millis();
  if (mqttServiced) {
    uint32_t gap = now - mqttLastServiceAt;
    if (gap > mqttMaxServiceGap) mqttMaxServiceGap = gap;
  }
  mqttServiced = true;
  mqttLastServiceAt = now;
  bool wifi = WiFi.status() == WL_CONNECTED;
  if (!wifi) espClient.stop();
  // PubSubClient needs loop() even when no sample is due, to maintain keepalive.
  // Reconnect attempts below are spaced by the capped, jittered retry policy.
  if (client.connected()) client.loop();
  bool connected = client.connected();
  if (mqttWasConnected && !connected) ++mqttDisconnects;
  if (mqttReconnect.due(millis(), wifi, connected)) {
    ++mqttConnectAttempts;
    connected = client.connect(hostname.c_str());
    mqttReconnect.finish(millis(), connected, random(251));
  }
  mqttWasConnected = connected;
}

// Publish on the five-minute schedule; retry unsent halves independently.
// PendingPair expires old readings and retains only the latest scheduled pair.
// A successful publish counts the client send, not downstream database storage.
void serviceMqttSamples() {
  if (Telemetry::elapsed(millis(), lastMqttScheduleAt, UPDATE_MQTT_INTERVAL_SECS * 1000UL)) {
    lastMqttScheduleAt = millis();
    updateMQTT();
  }
  pendingSample.flush(client, millis(), MQTT_OUT_TOPIC_TEMP, MQTT_OUT_TOPIC_HUM, publishStats);
}

// Presence has its own one-second retry clock. Queue updates coalesce to the
// latest state, and timer callbacks never publish directly.
void serviceMqttPresence() {
  if (presencePending && client.connected() &&
      (!presenceAttempted || Telemetry::elapsed(millis(), lastPresenceAttempt, 1000))) {
    presenceAttempted = true;
    lastPresenceAttempt = millis();
    ++publishStats.attempts;
    if (client.publish(MQTT_OUT_TOPIC_PRESENCE, desiredPresence ? "TRUE" : "FALSE")) {
      ++publishStats.successes;
      presencePending = false;
    } else ++publishStats.failures;
  }
}

bool mqttConnected() { return client.connected(); }
bool mqttSamplePending() { return pendingSample.active; }
