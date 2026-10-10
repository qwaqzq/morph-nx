#include "keys.hpp"
#include "defines.hpp"
#include "log.hpp"

#include <minIni.h>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace morph::keys {
namespace {

constexpr u8 HEADER_KEK_SRC[0x10] = {
    0x1F, 0x12, 0x91, 0x3A, 0x4A, 0xCB, 0xF0, 0x0D, 0x4C, 0xDE, 0x3A, 0xF6, 0xD5, 0x23, 0x88, 0x2A,
};

constexpr u8 HEADER_KEY_SRC[0x20] = {
    0x5A, 0x3E, 0xD8, 0x4F, 0xDE, 0xC0, 0xD8, 0x26, 0x31, 0xF7, 0xE2, 0x5D, 0x19, 0x7B, 0xF5, 0xD0,
    0x1C, 0x9B, 0x7B, 0xFA, 0xF6, 0x28, 0x18, 0x3D, 0x71, 0xF6, 0x4D, 0x73, 0xF1, 0x50, 0xB9, 0xD2,
};

struct SplCryptoGuard {
    ~SplCryptoGuard() { splCryptoExit(); }
};

struct SetCalGuard {
    ~SetCalGuard() { setcalExit(); }
};

void ParseHexKey(void* key, const char* hex) {
    char low[0x11]{};
    char upp[0x11]{};
    std::memcpy(low, hex, 0x10);
    std::memcpy(upp, hex + 0x10, 0x10);
    *(u64*)key = std::byteswap(std::strtoul(low, nullptr, 0x10));
    *(u64*)((u8*)key + 8) = std::byteswap(std::strtoul(upp, nullptr, 0x10));
}

bool FindKeyIndexed(const char* key, const char* value, const char* prefix, KeySection& section) {
    const auto len = std::strlen(prefix);
    if (std::strncmp(key, prefix, len) != 0) {
        return false;
    }
    char* end = nullptr;
    const char* digits = key + len;
    const auto index = std::strtoul(digits, &end, 0x10);
    if (end == nullptr || end == digits || index >= section.size()) {
        return false;
    }
    ParseHexKey(&section[index], value);
    return true;
}

bool FindKeySingle(const char* key, const char* value, const char* name, KeyEntry& entry) {
    if (std::strcmp(key, name) != 0) {
        return false;
    }
    ParseHexKey(&entry, value);
    return true;
}

int IniCallback(const char* /*section*/, const char* key, const char* value, void* user) {
    auto keys = static_cast<Keys*>(user);

    // Sections are not important; every line is matched by its key name.
    if (FindKeyIndexed(key, value, "key_area_key_application_", keys->key_area_key[KeyAreaIndex_Application])) {
        return 1;
    }
    if (FindKeyIndexed(key, value, "key_area_key_ocean_", keys->key_area_key[KeyAreaIndex_Ocean])) {
        return 1;
    }
    if (FindKeyIndexed(key, value, "key_area_key_system_", keys->key_area_key[KeyAreaIndex_System])) {
        return 1;
    }
    if (FindKeyIndexed(key, value, "titlekek_", keys->titlekek)) {
        return 1;
    }
    if (FindKeyIndexed(key, value, "master_key_", keys->master_key)) {
        return 1;
    }

    const char* rsa_kek_name = keys->eticket_device_key.generation ? "eticket_rsa_kek_personalized" : "eticket_rsa_kek";
    if (FindKeySingle(key, value, rsa_kek_name, keys->eticket_rsa_kek)) {
        log_write("keys: found %s\n", key);
    }
    return 1;
}

} // namespace

size_t Keys::CountValid(const KeySection& section) const {
    size_t n = 0;
    for (const auto& e : section) {
        n += e.IsValid() ? 1 : 0;
    }
    return n;
}

Result Load(Keys& out, bool read_from_file) {
    // Header key is derived from constants, it does not need prod.keys.
    R_TRY(splCryptoInitialize());
    SplCryptoGuard spl_guard{};

    u8 header_kek[0x20];
    R_TRY(splCryptoGenerateAesKek(HEADER_KEK_SRC, 0, 0, header_kek));
    R_TRY(splCryptoGenerateAesKey(header_kek, HEADER_KEY_SRC, out.header_key));
    R_TRY(splCryptoGenerateAesKey(header_kek, HEADER_KEY_SRC + 0x10, out.header_key + 0x10));

    if (!read_from_file) {
        R_SUCCEED();
    }

    FILE* f = std::fopen(kProdKeysPath, "rb");
    if (!f) {
        log_write("keys: %s not found\n", kProdKeysPath);
        R_THROW(Result_KeyFileNotFound);
    }
    std::fclose(f);

    R_TRY(setcalInitialize());
    SetCalGuard setcal_guard{};
    R_TRY(setcalGetEticketDeviceKey(&out.eticket_device_key));

    // Failing to parse is not fatal: keys that are missing only break decryption later.
    ini_browse(IniCallback, &out, kProdKeysPath);

    if (out.eticket_rsa_kek.IsValid()) {
        auto rsa = reinterpret_cast<EticketRsaDeviceKey*>(out.eticket_device_key.key);

        Aes128CtrContext ctx{};
        aes128CtrContextCreate(&ctx, &out.eticket_rsa_kek, rsa->ctr);
        aes128CtrCrypt(&ctx, &rsa->private_exponent, &rsa->private_exponent,
                       sizeof(EticketRsaDeviceKey) - sizeof(rsa->ctr));

        const auto public_exponent = std::byteswap(rsa->public_exponent);
        if (public_exponent != 0x10001) {
            log_write("keys: eticket device key decryption failed (0x%X)\n", public_exponent);
            R_THROW(Result_KeyFailedDecryptETicketDeviceKey);
        }
        log_write("keys: eticket device key ok\n");
    }

    log_write("keys: loaded, master=%zu titlekek=%zu area(app=%zu, sys=%zu, ocean=%zu)\n",
              out.CountValid(out.master_key), out.CountValid(out.titlekek),
              out.CountValid(out.key_area_key[KeyAreaIndex_Application]),
              out.CountValid(out.key_area_key[KeyAreaIndex_System]),
              out.CountValid(out.key_area_key[KeyAreaIndex_Ocean]));
    R_SUCCEED();
}

} // namespace morph::keys
