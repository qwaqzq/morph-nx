#pragma once

#include <switch.h>
#include <array>
#include <cstring>

// Loads title/NCA keys from sdmc:/switch/prod.keys.
// Ported from sphaira (source/yati/nx/keys.cpp), GPL-3.0-or-later like the rest of morph.

namespace morph::keys {

constexpr const char* kProdKeysPath = "sdmc:/switch/prod.keys";

struct KeyEntry {
    u8 key[0x10]{};

    bool IsValid() const {
        const KeyEntry empty{};
        return std::memcmp(key, empty.key, sizeof(key)) != 0;
    }
};

using KeySection = std::array<KeyEntry, 0x20>;

// Index into Keys::key_area_key (matches the NCA key-area encryption key order).
enum KeyAreaIndex : u8 {
    KeyAreaIndex_Application = 0x0,
    KeyAreaIndex_Ocean       = 0x1,
    KeyAreaIndex_System      = 0x2,
};

// Layout of the eticket RSA device key as stored by setcal (0x240 bytes).
struct EticketRsaDeviceKey {
    u8 ctr[0x10];
    u8 private_exponent[0x100];
    u8 modulus[0x100];
    u32 public_exponent;   // big endian
    u8 padding[0x14];
    u64 device_id;
    u8 ghash[0x10];
};
static_assert(sizeof(EticketRsaDeviceKey) == 0x240);

struct Keys {
    u8 header_key[0x20]{};
    KeySection key_area_key[3]{};
    KeySection titlekek{};
    KeySection master_key{};
    KeyEntry eticket_rsa_kek{};
    SetCalRsa2048DeviceKey eticket_device_key{};

    // Number of parsed entries per section, for logging and for the UI.
    size_t CountValid(const KeySection& section) const;
};

// Reads prod.keys and fills `out`. `read_from_file == false` only derives the header key.
Result Load(Keys& out, bool read_from_file = true);

} // namespace morph::keys
