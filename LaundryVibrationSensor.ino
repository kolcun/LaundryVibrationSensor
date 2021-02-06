

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <PubSubClient.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include "credentials.h"

#define HOSTNAME "LaundrySensor"
#define MQTT_CLIENT_NAME "kolcun/indoor/laundrysensor"

#define SensorPin D6
unsigned char vibrationState = 0;
unsigned long vibrationCounter = 0;
void ICACHE_RAM_ATTR vibrationDetected();
unsigned long previousMillis = 0;
const long interval = 1000 * 5;
const long vibrationThreshold = 500; // at least this many vibrations must happen before we call it
//todo, enough bumps, etc and this will probably kick off a notification - maybe reset the counter every 5 minuts if it's not changed?

const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWD;

const char* overwatchTopic = MQTT_CLIENT_NAME"/overwatch";

char charPayload[50];

WiFiClient wifiClient;
PubSubClient pubSubClient(wifiClient);

void setup() {
  Serial.begin(115200);
  Serial.println("Booting");
  setupOTA();
  setupMqtt();
  pinMode(SensorPin, INPUT);
  attachInterrupt(digitalPinToInterrupt(SensorPin), vibrationDetected, FALLING);
}

void loop() {
  ArduinoOTA.handle();
  if (!pubSubClient.connected()) {
    reconnect();
  }
  pubSubClient.loop();
  unsigned long currentMillis = millis();

  if (enoughTimeElapsedSinceSeeingAVibration(currentMillis) && vibrationCountOverThreshold()) {
        Serial.print("time passed since vibration, vibration counter: ");
        Serial.println(vibrationCounter);
        vibrationCounter=0;
        resetTimeSinceVibration(currentMillis);
        pubSubClient.publish(MQTT_CLIENT_NAME"/state", "complete");
  }
  
  if (seenAnyVibration()) {
    vibrationState = 0;
    resetTimeSinceVibration(currentMillis);
    Serial.print("vibration occured: ");
    Serial.println(vibrationCounter);
    pubSubClient.publish(MQTT_CLIENT_NAME"/vibration", String(vibrationCounter).c_str());
  }

}

bool seenAnyVibration(){
  return vibrationState != 0;
}

void resetTimeSinceVibration(long currentMillis){
  previousMillis = currentMillis; 
}

bool enoughTimeElapsedSinceSeeingAVibration(long currentMillis){
  return currentMillis - previousMillis >= interval;
}

bool vibrationCountOverThreshold() {
  return vibrationCounter >= vibrationThreshold;
}

//Interrupt function
void vibrationDetected() {
  vibrationState++;
  vibrationCounter++;
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  String newTopic = topic;
  Serial.print(topic);
  Serial.print("] ");
  payload[length] = '\0';
  String newPayload = String((char *)payload);
  int intPayload = newPayload.toInt();
  Serial.println(newPayload);
  Serial.println();
  newPayload.toCharArray(charPayload, newPayload.length() + 1);

}

void setupOTA() {
  WiFi.mode(WIFI_STA);
  WiFi.hostname(HOSTNAME);
  WiFi.begin(WIFI_SSID, WIFI_PASSWD);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Wifi Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }
  ArduinoOTA.setHostname(HOSTNAME);

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_FS
      type = "filesystem";
    }
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void setupMqtt() {
  pubSubClient.setServer(MQTT_SERVER, 1883);
  pubSubClient.setCallback(mqttCallback);
  if (!pubSubClient.connected()) {
    reconnect();
  }
}

void reconnect() {
  bool boot = true;
  int retries = 0;
  while (!pubSubClient.connected()) {
    if (retries < 10) {
      Serial.print("Attempting MQTT connection...");
      // Attempt to connect
      if (pubSubClient.connect(MQTT_CLIENT_NAME, MQTT_USER, MQTT_PASSWD)) {
        Serial.println("connected");
        // Once connected, publish an announcement...
        if (boot == true) {
          pubSubClient.publish(overwatchTopic, "Rebooted");
          boot = false;
        } else {
          pubSubClient.publish(overwatchTopic, "Reconnected");
        }
        //MQTT Subscriptions
      } else {
        Serial.print("failed, rc=");
        Serial.print(pubSubClient.state());
        Serial.println(" try again in 5 seconds");
        retries++;
        // Wait 5 seconds before retrying
        delay(5000);
      }
    }
    else {
      ESP.restart();
    }
  }
}
