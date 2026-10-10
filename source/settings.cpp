#include "settings.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <sys/stat.h>

namespace morph::settings {
namespace {

constexpr const char* kConfigDir = "sdmc:/config";
constexpr const char* kAppDir = "sdmc:/config/morph";
constexpr const char* kPath = "sdmc:/config/morph/settings.txt";
constexpr const char* kFavPath = "sdmc:/config/morph/favorites.txt";

Settings s_settings;

} // namespace

Settings& Get() {
    return s_settings;
}

void Load() {
    std::FILE* f = std::fopen(kPath, "r");
    if (!f) return;

    char line[64];
    while (std::fgets(line, sizeof(line), f)) {
        if (std::strncmp(line, "time_bg=", 8) == 0) {
            s_settings.time_bg = (line[8] == '1');
        } else if (std::strncmp(line, "bgkey=", 6) == 0) {
            line[std::strcspn(line, "\r\n")] = '\0';
            const int idx = themes::IndexOfKey(line + 6);
            s_settings.bg_preset = (idx >= 0) ? idx : themes::kDefaultTheme;
        } else if (std::strncmp(line, "custom=", 7) == 0) {
            unsigned r = 0, g = 0, b = 0;
            if (std::sscanf(line + 7, "%02x%02x%02x", &r, &g, &b) == 3) {
                s_settings.custom = {(unsigned char)r, (unsigned char)g, (unsigned char)b};
            }
        }
    }
    std::fclose(f);
}

void Save() {
    mkdir(kConfigDir, 0777);
    mkdir(kAppDir, 0777);

    std::FILE* f = std::fopen(kPath, "w");
    if (!f) return;
    std::fprintf(f, "time_bg=%d\n", s_settings.time_bg ? 1 : 0);
    const int idx = themes::Clamp(s_settings.bg_preset);
    std::fprintf(f, "bgkey=%s\n", idx == themes::kCustomTheme ? "custom" : themes::kThemes[idx].key);
    std::fprintf(f, "custom=%02x%02x%02x\n", s_settings.custom.r, s_settings.custom.g, s_settings.custom.b);
    std::fclose(f);
}

bool IsFavorite(const char* path) {
    std::FILE* f = std::fopen(kFavPath, "r");
    if (!f) return false;
    char line[0x320];
    bool found = false;
    while (!found && std::fgets(line, sizeof(line), f)) {
        line[std::strcspn(line, "\r\n")] = '\0';
        found = std::strcmp(line, path) == 0;
    }
    std::fclose(f);
    return found;
}

void SetFavorite(const char* path, bool on) {
    std::vector<std::string> lines;
    if (std::FILE* f = std::fopen(kFavPath, "r")) {
        char line[0x320];
        while (std::fgets(line, sizeof(line), f)) {
            line[std::strcspn(line, "\r\n")] = '\0';
            if (line[0] && std::strcmp(line, path) != 0) lines.emplace_back(line);
        }
        std::fclose(f);
    }
    if (on) lines.emplace_back(path);

    mkdir(kConfigDir, 0777);
    mkdir(kAppDir, 0777);
    if (std::FILE* f = std::fopen(kFavPath, "w")) {
        for (const auto& l : lines) std::fprintf(f, "%s\n", l.c_str());
        std::fclose(f);
    }
}

} // namespace morph::settings
