#pragma once

#include <cmath>
#include <cstdio>
#include <cstring>

// Background colour: the fixed Ocean palette, or the colour the user picked.
namespace morph::themes {

struct Rgb {
    unsigned char r, g, b;
};

struct Theme {
    const char* key;
    Rgb day_top, day_bot;
    Rgb night_top, night_bot;
};

inline constexpr Theme kThemes[] = {
    {"ocean", {20, 130, 150}, {30, 160, 175}, {4, 30, 40}, {8, 56, 70}},
};

inline constexpr int kThemeCount = sizeof(kThemes) / sizeof(kThemes[0]);
inline constexpr int kDefaultTheme = 0; // Ocean
// One extra slot after the fixed list: the user's own colour from the picker.
inline constexpr int kCustomTheme = kThemeCount;

// Index for a stable key, or -1 if the key is unknown.
inline int IndexOfKey(const char* key) {
    for (int i = 0; i < kThemeCount; i++) {
        if (std::strcmp(kThemes[i].key, key) == 0) return i;
    }
    if (std::strcmp(key, "custom") == 0) return kCustomTheme;
    return -1;
}

inline int Clamp(int index) {
    return (index >= 0 && index <= kCustomTheme) ? index : kDefaultTheme;
}

// Colour maths for the picker. h in degrees, s and v in 0..1.
inline Rgb HsvToRgb(float h, float s, float v) {
    h = std::fmod(std::fmod(h, 360.f) + 360.f, 360.f);
    const float c = v * s;
    const float x = c * (1.f - std::fabs(std::fmod(h / 60.f, 2.f) - 1.f));
    const float m = v - c;
    float r = 0, g = 0, b = 0;
    if (h < 60)       { r = c; g = x; }
    else if (h < 120) { r = x; g = c; }
    else if (h < 180) { g = c; b = x; }
    else if (h < 240) { g = x; b = c; }
    else if (h < 300) { r = x; b = c; }
    else              { r = c; b = x; }
    auto to8 = [](float f) { return (unsigned char)(f * 255.f + 0.5f); };
    return {to8(r + m), to8(g + m), to8(b + m)};
}

inline void RgbToHsv(Rgb c, float& h, float& s, float& v) {
    const float r = c.r / 255.f, g = c.g / 255.f, b = c.b / 255.f;
    const float mx = std::fmax(r, std::fmax(g, b));
    const float mn = std::fmin(r, std::fmin(g, b));
    const float d = mx - mn;
    v = mx;
    s = mx > 0 ? d / mx : 0;
    if (d <= 0) { h = 0; return; }
    if (mx == r)      h = 60.f * std::fmod((g - b) / d, 6.f);
    else if (mx == g) h = 60.f * ((b - r) / d + 2.f);
    else              h = 60.f * ((r - g) / d + 4.f);
    if (h < 0) h += 360.f;
}

inline Rgb Scale(Rgb c, float k) {
    auto f = [k](unsigned char x) { return (unsigned char)(x * k + 0.5f); };
    return {f(c.r), f(c.g), f(c.b)};
}

} // namespace morph::themes
