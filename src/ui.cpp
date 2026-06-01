#include "ui.h"

#include <Arduino_GFX_Library.h>
#include <math.h>
#include "pomodoro_timer.h"
#include "version.h"

// Round Display for XIAO uses XIAO D8/D10 for SPI and D1/D3 for LCD CS/DC.
// XIAO ESP32S3 mapping: D8=GPIO7, D10=GPIO9, D1=GPIO2, D3=GPIO4, D6=GPIO43.
namespace {
constexpr int LCD_SCLK = 7;
constexpr int LCD_MOSI = 9;
constexpr int LCD_CS = 2;
constexpr int LCD_DC = 4;
constexpr int LCD_BL = 43;
constexpr int BACKLIGHT_CHANNEL = 0;

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

void eraseProgressArc() {
  const uint16_t outer = color565(16, 32, 52);
  const int center_x = 120;
  const int center_y = 120;
  const int radius = 111;

  for (int angle = -90; angle <= 270; angle++) {
    const float rad = angle * DEG_TO_RAD;
    const int x = center_x + static_cast<int>(cos(rad) * radius);
    const int y = center_y + static_cast<int>(sin(rad) * radius);
    display->fillCircle(x, y, 4, outer);
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

void redrawProgressTrack() {
  display->drawCircle(120, 120, 111, color565(56, 72, 94));
  display->drawCircle(120, 120, 112, color565(56, 72, 94));
  last_progress_angle = -90;
}
}  // namespace

void ui_set_backlight(uint8_t brightness) {
  ledcSetup(BACKLIGHT_CHANNEL, 5000, 8);
  ledcAttachPin(LCD_BL, BACKLIGHT_CHANNEL);
  ledcWrite(BACKLIGHT_CHANNEL, brightness);
}

void ui_apply_brightness(uint32_t brightness_percent) {
  const uint8_t brightness = static_cast<uint8_t>((brightness_percent * 255UL) / 100UL);
  ui_set_backlight(brightness);
}

bool ui_begin() {
  return display->begin(10000000);
}

void ui_draw_status_text() {
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

void ui_draw_static_pomodoro_home() {
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
  ui_draw_status_text();
}

void ui_draw_idle_screen() {
  const uint16_t background = color565(5, 10, 18);
  const uint16_t outer = color565(16, 32, 52);
  const uint16_t accent = color565(38, 150, 178);
  const uint16_t face = color565(255, 242, 214);
  const uint16_t ink = color565(35, 38, 42);

  display->fillScreen(background);
  display->fillCircle(120, 120, 116, outer);
  display->fillCircle(120, 120, 86, accent);
  display->fillCircle(120, 120, 58, face);

  display->setTextColor(ink);
  display->setTextSize(2);
  display->setCursor(92, 82);
  display->print("XIAO");
  display->setCursor(62, 110);
  display->print("Companion");

  display->setTextSize(1);
  display->setCursor(84, 144);
  display->print("Tap to focus");

  last_rendered_seconds = UINT32_MAX;
  last_progress_angle = -90;
}

void ui_draw_session_feedback(SessionMode next_mode, const char *message) {
  const uint16_t background = color565(5, 10, 18);
  const uint16_t outer = color565(16, 32, 52);
  const uint16_t accent = next_mode == SessionMode::Break ? color565(74, 172, 104) : color565(38, 150, 178);
  const uint16_t face = color565(255, 242, 214);
  const uint16_t ink = color565(35, 38, 42);

  display->fillScreen(background);
  display->fillCircle(120, 120, 116, outer);
  display->fillCircle(120, 120, 86, accent);
  display->fillCircle(120, 120, 58, face);

  display->setTextColor(ink);
  display->setTextSize(2);
  display->setCursor(next_mode == SessionMode::Break ? 58 : 55, 98);
  display->print(next_mode == SessionMode::Break ? "Break Time" : "Focus Time");

  display->setTextSize(1);
  display->setCursor(next_mode == SessionMode::Break ? 78 : 90, 128);
  display->print(message);

  last_rendered_seconds = UINT32_MAX;
  last_progress_angle = -90;
}

void ui_draw_settings(
    uint32_t focus_minutes,
    uint32_t brightness_percent,
    uint32_t completed_focus_count,
    uint8_t selected_item) {
  const uint16_t background = color565(5, 10, 18);
  const uint16_t outer = color565(16, 32, 52);
  const uint16_t accent = color565(244, 182, 84);
  const uint16_t face = color565(255, 242, 214);
  const uint16_t ink = color565(35, 38, 42);

  display->fillScreen(background);
  display->fillCircle(120, 120, 116, outer);
  display->fillCircle(120, 120, 90, accent);
  display->fillCircle(120, 120, 64, face);

  display->setTextColor(ink);
  display->setTextSize(1);
  display->setCursor(92, 66);
  display->print("Settings");

  display->setCursor(62, 90);
  display->print(selected_item == 0 ? "> Focus" : "  Focus");
  display->setCursor(132, 90);
  display->print(focus_minutes);
  display->print(" min");

  display->setCursor(62, 112);
  display->print(selected_item == 1 ? "> Bright" : "  Bright");
  display->setCursor(132, 112);
  display->print(brightness_percent);
  display->print("%");

  display->setCursor(62, 132);
  display->print(selected_item == 2 ? "> Reset" : "  Reset");

  display->setTextSize(1);
  display->setCursor(70, 154);
  display->print("Left switch");
  display->setCursor(68, 168);
  display->print("Right change");
  display->setCursor(84, 182);
  display->print("Hold save");
  display->setCursor(86, 194);
  display->print("Done: ");
  display->print(completed_focus_count);
  display->setCursor(106, 202);
  display->print(APP_VERSION);

  last_rendered_seconds = UINT32_MAX;
  last_progress_angle = -90;
}

void ui_render_tick() {
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

void ui_render_reset_without_full_redraw() {
  eraseProgressArc();
  redrawProgressTrack();
  last_rendered_seconds = UINT32_MAX;
  drawTimerText();
  ui_draw_status_text();
}

