#define BLYNK_TEMPLATE_ID "TMPL6tSitw7kx"
#define BLYNK_TEMPLATE_NAME "Tanawa Template"
#define BLYNK_AUTH_TOKEN "3f9t3xoMaZgWy2MLqTUG0ulcXDcvXjyh"

#define BLYNK_PRINT Serial
#include <BlynkSimpleEsp32.h>
#include <HTTPClient.h>
#include <Arduino.h>
#include <WiFi.h>
#include "time.h"
#include <ESP32Servo.h>
#include <WebServer.h>

WebServer server(80);  // Create an HTTP server on port 80

char auth[] = BLYNK_AUTH_TOKEN;
BlynkTimer timer;

// Wi-Fi credentials
const char* ssid = "";  // Wifi and Password are hidden for data privacy purposes
const char* password = "";
const char* ntpServer = "time.google.com";  // Using Google NTP server

const long gmtOffset_sec = 3600 * 8;  // Philippines Time (UTC +8)
const int daylightOffset_sec = 0;    // No DST in the Philippines

// Pin definitions
const int trigPin = 5;
const int echoPin = 18;
const int ledPin = 19;
const int buzzerPin = 21;
const int servoPin = 23;

// Constants for sound speed and distance conversion
#define SOUND_SPEED 0.034
#define CM_TO_INCH 0.393701

long duration;
float distanceCm;
float distanceInch;
String receivedText;

// Global variables for task synchronization
QueueHandle_t alarmQueue;

Servo servo1;

String time_info;
int alarm_info;
bool is_servo_fixed_control;

// Global flag to ensure only one capture request is sent per detection event.
volatile bool captureTriggered = false;

#define SWITCH_VPIN V2  // Use V2 for the switch in the Blynk app
#define CAPTURE_VPIN V3

// Blynk function to handle the switch state
BLYNK_WRITE(SWITCH_VPIN) {
  int switchState = param.asInt();
  if (switchState == 1) {
    is_servo_fixed_control = true;
    servo1.write(0);
    Serial.println("Servo moved to 0°");
  } else {
    is_servo_fixed_control = false;
    servo1.write(90);
    Serial.println("Servo moved to 90°");
  }
}

BLYNK_WRITE(CAPTURE_VPIN) {
  int captureState = param.asInt();
  if (captureState == 1) {
    bool alarmSignal = true;
    xQueueSend(alarmQueue, &alarmSignal, portMAX_DELAY);
  }
}

void controlServoTask(void *pvParameters) {
  while (true) {
    if (receivedText == "LMM 8923") {  // Check if the plate number matches
      Serial.println("✅ Authorized Vehicle Detected! Opening Gate...");
      servo1.write(0);  // Open the gate
      vTaskDelay(pdMS_TO_TICKS(5000));  // Keep gate open for 5 seconds
      Serial.println("❌ Closing Gate...");
      servo1.write(90);  // Close the gate
      receivedText = "";  // Reset the received text to avoid re-triggering
    }
    vTaskDelay(pdMS_TO_TICKS(500));  // Check every 500ms
  }
}

void sendCaptureRequest() {
  HTTPClient http;
  Serial.println("Sending capture request to Flask server...");

  http.begin("http://192.168.254.112/capture");
  http.addHeader("Content-Type", "application/json");
  int httpResponseCode = http.GET();

  if (httpResponseCode > 0) {
    Serial.print("Capture Request Sent Successfully, Response code: ");
    Serial.println(httpResponseCode);
  } else {
    Serial.print("Capture Request Failed: ");
    Serial.println(http.errorToString(httpResponseCode).c_str());
  }

  http.end();
}

void sendSensor() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return;
  }
  char formattedTime[20];
  strftime(formattedTime, sizeof(formattedTime), "%Y%m%d_%H%M%S", &timeinfo);
  time_info = String(formattedTime);
  xQueueReceive(alarmQueue, &alarm_info, portMAX_DELAY);
  Blynk.virtualWrite(V0, time_info);
  Blynk.virtualWrite(V1, receivedText);
}

void sendplateNumber() {
  Blynk.virtualWrite(V1, receivedText);
}

void printLocalTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return;
  }
  char formattedTime[20];
  strftime(formattedTime, sizeof(formattedTime), "%Y%m%d_%H%M%S", &timeinfo);
  Serial.print(formattedTime);
}

// Task to measure distance using the ultrasonic sensor
void measureDistanceTask(void *pvParameters) {
  unsigned long objectDetectedTime = 0;
  while (true) {
    digitalWrite(trigPin, LOW);
    delayMicroseconds(2);
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);

    duration = pulseIn(echoPin, HIGH);
    if (duration > 0 && duration < 30000) {
      distanceCm = duration * SOUND_SPEED / 2;
      distanceInch = distanceCm * CM_TO_INCH;

      if (distanceInch < 6) {
        if (objectDetectedTime == 0) {
          objectDetectedTime = millis();
        } else if (millis() - objectDetectedTime >= 3000) {
          bool alarmSignal = true;
          xQueueSend(alarmQueue, &alarmSignal, portMAX_DELAY);
        }
      } else {
        objectDetectedTime = 0;
        bool alarmSignal = false;
        xQueueSend(alarmQueue, &alarmSignal, portMAX_DELAY);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void alarmTask(void *pvParameters) {
  bool lastAlarm = false;

  while (true) {
    bool alarm = false;
    if (xQueueReceive(alarmQueue, &alarm, portMAX_DELAY)) {
      if (alarm && !lastAlarm) {
        lastAlarm = true;
        Serial.print("Date/Time: ");
        printLocalTime();
        Serial.println(" - VEHICLE DETECTED!");

        receivedText = "";
        Serial.println("🔄 Reset receivedText for new detection.");

        for (int i = 0; i < 3; i++) {
          digitalWrite(ledPin, HIGH);
          digitalWrite(buzzerPin, HIGH);
          vTaskDelay(pdMS_TO_TICKS(100));
          digitalWrite(ledPin, LOW);
          digitalWrite(buzzerPin, LOW);
          vTaskDelay(pdMS_TO_TICKS(100));
        }

        if (!captureTriggered) {
          sendCaptureRequest();
          captureTriggered = true;
        }
      } else if (!alarm && lastAlarm) {
        lastAlarm = false;
        captureTriggered = false;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(ledPin, OUTPUT);
  pinMode(buzzerPin, OUTPUT);

  servo1.attach(servoPin);

  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected.");

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  struct tm timeinfo;
  int attempts = 0;
  while (!getLocalTime(&timeinfo) && attempts < 5) {
    Serial.println("Failed to obtain time, retrying...");
    delay(2000);
    attempts++;
  }

  if (attempts == 5) {
    Serial.println("Failed to obtain time.");
  } else {
    Serial.println("Time synchronized!");
    printLocalTime();
    Serial.println();
  }

  alarmQueue = xQueueCreate(5, sizeof(bool));

  Blynk.begin(auth, ssid, password);
  timer.setInterval(10000L, sendSensor);

  server.on("/receive_text", HTTP_GET, []() {
    if (server.hasArg("text")) {
      receivedText = server.arg("text");
      Serial.print("📌 Extracted Text Received: ");
      Serial.println(receivedText);
      sendSensor();
    }
    server.send(200, "text/plain", "Text received");
  });

  server.begin();
  Serial.println("🔹 HTTP Server Started on ESP32");

  xTaskCreate(measureDistanceTask, "Measure Distance", 4096, NULL, 2, NULL);
  xTaskCreate(alarmTask, "Alarm", 4096, NULL, 1, NULL);
  xTaskCreate(controlServoTask, "Control Servo", 4096, NULL, 1, NULL);
}

void loop() {
  Blynk.run();
  timer.run();
  server.handleClient();

  if (alarm_info == 1) {
    Blynk.logEvent("vehicle_detected", "Vehicle Detected! It wants to enter.");
  }
}


