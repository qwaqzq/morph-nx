#pragma once

#include <switch.h>

// Minimal NCM layout used by the forwarder builder (from sphaira yati/nx/ncm.hpp).
namespace morph::ncm {

struct ContentStorageRecord {
    NcmContentMetaKey key;
    u8 storage_id; // NcmStorageId
    u8 padding[0x7];
};

} // namespace morph::ncm
