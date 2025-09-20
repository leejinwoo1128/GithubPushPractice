#include <Arduino.h>
#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <NimBLEDevice.h>

// ==================== 버튼 & 배터리 핀 ====================
#define BUTTON_PIN   15        // 버튼: GND ↔ GPIO15
#define BATTERY_PIN  4         // 배터리 전압 ADC 입력 (보드 회로에 맞게 조정)

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

// ==================== Wi-Fi 설정 ====================
#define AP_SSID       "XIAO_S3_CAM_AP"
#define AP_PASSWORD   "esp32s3cam123"
#define PHONE_IP      "192.168.4.2"   // 스마트폰이 AP 접속 시 받는 IP
#define PHONE_PORT    8080
#define PHONE_PATH    "/upload"

// ==================== BLE UUID ====================
#define SERVICE_UUID        "12345678-1234-1234-1234-1234567890ab"
#define CHAR_BUTTON_UUID    "12345678-1234-1234-1234-1234567890b1"
#define CHAR_BATTERY_UUID   "12345678-1234-1234-1234-1234567890b2"

// ==================== 전역 상태 ====================
volatile bool g_btnPressed = false;
volatile uint32_t g_lastMs = 0;

NimBLECharacteristic* chButton;
NimBLECharacteristic* chBattery;

// ===== 버튼 인터럽트 =====
void IRAM_ATTR onButtonISR() {
  uint32_t now = millis();
  if (now - g_lastMs > 150) {
    g_btnPressed = true;
    g_lastMs = now;
  }
}

// ===== 배터리 퍼센트 계산 =====
int readBatteryPercent() {
  int raw = analogRead(BATTERY_PIN);         // 0~4095
  float voltage = (raw / 4095.0) * 3.3 * 2;  // 보드에 분압회로 있다고 가정
  int percent = map(voltage * 100, 330, 420, 0, 100); // 3.3V=0%, 4.2V=100%
  return constrain(percent, 0, 100);
}

// ===== 카메라 초기화 =====
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
  c.frame_size   = FRAMESIZE_SVGA;    // 800x600
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

// ===== HTTP POST (JPEG 업로드) =====
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
    Serial.printf("[HTTP] POST code: %d\n", code);
  } else {
    Serial.printf("[HTTP] POST failed: %s\n", http.errorToString(code).c_str());
  }
  http.end();
  return (code == 200);
}

// ===== 카메라 촬영 & 전송 =====
void capture_and_send() {
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
    Serial.println("[ERR] Camera init failed");
  } else {
    Serial.println("[OK] Camera ready");
  }

  // Wi-Fi AP 모드
  if (WiFi.softAP(AP_SSID, AP_PASSWORD)) {
    Serial.print("[OK] AP SSID: "); Serial.println(AP_SSID);
    Serial.print("[OK] AP IP  : "); Serial.println(WiFi.softAPIP());
  } else {
    Serial.println("[ERR] Wi-Fi AP start failed");
  }

  // BLE 초기화
  NimBLEDevice::init("AI_DOCENT_GLASS");
  NimBLEServer* pServer = NimBLEDevice::createServer();
  NimBLEService* service = pServer->createService(SERVICE_UUID);

  chButton = service->createCharacteristic(CHAR_BUTTON_UUID, NIMBLE_PROPERTY::NOTIFY);
  chBattery = service->createCharacteristic(CHAR_BATTERY_UUID, NIMBLE_PROPERTY::NOTIFY);

  service->start();
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->setScanResponse(true);
  adv->start();

  Serial.println("[OK] BLE Advertising started");
}

// ==================== loop ====================
void loop() {
  // 버튼 눌림 → 사진 촬영 + 전송 + BLE Notify
  if (g_btnPressed) {
    g_btnPressed = false;
    Serial.println("Button Press → Capture & Notify");

    // 1. 사진 촬영 & HTTP POST 전송
    capture_and_send();

    // 2. BLE로 버튼 이벤트 전송
    uint8_t val = 1;
    chButton->setValue(&val, 1);
    chButton->notify();
  }

  // 배터리 상태 주기 전송 (5초마다)
  static uint32_t lastBat = 0;
  if (millis() - lastBat > 5000) {
    lastBat = millis();
    int bat = readBatteryPercent();
    Serial.printf("Battery %d%% → Notify\n", bat);
    chBattery->setValue(bat);
    chBattery->notify();
  }
}
