#pragma once

#include <Arduino.h>
#include "app_state.h"

bool ui_begin();
void ui_set_backlight(uint8_t brightness);
void ui_apply_brightness(uint32_t brightness_percent);
void ui_draw_static_pomodoro_home();
void ui_draw_idle_screen();
void ui_draw_session_feedback(SessionMode next_mode, const char *message);
void ui_draw_settings(
    uint32_t focus_minutes,
    uint32_t brightness_percent,
    uint32_t completed_focus_count,
    uint8_t selected_item);
void ui_draw_status_text();
void ui_draw_microphone_status(const char *label);
void ui_render_tick();
void ui_render_reset_without_full_redraw();
