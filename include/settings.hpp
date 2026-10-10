#pragma once

#include "themes.hpp"

// Persistent user settings (sdmc:/config/morph/settings.txt).

namespace morph::settings {


struct Settings {
    bool time_bg{false};                          // optional day/night background; off = fixed night palette
    int bg_preset{themes::kDefaultTheme};         // index into themes::kThemes, or kCustomTheme
    themes::Rgb custom{20, 130, 150};             // colour chosen in the picker
};

Settings& Get();

// Favourites (R3): homebrew paths marked with a star, kept across launches.
bool IsFavorite(const char* path);
void SetFavorite(const char* path, bool on);
void Load();
void Save();

} // namespace morph::settings
