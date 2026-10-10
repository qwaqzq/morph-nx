#pragma once

#include "themes.hpp"

struct NVGcontext;

namespace morph::ui {

struct Color {
    float r, g, b, a;
};

struct AccentColors {
    Color primary;
    Color secondary;
    Color glow;
};

void DrawBackground(NVGcontext* vg, float w, float h, float time_of_day,
                    bool applet_mode = false, float anim_t = 0.f, int preset = 0,
                    themes::Rgb custom = {20, 130, 150});

void DrawGlassCell(NVGcontext* vg, float x, float y, float w, float h, float radius,
                   float select_anim, float gyro_x, float gyro_y,
                   const AccentColors* accent = nullptr, int icon_texture = 0,
                   float pulse_t = 0.f);

// scroll_time is a per-callsite float that the caller owns and resets when the
// displayed text changes.  It is accumulated inside the function when the text
// is wider than the available box.
void DrawTooltip(NVGcontext* vg, float x, float y, float w, float h,
                 float arrow_x, const char* title, const char* subtitle,
                 bool arrow_up, float& scroll_time);

float Lerp(float a, float b, float t);

} // namespace morph::ui
