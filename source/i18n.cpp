#include "i18n.hpp"

#include <switch.h>

namespace morph::i18n {
namespace {

bool s_russian = false;

struct Table {
    const char* ru;
    const char* en;
};

// One row per Str, in exactly the same order as the enum. The static_assert below catches drift.
constexpr Table kStrings[] = {
    /* Settings              */ {"Настройки", "Settings"},
    /* On                    */ {"ВКЛ", "ON"},
    /* Off                   */ {"ВЫКЛ", "OFF"},
    /* BgColor               */ {"Цвет фона", "Background color"},
    /* ShortcutHbmenu        */ {"Создать форвардер для Homebrew Launcher", "Create forwarder for Homebrew Launcher"},
    /* ShortcutCreateFor     */ {"Создать форвардер для", "Create forwarder for"},
    /* ShortcutPickInGrid    */ {"(выберите приложение в сетке)", "(select an app in the grid)"},
    /* MenuFooter            */ {"A Выбрать  |  B/- Закрыть", "A Select  |  B/- Close"},
    /* StatusPickHomebrew    */ {"Сначала выберите приложение", "Select an app first"},
    /* StatusNoProdKeys      */ {"Не найден /switch/prod.keys", "No /switch/prod.keys found"},
    /* StatusInstalled       */ {"Форвардер установлен", "Forwarder installed"},
    /* StatusInstallFailed   */ {"Ошибка установки (см. log.txt)", "Failed to install (see log.txt)"},
    /* StatusBuildFailed     */ {"Ошибка сборки форвардера (см. log.txt)", "Failed to build forwarder (see log.txt)"},
    /* Legend                */ {"A Запуск  |  R3 Звезда  |  - Настройки  |  ZL/ZR Страница  |  B/+ Выход",
                                "A Launch  |  R3 Star  |  - Menu  |  ZL/ZR Page  |  B/+ Exit"},
    /* CustomColour          */ {"Свой цвет", "Custom color"},
    /* PickerHint            */ {"Стики: насыщенность, яркость, тон  |  A ввести HEX  |  B закрыть",
                                "Sticks: saturation, brightness, hue  |  A type HEX  |  B close"},
    /* HexLabel              */ {"HEX", "HEX"},
};
static_assert(sizeof(kStrings) / sizeof(kStrings[0]) == static_cast<int>(Str::Count),
              "kStrings must have one row per Str");

} // namespace

void Init() {
    s_russian = true;   // fallback if the system language cannot be read
    if (R_FAILED(setInitialize())) return;

    u64 current = 0, russian = 0;
    if (R_SUCCEEDED(setGetSystemLanguage(&current)) && R_SUCCEEDED(setMakeLanguageCode(SetLanguage_RU, &russian))) {
        s_russian = (current == russian);
    }
    setExit();
}

const char* T(Str id) {
    const auto& row = kStrings[static_cast<int>(id)];
    return s_russian ? row.ru : row.en;
}
} // namespace morph::i18n
