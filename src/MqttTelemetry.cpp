/**The MIT License (MIT)

Copyright (c) 2018 by Daniel Eichhorn - ThingPulse

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

See more at https://thingpulse.com
*/

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

void serviceMqttSamples() {
  if (Telemetry::elapsed(millis(), lastMqttScheduleAt, UPDATE_MQTT_INTERVAL_SECS * 1000UL)) {
    lastMqttScheduleAt = millis();
    updateMQTT();
  }
  pendingSample.flush(client, millis(), MQTT_OUT_TOPIC_TEMP, MQTT_OUT_TOPIC_HUM, publishStats);
}

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
