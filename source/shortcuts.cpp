#include "shortcuts.hpp"
#include "forwarder/forwarder.hpp"
#include "i18n.hpp"
#include "keys.hpp"
#include "log.hpp"
#include "nro.hpp"

#include <cstdio>
#include <cstring>
#include <string>

namespace morph {
namespace {

constexpr const char* kForwarderRoot = "sdmc:/config/morph/forwarder";

// Same normalisation nro_launch() applies, so the forwarder gets the absolute sdmc:/ path.
std::string AbsoluteSdmcPath(std::string path) {
    if (path.starts_with("sdmc:/")) return path;
    if (path.starts_with("sdmc:")) return "sdmc:/" + path.substr(5);
    if (path.starts_with("/")) return "sdmc:" + path;
    return "sdmc:/" + path;
}

// Reads the NACP that the NRO carries in its ASET section (the same data the NRO's own icon comes from).
std::vector<u8> ReadNroNacp(const char* path) {
    std::vector<u8> out;
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return out;

    u32 nro_size = 0;
    std::fseek(f, 0x18, SEEK_SET);
    std::fread(&nro_size, sizeof(nro_size), 1, f);

    u8 hdr[56];
    std::fseek(f, nro_size, SEEK_SET);
    if (std::fread(hdr, 1, sizeof(hdr), f) == sizeof(hdr) && std::memcmp(hdr, "ASET", 4) == 0) {
        u64 nacp_off = 0, nacp_size = 0;
        std::memcpy(&nacp_off, hdr + 24, sizeof(nacp_off));
        std::memcpy(&nacp_size, hdr + 32, sizeof(nacp_size));
        if (nacp_size == sizeof(NacpStruct)) {
            out.resize(nacp_size);
            std::fseek(f, nro_size + nacp_off, SEEK_SET);
            if (std::fread(out.data(), 1, out.size(), f) != out.size()) out.clear();
        }
    }
    std::fclose(f);
    return out;
}

bool HasProdKeys() {
    std::FILE* f = std::fopen(keys::kProdKeysPath, "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

ShortcutResult BuildAndInstall(const forwarder::Config& config) {
    forwarder::BuildResult built{};
    const Result build_rc = forwarder::BuildFiles(config, kForwarderRoot, &built);
    if (R_FAILED(build_rc)) {
        log_write("shortcut: BuildFiles failed: 0x%X\n", build_rc);
        return ShortcutResult::BuildFailed;
    }

    const Result install_rc = forwarder::InstallBuilt(built.dir, built.tid, NcmStorageId_SdCard);
    if (R_FAILED(install_rc)) {
        log_write("shortcut: install failed: 0x%X\n", install_rc);
        return ShortcutResult::InstallFailed;
    }
    return ShortcutResult::Installed;
}

} // namespace

ShortcutResult CreateHbmenuShortcut() {
    log_write("shortcut: hbmenu requested\n");
    if (!HasProdKeys()) {
        log_write("shortcut: %s missing\n", keys::kProdKeysPath);
        return ShortcutResult::NoProdKeys;
    }

    // hbmenu's own icon and name, read from the NRO on the SD card.
    // nro_get_icon() takes a path relative to the SD root (like the grid entries: "/switch/..."),
    // while the forwarder config and stdio use the absolute "sdmc:/..." form.
    fs::FsPath rel{};
    std::snprintf(rel.s, sizeof(rel.s), "%s", "/hbmenu.nro");
    const char* absolute = "sdmc:/hbmenu.nro";

    forwarder::Config config{};
    config.nro_path = absolute;
    config.name = "Homebrew Launcher";   // always this name, not the one from the NRO
    config.author = "qwaqzq";            // same author the morph NRO declares (CMakeLists.txt)
    config.icon = nro_get_icon(rel);
    config.nacp = ReadNroNacp(absolute);
    return BuildAndInstall(config);
}

ShortcutResult CreateShortcut(const NroEntry& entry) {
    log_write("shortcut: entry requested: %s\n", entry.path.s);
    if (!HasProdKeys()) {
        log_write("shortcut: %s missing\n", keys::kProdKeysPath);
        return ShortcutResult::NoProdKeys;
    }

    forwarder::Config config{};
    config.nro_path = AbsoluteSdmcPath(entry.path.s);
    config.name = entry.nacp.lang.name;
    config.author = entry.nacp.lang.author;
    config.icon = nro_get_icon(entry.path, entry.icon_size, entry.icon_offset);
    config.nacp = ReadNroNacp(config.nro_path.c_str());
    return BuildAndInstall(config);
}

const char* ShortcutResultText(ShortcutResult result) {
    switch (result) {
    case ShortcutResult::NoProdKeys:    return i18n::T(i18n::Str::StatusNoProdKeys);
    case ShortcutResult::Installed:     return i18n::T(i18n::Str::StatusInstalled);
    case ShortcutResult::InstallFailed: return i18n::T(i18n::Str::StatusInstallFailed);
    case ShortcutResult::BuildFailed:   return i18n::T(i18n::Str::StatusBuildFailed);
    }
    return "";
}

} // namespace morph
