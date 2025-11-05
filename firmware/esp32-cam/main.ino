#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>

#define CAMERA_MODEL_AI_THINKER
#include "camera_pins.h"

// WiFi credentials
const char* ssid     = "";
const char* password = "";

// Flask server URL
const char* flaskServerUrl = "http://192.168.254.108:5000/capture";

// Web server on port 80
WebServer server(80);

void handleCapture() {
  Serial.println("[INFO] Received HTTP request on /capture");

  // Capture an image
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
      Serial.println("[ERROR] Camera capture failed");
      server.send(500, "text/plain", "Camera capture failed");
      return;
  }
  Serial.printf("[INFO] Captured image size: %d bytes\n", fb->len);

  // Send POST request to Flask server
  HTTPClient http;
  http.begin(flaskServerUrl);
  http.addHeader("Content-Type", "application/octet-stream");

  Serial.println("[INFO] Sending image to Flask server...");
  int httpResponseCode = http.POST(fb->buf, fb->len);

  if (httpResponseCode > 0) {
      String response = http.getString();
      Serial.printf("[SUCCESS] Flask server response: %s\n", response.c_str());
      server.send(200, "application/json", response);
  } else {
      Serial.printf("[ERROR] HTTP POST failed: %s\n", http.errorToString(httpResponseCode).c_str());
      server.send(500, "text/plain", "Error sending image to Flask");
  }

  http.end();
  esp_camera_fb_return(fb);  // Release buffer only after HTTP request completes
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n[INFO] Starting ESP32-CAM...");

  // Initialize Camera
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("[ERROR] Camera initialization failed!");
    return;
  }
  Serial.println("[INFO] Camera initialized");

  // Connect to WiFi
  WiFi.begin(ssid, password);
  Serial.print("[INFO] Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\n[INFO] WiFi connected!");
  Serial.print("[INFO] ESP32-CAM IP Address: ");
  Serial.println(WiFi.localIP());

  // Web Server
  server.on("/capture", HTTP_GET, handleCapture);
  server.begin();
  Serial.println("[INFO] Web server started!");
}

void loop() {
  server.handleClient();
}


