#pragma once

namespace morph::i18n {

enum class Str {
    Settings,
    On,
    Off,
    BgColor,
    ShortcutHbmenu,
    ShortcutCreateFor,
    ShortcutPickInGrid,
    MenuFooter,
    StatusPickHomebrew,
    StatusNoProdKeys,
    StatusInstalled,
    StatusInstallFailed,
    StatusBuildFailed,
    Legend,
    CustomColour,
    PickerHint,
    HexLabel,
    Count,   // keep last: size of the table
};

// Picks the language once at start-up from the console's system language (Russian or English).
void Init();
const char* T(Str id);

} // namespace morph::i18n
