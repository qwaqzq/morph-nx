#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace morph::forwarder {

enum class IconAction {
    AlreadyCompatible,  // left untouched
    Converted,          // re-encoded as a 256x256 baseline JPEG
    Failed,             // could not decode; the original bytes are left as they were
};

// HOME decodes a title's icon with a hardware decoder that only accepts baseline JPEG. Many
// NROs carry progressive icons (the All-in-One Switch Updater does): they show fine in DBI, but
// HOME fails to read them and the forwarder refuses to launch. This keeps compatible icons
// as they are and re-encodes the rest as a 256x256 baseline JPEG.
IconAction MakeHomeIcon(std::vector<std::uint8_t>& icon);

// True for a baseline (SOF0/SOF1), 3-component, exactly 256x256 JPEG.
bool IsHomeCompatibleIcon(std::span<const std::uint8_t> jpeg);

} // namespace morph::forwarder
