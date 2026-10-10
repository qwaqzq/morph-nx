#pragma once

#include <switch.h>
#include <vector>
#include <string>
#include <span>
#include <optional>
#include "fs.hpp"

namespace morph {

struct NroData {
    NroStart start;
    NroHeader header;
};

struct Hbini {
    u64 timestamp{}; // timestamp of last launch
    bool hidden{};
};

struct MiniNacp {
    NacpLanguageEntry lang;
    char display_version[0x10];
};

struct NroEntry {
    fs::FsPath path{};
    s64 size{};
    MiniNacp nacp{};

    u64 icon_size{};
    u64 icon_offset{};

    FsTimeStampRaw timestamp{};
    Hbini hbini{};

    int image{}; // nvg image
    int x,y,w,h{}; // image
    bool is_nacp_valid{};
    std::optional<bool> has_star{std::nullopt};
    std::size_t order{};   // position in the directory scan; used to restore order

    auto GetName() const -> const char* {
        return nacp.lang.name;
    }

    auto GetAuthor() const -> const char* {
        return nacp.lang.author;
    }
};


/**
 * Scans a folder for all nro's.
 *
 * \param path path to the folder that is to be scanned.
 * \param nros output of all scanned nros.
 * \param nested if true, it will scan any folders for nros, ie path/folder/game.nro.
 * \param scan_all_dir if true, when a folder is found, it will scan the entire
 *                     folder for all nros, rather than stopping at the first
 *                     nro found.
 *                     this does nothing if nested=false.
 */
auto nro_scan(const fs::FsPath& path, std::vector<NroEntry>& nros, bool nested = true, bool scan_all_dir = true) -> Result;

auto nro_get_icon(const fs::FsPath& path, u64 size, u64 offset) -> std::vector<u8>;
auto nro_get_icon(const fs::FsPath& path) -> std::vector<u8>;

// path is pre-appended to args, such that argv[0] == path
auto nro_launch(std::string path, std::string args = {}) -> Result;

// if the arg contains a space, it will wrap it in quotes
auto nro_add_arg(std::string arg) -> std::string;

} // namespace morph
