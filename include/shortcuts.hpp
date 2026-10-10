#pragma once

#include "nro.hpp"

// HOME-screen shortcuts (forwarders). Requires prod.keys to build forwarder NCAs.

namespace morph {

enum class ShortcutResult {
    NoProdKeys,
    Installed,
    BuildFailed,
    InstallFailed,
};

ShortcutResult CreateHbmenuShortcut();
ShortcutResult CreateShortcut(const NroEntry& entry);
const char* ShortcutResultText(ShortcutResult result);

} // namespace morph
