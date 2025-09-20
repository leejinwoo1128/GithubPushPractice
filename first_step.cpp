#include <Arduino.h>
#include "esp_camera.h"

// ==================== 버튼 연결 ====================
// 버튼 한쪽 → GND, 다른쪽 → 이 GPIO (내장 풀업 사용)
#define BUTTON_PIN 15   // 원하면 다른 GPIO로 변경 가능

// ==================== 카메라 핀맵 ====================
// XIAO ESP32-S3 Sense의 24핀 FPC 커넥터 기준 (예시)
// 필요시 보드 문서대로 숫자만 바꾸면 됨.
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

// ==================== 전역 상태 ====================
volatile bool g_btn = false;
volatile uint32_t g_lastMs = 0;

// 버튼 인터럽트 (active-low: 눌리면 LOW)
void IRAM_ATTR onButtonISR() {
  uint32_t now = millis();
  if (now - g_lastMs > 150) { // 150ms 소프트 디바운스
    g_btn = true;
    g_lastMs = now;
  }
}

// 카메라 초기화
bool init_camera() {
  camera_config_t c{};
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer   = LEDC_TIMER_0;

  // D0~D7
  c.pin_d0 = Y2_GPIO_NUM;
  c.pin_d1 = Y3_GPIO_NUM;
  c.pin_d2 = Y4_GPIO_NUM;
  c.pin_d3 = Y5_GPIO_NUM;
  c.pin_d4 = Y6_GPIO_NUM;
  c.pin_d5 = Y7_GPIO_NUM;
  c.pin_d6 = Y8_GPIO_NUM;
  c.pin_d7 = Y9_GPIO_NUM;

  // sync/clock
  c.pin_xclk  = XCLK_GPIO_NUM;
  c.pin_pclk  = PCLK_GPIO_NUM;
  c.pin_vsync = VSYNC_GPIO_NUM;
  c.pin_href  = HREF_GPIO_NUM;

  // SCCB (I2C-like)
  c.pin_sccb_sda = SIOD_GPIO_NUM;
  c.pin_sccb_scl = SIOC_GPIO_NUM;

  // power/reset
  c.pin_pwdn  = PWDN_GPIO_NUM;
  c.pin_reset = RESET_GPIO_NUM;

  // OV3660 권장 스타트
  c.xclk_freq_hz = 24000000;           // 24MHz 권장
  c.pixel_format = PIXFORMAT_JPEG;     // 바로 JPEG로 받기
  c.frame_size   = FRAMESIZE_SVGA;     // 800x600부터 안정화
  c.jpeg_quality = 12;                 // 10(고화질)~15(저화질) 범위 권장
  c.fb_count     = 2;                  // 더블버퍼 → 캡처/전송 동시성↑
  c.fb_location  = CAMERA_FB_IN_PSRAM; // PSRAM 사용
  c.grab_mode    = CAMERA_GRAB_WHEN_EMPTY;

  esp_err_t err = esp_camera_init(&c);
  if (err != ESP_OK) return false;

  // 센서 PID에 따라 기본값 튜닝 (OV3660/OV2640 둘 다 대응)
  sensor_t* s = esp_camera_sensor_get();
  if (!s) return true;

  Serial.printf("[INFO] Detected camera PID: 0x%02x\n", s->id.PID);

  // 공통 자동 제어 기본 ON
  s->set_gain_ctrl(s, 1);       // AGC on
  s->set_exposure_ctrl(s, 1);   // AEC on
  s->set_awb_gain(s, 1);        // AWB on
  s->set_brightness(s, 0);      // -2~2
  s->set_contrast(s, 0);        // -2~2
  s->set_saturation(s, 0);      // -2~2

  // 필요 시 해상도 단계적으로 올리기 예시:
  // s->set_framesize(s, FRAMESIZE_XGA);   // 1024x768
  // s->set_framesize(s, FRAMESIZE_HD);    // 1280x720
  // s->set_framesize(s, FRAMESIZE_QXGA);  // 2048x1536 (OV3660 가능, 메모리/속도 주의)

  return true;
}

// 한 장 촬영
void capture_once() {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("[ERR] capture failed");
    return;
  }
  Serial.print("[OK] JPEG size = ");
  Serial.print(fb->len);
  Serial.print(" bytes, ");
  Serial.print(fb->width);
  Serial.print("x");
  Serial.println(fb->height);

  // TODO: 여기서 스마트폰 전송(Wi-Fi HTTP POST / BLE 분할전송),
  //       또는 SD/플래시에 저장 로직을 넣으면 됨.
  //       데이터: fb->buf (uint8_t*), 길이: fb->len

  esp_camera_fb_return(fb); // 중요: 버퍼 반납
}

void setup() {
  Serial.begin(115200);
  delay(300);

  // 버튼: 내부 풀업 → 평소 HIGH, 누르면 LOW
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), onButtonISR, FALLING);

  if (!init_camera()) {
    Serial.println("[ERR] Camera init failed. Check pin map & power.");
  } else {
    Serial.println("[OK] Camera ready.");
  }
}

void loop() {
  if (g_btn) {
    g_btn = false;
    Serial.println("Button → capture");
    capture_once();
  }
}
