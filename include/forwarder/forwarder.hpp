#pragma once

#include <switch.h>
#include <string>
#include <vector>
#include <span>

namespace morph::forwarder {

enum class CpuCoreMode : u8 {
    Zero = 0,
    One = 1,
    Two = 2,
    Three = 3,
    Four = 4,
};

enum class AddressSpace : u8 {
    Bit32 = 0,
    Bit36 = 1,
    Bit32NoAlias = 2,
    Bit39 = 3,
};

enum class SvcDebugMode : u8 {
    Automatic,
    Enabled,
    Disabled,
};

// Everything the builder needs to describe one forwarder title.
struct Config {
    std::string nro_path;          // e.g. "sdmc:/switch/foo/foo.nro"
    std::string args;              // extra argv, nro_path is prepended by the builder
    std::string name;
    std::string author;
    std::vector<u8> icon;          // JPEG
    std::vector<u8> nacp;          // the NRO's own 0x4000-byte NACP; empty = generate one
    AddressSpace address_space{AddressSpace::Bit36};
    SvcDebugMode svc_debug_mode{SvcDebugMode::Automatic};
};

struct BuildResult {
    u64 tid{};
    std::string dir;   // folder with program.nca / control.nca / meta.nca and the record files
};

// Builds the three NCAs and the CNMT record for a forwarder into `out_root/<tid>/`.
// Nothing is installed: the output is only written to the SD card. Needs the header key
// (keys::Load with read_from_file=false), so it only works on a console.
Result BuildFiles(const Config& config, const std::string& out_root, BuildResult* out);

// Installs what BuildFiles() wrote into `dir` (the folder that contains program.nca etc.).
// Writes to the NCM content storage chosen by storage_id and registers the application.
Result InstallBuilt(const std::string& dir, u64 tid, NcmStorageId storage_id);

} // namespace morph::forwarder
