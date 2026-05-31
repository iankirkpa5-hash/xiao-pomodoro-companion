#pragma once

#include <Arduino.h>
#include "app_state.h"

bool ui_begin();
void ui_set_backlight(uint8_t brightness);
void ui_draw_static_pomodoro_home();
void ui_draw_session_feedback(SessionMode next_mode);
void ui_draw_status_text();
void ui_render_tick();
void ui_render_reset_without_full_redraw();
