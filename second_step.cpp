#include <Arduino.h>
#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>

// ==================== 버튼 연결 ====================
// 버튼 한쪽 → GND, 다른쪽 → GPIO15 (내장 풀업 사용)
#define BUTTON_PIN 15

// ==================== 카메라 핀맵 (XIAO ESP32-S3 Sense 예시) ====================
#define PWDN_GPIO_NUM   -1
#define RESET_GPIO_NUM  -1
#define XCLK_GPIO_NUM   10
#define SIOD_GPIO_NUM   40
#define SIOC_GPIO_NUM   39
#define Y9_GPIO_NUM     48
#define Y8_GPIO_NUM     11
#define Y7_GPIO_NUM     12
#define Y6_GPIO_NUM     14
#define Y5_GPIO_NUM     16
#define Y4_GPIO_NUM     18
#define Y3_GPIO_NUM     17
#define Y2_GPIO_NUM     15
#define VSYNC_GPIO_NUM  38
#define HREF_GPIO_NUM   47
#define PCLK_GPIO_NUM   13

// ==================== Wi-Fi AP & 업로드 대상 ====================
#define AP_SSID       "XIAO_S3_CAM_AP"    // ESP32가 생성할 AP SSID
#define AP_PASSWORD   "esp32s3cam123"     // 8자 이상
#define PHONE_IP      "192.168.4.2"       // 스마트폰 IP (AP 접속 후 확인)
#define PHONE_PORT    8080
#define PHONE_PATH    "/upload"

// ==================== 전역 상태 ====================
volatile bool g_btn = false;
volatile uint32_t g_lastMs = 0;

// 버튼 인터럽트 (active-low: 눌리면 LOW)
void IRAM_ATTR onButtonISR() {
  uint32_t now = millis();
  if (now - g_lastMs > 150) {
    g_btn = true;
    g_lastMs = now;
  }
}

// ==================== 카메라 초기화 ====================
bool init_camera() {
  camera_config_t c{};
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer   = LEDC_TIMER_0;

  c.pin_d0 = Y2_GPIO_NUM; c.pin_d1 = Y3_GPIO_NUM; c.pin_d2 = Y4_GPIO_NUM; c.pin_d3 = Y5_GPIO_NUM;
  c.pin_d4 = Y6_GPIO_NUM; c.pin_d5 = Y7_GPIO_NUM; c.pin_d6 = Y8_GPIO_NUM; c.pin_d7 = Y9_GPIO_NUM;

  c.pin_xclk  = XCLK_GPIO_NUM;
  c.pin_pclk  = PCLK_GPIO_NUM;
  c.pin_vsync = VSYNC_GPIO_NUM;
  c.pin_href  = HREF_GPIO_NUM;

  c.pin_sccb_sda = SIOD_GPIO_NUM;
  c.pin_sccb_scl = SIOC_GPIO_NUM;

  c.pin_pwdn  = PWDN_GPIO_NUM;
  c.pin_reset = RESET_GPIO_NUM;

  c.xclk_freq_hz = 24000000;          
  c.pixel_format = PIXFORMAT_JPEG;    
  c.frame_size   = FRAMESIZE_SVGA;    
  c.jpeg_quality = 12;                
  c.fb_count     = 2;                 
  c.fb_location  = CAMERA_FB_IN_PSRAM;
  c.grab_mode    = CAMERA_GRAB_WHEN_EMPTY;

  esp_err_t err = esp_camera_init(&c);
  if (err != ESP_OK) return false;

  sensor_t* s = esp_camera_sensor_get();
  if (s) {
    Serial.printf("[INFO] Camera PID: 0x%02x\n", s->id.PID);
    s->set_gain_ctrl(s, 1);
    s->set_exposure_ctrl(s, 1);
    s->set_awb_gain(s, 1);
  }
  return true;
}

// ==================== JPEG POST 함수 ====================
bool post_jpeg_to_phone(const uint8_t* buf, size_t len) {
  HTTPClient http;
  String url = String("http://") + PHONE_IP + ":" + String(PHONE_PORT) + PHONE_PATH;

  http.setConnectTimeout(8000);
  http.setTimeout(15000);

  if (!http.begin(url)) {
    Serial.println("[ERR] HTTP begin failed");
    return false;
  }

  http.addHeader("Content-Type", "image/jpeg");
  int code = http.POST(buf, len);

  if (code > 0) {
    Serial.printf("[HTTP] POST... code: %d\n", code);
  } else {
    Serial.printf("[HTTP] POST failed, error: %s\n", http.errorToString(code).c_str());
  }
  http.end();
  return (code == 200);
}

// ==================== 한 장 촬영 & 전송 ====================
void capture_once() {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("[ERR] capture failed");
    return;
  }
  Serial.printf("[OK] JPEG %ux%u, %u bytes\n",
                fb->width, fb->height, (unsigned)fb->len);

  bool ok = post_jpeg_to_phone(fb->buf, fb->len);
  Serial.println(ok ? "[OK] Upload done" : "[ERR] Upload failed");

  esp_camera_fb_return(fb);
}

// ==================== setup ====================
void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), onButtonISR, FALLING);

  if (!init_camera()) {
    Serial.println("[ERR] Camera init failed. Check pin map & power.");
  } else {
    Serial.println("[OK] Camera ready.");
  }

  if (WiFi.softAP(AP_SSID, AP_PASSWORD)) {
    Serial.print("[OK] AP SSID: "); Serial.println(AP_SSID);
    Serial.print("[OK] AP IP  : "); Serial.println(WiFi.softAPIP());
    Serial.printf("Phone connect to \"%s\" then run server at http://%s:%d%s\n",
                  AP_SSID, PHONE_IP, PHONE_PORT, PHONE_PATH);
  } else {
    Serial.println("[ERR] softAP start failed");
  }
}

// ==================== loop ====================
void loop() {
  if (g_btn) {
    g_btn = false;
    Serial.println("Button → capture");
    capture_once();
  }
}
