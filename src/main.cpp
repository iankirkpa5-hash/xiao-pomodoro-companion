#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <math.h>

// Round Display for XIAO uses XIAO D8/D10 for SPI and D1/D3 for LCD CS/DC.
// XIAO ESP32S3 mapping: D8=GPIO7, D10=GPIO9, D1=GPIO2, D3=GPIO4, D6=GPIO43.
constexpr int LCD_SCLK = 7;
constexpr int LCD_MOSI = 9;
constexpr int LCD_CS = 2;
constexpr int LCD_DC = 4;
constexpr int LCD_BL = 43;
constexpr int BACKLIGHT_CHANNEL = 0;
constexpr int BACKLIGHT_BRIGHTNESS = 150;

constexpr int TOUCH_SDA = 5;       // XIAO D4
constexpr int TOUCH_SCL = 6;       // XIAO D5
constexpr int TOUCH_INT = 44;      // XIAO D7
constexpr uint8_t TOUCH_ADDR = 0x2e;
constexpr uint32_t FOCUS_SECONDS = 25UL * 60UL;
constexpr uint32_t BREAK_SECONDS = 5UL * 60UL;
constexpr uint32_t LONG_PRESS_MS = 900;
constexpr uint32_t TOUCH_RELEASE_GAP_MS = 180;

enum class SessionMode {
  Focus,
  Break,
};

SessionMode mode = SessionMode::Focus;
bool running = false;
uint32_t remaining_seconds = FOCUS_SECONDS;
unsigned long last_countdown_ms = 0;
uint32_t last_rendered_seconds = UINT32_MAX;
int last_progress_angle = -90;

Arduino_DataBus *bus = new Arduino_ESP32SPI(
    LCD_DC,
    LCD_CS,
    LCD_SCLK,
    LCD_MOSI,
    GFX_NOT_DEFINED);

Arduino_GFX *display = new Arduino_GC9A01(
    bus,
    GFX_NOT_DEFINED,
    0,
    true);

uint16_t color565(uint8_t r, uint8_t g, uint8_t b) {
  return display->color565(r, g, b);
}

bool readTouch(uint8_t &x, uint8_t &y) {
  uint8_t data[5] = {0};
  const uint8_t read_len = Wire.requestFrom(TOUCH_ADDR, static_cast<uint8_t>(sizeof(data)));
  if (read_len != sizeof(data)) {
    while (Wire.available()) {
      Wire.read();
    }
    return false;
  }

  for (uint8_t i = 0; i < sizeof(data); i++) {
    data[i] = Wire.read();
  }

  if (data[0] != 0x01) {
    return false;
  }

  x = data[2];
  y = data[4];
  return true;
}

void scanI2C() {
  Serial.println("Scanning I2C...");
  for (uint8_t address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      Serial.printf("I2C device found: 0x%02X\n", address);
    }
  }
}

void setBacklight(uint8_t brightness) {
  ledcSetup(BACKLIGHT_CHANNEL, 5000, 8);
  ledcAttachPin(LCD_BL, BACKLIGHT_CHANNEL);
  ledcWrite(BACKLIGHT_CHANNEL, brightness);
}

const char *modeLabel() {
  return mode == SessionMode::Focus ? "Focus" : "Break";
}

uint32_t sessionSeconds() {
  return mode == SessionMode::Focus ? FOCUS_SECONDS : BREAK_SECONDS;
}

uint16_t progressColor() {
  return mode == SessionMode::Focus ? color565(38, 150, 178) : color565(74, 172, 104);
}

void formatTime(char *buffer, size_t buffer_size, uint32_t seconds) {
  snprintf(buffer, buffer_size, "%02lu:%02lu", seconds / 60, seconds % 60);
}

void drawProgressArcSegment(int from_angle, int to_angle, uint16_t color) {
  const int center_x = 120;
  const int center_y = 120;
  const int radius = 111;

  for (int angle = from_angle; angle <= to_angle; angle += 2) {
    const float rad = angle * DEG_TO_RAD;
    const int x = center_x + static_cast<int>(cos(rad) * radius);
    const int y = center_y + static_cast<int>(sin(rad) * radius);
    display->fillCircle(x, y, 2, color);
  }
}

void drawTimerText() {
  const uint16_t face = color565(255, 242, 214);
  const uint16_t ink = color565(35, 38, 42);
  char time_text[6] = {0};
  formatTime(time_text, sizeof(time_text), remaining_seconds);

  display->fillRect(70, 88, 104, 34, face);
  display->setTextColor(ink);
  display->setTextSize(3);
  display->setCursor(84, 92);
  display->print(time_text);

  last_rendered_seconds = remaining_seconds;
}

void drawStatusText() {
  const uint16_t face = color565(255, 242, 214);
  const uint16_t ink = color565(35, 38, 42);

  display->fillRect(74, 128, 92, 32, face);
  display->setTextColor(ink);
  display->setTextSize(2);
  display->setCursor(mode == SessionMode::Focus ? 78 : 78, 130);
  display->print(modeLabel());

  display->setTextSize(1);
  display->setCursor(running ? 100 : 98, 152);
  display->print(running ? "RUN" : "PAUSE");
}

void drawStaticPomodoroHome() {
  const uint16_t background = color565(5, 10, 18);
  const uint16_t outer = color565(16, 32, 52);
  const uint16_t warm = color565(244, 182, 84);
  const uint16_t face = color565(255, 242, 214);

  display->fillScreen(background);
  display->fillCircle(120, 120, 116, outer);
  display->fillCircle(120, 120, 92, progressColor());
  display->fillCircle(120, 120, 64, warm);
  display->fillCircle(120, 120, 46, face);

  display->drawCircle(120, 120, 111, color565(56, 72, 94));
  display->drawCircle(120, 120, 112, color565(56, 72, 94));
  last_progress_angle = -90;
  last_rendered_seconds = UINT32_MAX;

  const float progress = 1.0f - (static_cast<float>(remaining_seconds) / sessionSeconds());
  const int target_angle = static_cast<int>(-90 + progress * 360.0f);
  drawProgressArcSegment(-90, target_angle, color565(228, 244, 250));
  last_progress_angle = target_angle;

  drawTimerText();
  drawStatusText();
}

void renderTick() {
  if (remaining_seconds != last_rendered_seconds) {
    drawTimerText();
  }

  const float progress = 1.0f - (static_cast<float>(remaining_seconds) / sessionSeconds());
  const int target_angle = static_cast<int>(-90 + progress * 360.0f);
  if (target_angle > last_progress_angle) {
    drawProgressArcSegment(last_progress_angle + 1, target_angle, color565(228, 244, 250));
    last_progress_angle = target_angle;
  }
}

void redrawProgressTrack() {
  display->drawCircle(120, 120, 111, color565(56, 72, 94));
  display->drawCircle(120, 120, 112, color565(56, 72, 94));
  last_progress_angle = -90;
}

void renderResetWithoutFullRedraw() {
  const uint16_t outer = color565(16, 32, 52);

  display->drawCircle(120, 120, 111, outer);
  display->drawCircle(120, 120, 112, outer);
  redrawProgressTrack();
  last_rendered_seconds = UINT32_MAX;
  drawTimerText();
  drawStatusText();
}

void resetCurrentSession() {
  running = false;
  remaining_seconds = sessionSeconds();
  last_countdown_ms = millis();
  renderResetWithoutFullRedraw();
  Serial.println("Session reset");
}

void switchSession() {
  mode = mode == SessionMode::Focus ? SessionMode::Break : SessionMode::Focus;
  running = false;
  remaining_seconds = sessionSeconds();
  last_countdown_ms = millis();
  drawStaticPomodoroHome();
  Serial.printf("Switched to %s\n", modeLabel());
}

void updateCountdown(unsigned long now) {
  if (!running) {
    last_countdown_ms = now;
    return;
  }

  if (now - last_countdown_ms < 1000) {
    return;
  }

  const uint32_t elapsed = (now - last_countdown_ms) / 1000;
  last_countdown_ms += elapsed * 1000;

  if (elapsed >= remaining_seconds) {
    remaining_seconds = 0;
  } else {
    remaining_seconds -= elapsed;
  }

  renderTick();

  if (remaining_seconds == 0) {
    Serial.println("Session complete");
    switchSession();
  }
}

void setup() {
  Serial.begin(115200);

  // Give the USB CDC serial port a moment to enumerate on boot.
  delay(1500);

  Serial.println();
  Serial.println("XIAO ESP32S3 Round Display Arduino_GFX test");

  setBacklight(BACKLIGHT_BRIGHTNESS);
  pinMode(TOUCH_INT, INPUT_PULLUP);
  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  scanI2C();

  if (!display->begin(10000000)) {
    Serial.println("Display init failed");
    return;
  }

  drawStaticPomodoroHome();

  Serial.println("Display initialized");
}

void loop() {
  static unsigned long last_print_ms = 0;
  static unsigned long last_touch_ms = 0;
  static unsigned long touch_started_ms = 0;
  static unsigned long last_touch_seen_ms = 0;
  static bool touch_active = false;
  static bool long_press_handled = false;
  static uint8_t last_touch_x = 120;
  static uint8_t last_touch_y = 120;
  const unsigned long now = millis();

  uint8_t touch_x = 0;
  uint8_t touch_y = 0;
  const bool touched = readTouch(touch_x, touch_y);
  if (touched) {
    last_touch_x = touch_x;
    last_touch_y = touch_y;

    if (!touch_active) {
      touch_active = true;
      touch_started_ms = now;
      long_press_handled = false;
    }

    last_touch_seen_ms = now;
  }

  if (touch_active && !long_press_handled && now - touch_started_ms >= LONG_PRESS_MS) {
    long_press_handled = true;
    last_touch_ms = now;
    resetCurrentSession();
    Serial.printf("Long press reset: x=%u y=%u\n", last_touch_x, last_touch_y);
  }

  const bool touch_released = touch_active && now - last_touch_seen_ms > TOUCH_RELEASE_GAP_MS;
  if (touch_released && !long_press_handled && now - last_touch_ms > 300) {
    last_touch_ms = now;
    running = !running;
    last_countdown_ms = now;
    drawStatusText();

    Serial.printf("Touch: x=%u y=%u, running=%s\n", last_touch_x, last_touch_y, running ? "true" : "false");
  }

  if (touch_released) {
    touch_active = false;
  }

  updateCountdown(now);

  if (now - last_print_ms >= 1000) {
    last_print_ms = now;
    Serial.printf("Pomodoro: %s %lus running=%s\n", modeLabel(), remaining_seconds, running ? "true" : "false");
  }
}
