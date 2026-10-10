#include "ui/renderer.hpp"
#include "themes.hpp"
#include <nanovg.h>
#include <cmath>

namespace morph::ui {

float Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

static unsigned char LerpByte(unsigned char a, unsigned char b, float t) {
    return (unsigned char)(a + (b - a) * t);
}

void DrawBackground(NVGcontext* vg, float w, float h, float time_of_day,
                    bool applet_mode, float anim_t, int preset, themes::Rgb custom) {
    themes::Rgb day_top, day_bot, night_top, night_bot;
    if (themes::Clamp(preset) == themes::kCustomTheme) {
        day_top = custom;
        day_bot = themes::Scale(custom, 0.75f);
        night_top = themes::Scale(custom, 0.25f);
        night_bot = themes::Scale(custom, 0.12f);
    } else {
        const auto& theme = themes::kThemes[themes::Clamp(preset)];
        day_top = theme.day_top;
        day_bot = theme.day_bot;
        night_top = theme.night_top;
        night_bot = theme.night_bot;
    }

    // Applet mode is always plain black; the theme does not apply there.
    if (applet_mode) {
        day_top = day_bot = night_top = night_bot = {0, 0, 0};
    }

    // Calculate blend: 0 = full night, 1 = full day
    // time_of_day: 0.0 = midnight, 0.25 = 6am, 0.5 = noon, 0.75 = 6pm
    float day_blend;
    if (time_of_day < 0.2f) {
        day_blend = 0.f; // night
    } else if (time_of_day < 0.3f) {
        day_blend = (time_of_day - 0.2f) / 0.1f; // sunrise
    } else if (time_of_day < 0.7f) {
        day_blend = 1.f; // day
    } else if (time_of_day < 0.8f) {
        day_blend = 1.f - (time_of_day - 0.7f) / 0.1f; // sunset
    } else {
        day_blend = 0.f; // night
    }

    auto top = nvgRGBA(
        LerpByte(night_top.r, day_top.r, day_blend),
        LerpByte(night_top.g, day_top.g, day_blend),
        LerpByte(night_top.b, day_top.b, day_blend), 255);
    auto bot = nvgRGBA(
        LerpByte(night_bot.r, day_bot.r, day_blend),
        LerpByte(night_bot.g, day_bot.g, day_blend),
        LerpByte(night_bot.b, day_bot.b, day_blend), 255);

    auto bg = nvgLinearGradient(vg, 0, 0, 0, h, top, bot);
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, w, h);
    nvgFillPaint(vg, bg);
    nvgFill(vg);

    // Drifting light blobs (animated)
    for (int i = 0; i < 3; i++) {
        float fi = (float)i;
        float bx = w * (0.25f + 0.25f * fi) + std::sin(anim_t * (0.30f + 0.10f * fi) + fi) * w * 0.12f;
        float by = h * (0.30f + 0.20f * fi) + std::cos(anim_t * (0.25f + 0.07f * fi) + fi * 2.f) * h * 0.10f;
        auto blob = nvgRadialGradient(vg, bx, by, 0.f, w * 0.35f,
            applet_mode ? nvgRGBA(0, 0, 0, 0)
                        : nvgRGBA(120, 190, 255, 40),
            nvgRGBA(0, 0, 0, 0));
        nvgBeginPath(vg);
        nvgRect(vg, 0, 0, w, h);
        nvgFillPaint(vg, blob);
        nvgFill(vg);
    }

    // Applet mode: a large red "APPLET" watermark on black, behind the grid
    if (applet_mode) {
        nvgFontFace(vg, "Standard");
        nvgFontSize(vg, 220.f);
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(vg, nvgRGBA(200, 20, 24, 150));
        nvgText(vg, w / 2.f, h / 2.f, "APPLET", nullptr);
    }

}

void DrawGlassCell(NVGcontext* vg, float x, float y, float w, float h, float radius,
                   float select_anim, float gyro_x, float gyro_y,
                   const AccentColors* accent, int icon_texture, float pulse_t) {
    // Scale with a small overshoot-style bump while selected
    float bump = select_anim * (1.f - select_anim) * 4.f * 0.03f; // peaks mid-transition
    float scale = 1.0f + select_anim * 0.05f + bump;
    float sw = w * scale;
    float sh = h * scale;
    float sx = x - (sw - w) * 0.5f;
    float sy = y - (sh - h) * 0.5f;

    if (select_anim > 0.01f) {
        float k = select_anim * (0.6f + 0.4f * std::sin(pulse_t * 3.f));
        auto halo = nvgBoxGradient(vg, sx, sy, sw, sh, radius, 24.f,
            nvgRGBA(255, 255, 255, (unsigned char)(90.f * k)), nvgRGBA(255, 255, 255, 0));
        nvgBeginPath(vg);
        nvgRect(vg, sx - 60.f, sy - 60.f, sw + 120.f, sh + 120.f);
        nvgFillPaint(vg, halo);
        nvgFill(vg);
    }

    nvgBeginPath(vg);
    nvgRoundedRect(vg, sx, sy, sw, sh, radius);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 14));
    nvgFill(vg);

    if (icon_texture > 0) {
        float pad = sw * 0.1f;
        float ix = sx + pad;
        float iy = sy + pad;
        float iw = sw - pad * 2.f;
        float ih = sh - pad * 2.f;

        nvgBeginPath(vg);
        nvgRoundedRect(vg, ix, iy, iw, ih, radius * 0.5f);
        nvgFillColor(vg, nvgRGBA(0, 0, 0, 200));
        nvgFill(vg);

        auto paint = nvgImagePattern(vg, ix, iy, iw, ih, 0, icon_texture, 1.0f);
        nvgBeginPath(vg);
        nvgRoundedRect(vg, ix, iy, iw, ih, radius * 0.5f);
        nvgFillPaint(vg, paint);
        nvgFill(vg);
    }

    {
        float gx = gyro_y * 8.f;
        float gy = gyro_x * 5.f;
        float alpha = 30.f + select_anim * 20.f;

        auto glare = nvgLinearGradient(vg,
            sx + gx, sy + gy,
            sx + gx, sy + sh * 0.5f + gy,
            nvgRGBA(255, 255, 255, (unsigned char)alpha),
            nvgRGBA(255, 255, 255, 0));

        nvgSave(vg);
        nvgIntersectScissor(vg, sx, sy, sw, sh);
        nvgBeginPath(vg);
        nvgRoundedRect(vg, sx, sy, sw, sh, radius);
        nvgFillPaint(vg, glare);
        nvgFill(vg);
        nvgRestore(vg);
    }

    nvgBeginPath(vg);
    nvgRoundedRect(vg, sx, sy, sw, sh, radius);
    nvgStrokeColor(vg, nvgRGBA(255, 255, 255, (unsigned char)(20 + select_anim * 30)));
    nvgStrokeWidth(vg, 1.0f);
    nvgStroke(vg);

    if (select_anim > 0.01f) {
        auto glow = nvgBoxGradient(vg, sx, sy, sw, sh, radius, 10.0f,
            nvgRGBA(255, 255, 255, (unsigned char)(select_anim * 50)),
            nvgRGBA(255, 255, 255, 0));
        nvgBeginPath(vg);
        nvgRect(vg, sx - 12, sy - 12, sw + 24, sh + 24);
        nvgRoundedRect(vg, sx, sy, sw, sh, radius);
        nvgPathWinding(vg, NVG_HOLE);
        nvgFillPaint(vg, glow);
        nvgFill(vg);

        nvgBeginPath(vg);
        nvgRoundedRect(vg, sx, sy, sw, sh, radius);
        nvgStrokeColor(vg, nvgRGBA(255, 255, 255, (unsigned char)(select_anim * 180)));
        nvgStrokeWidth(vg, 2.0f);
        nvgStroke(vg);
    }
}

void DrawTooltip(NVGcontext* vg, float x, float y, float w, float h,
                 float arrow_x, const char* title, const char* subtitle, bool arrow_up,
                 float& scroll_time) {
    float radius = 10.f;
    float arrow_size = 8.f;

    // Background
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, w, h, radius);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 235));
    nvgFill(vg);

    // Glare
    auto glare = nvgLinearGradient(vg, x, y, x, y + h * 0.5f,
        nvgRGBA(255, 255, 255, 40), nvgRGBA(255, 255, 255, 0));
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x + 1, y + 1, w - 2, h * 0.5f, radius);
    nvgFillPaint(vg, glare);
    nvgFill(vg);

    // Arrow
    nvgBeginPath(vg);
    if (arrow_up) {
        nvgMoveTo(vg, arrow_x - arrow_size, y);
        nvgLineTo(vg, arrow_x, y - arrow_size);
        nvgLineTo(vg, arrow_x + arrow_size, y);
    } else {
        nvgMoveTo(vg, arrow_x - arrow_size, y + h);
        nvgLineTo(vg, arrow_x, y + h + arrow_size);
        nvgLineTo(vg, arrow_x + arrow_size, y + h);
    }
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 235));
    nvgFill(vg);

    // Scrolling title
    nvgSave(vg);
    nvgIntersectScissor(vg, x + 8, y, w - 16, h);
    nvgFontFace(vg, "Standard");

    {
        nvgFontSize(vg, 16.f);
        float bounds[4];
        nvgTextBounds(vg, 0, 0, title, nullptr, bounds);
        float text_w = bounds[2] - bounds[0];
        float max_w = w - 20.f;

        if (text_w > max_w) {
            // Scroll: accumulate time passed in from caller so it resets when the selection changes.
            scroll_time += 1.f / 60.f;
            float scroll_speed = 30.f;
            float total = text_w + 40.f;
            float offset = std::fmod(scroll_time * scroll_speed, total);

            nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
            nvgFillColor(vg, nvgRGBA(15, 20, 40, 255));
            nvgText(vg, x + 10.f - offset, y + 8.f, title, nullptr);
            nvgText(vg, x + 10.f - offset + total, y + 8.f, title, nullptr);
        } else {
            nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
            nvgFillColor(vg, nvgRGBA(15, 20, 40, 255));
            nvgText(vg, x + w / 2.f, y + 8.f, title, nullptr);
        }
    }

    // Subtitle (always centered, clipped)
    nvgFontSize(vg, 12.f);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
    nvgFillColor(vg, nvgRGBA(70, 80, 100, 255));
    nvgText(vg, x + w / 2.f, y + 28.f, subtitle, nullptr);

    nvgRestore(vg);
}

} // namespace morph::ui
