#include "forwarder/icon.hpp"

// stb_image's implementation lives in source/image.cpp; only its declarations are needed here.
// Writing and resizing are used only by this file, so their implementations are compiled here.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Warray-bounds="
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#define STBI_WRITE_NO_STDIO
#include <stb_image_write.h>
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_STATIC
#include <stb_image_resize2.h>
#pragma GCC diagnostic pop

#include <cstring>

namespace morph::forwarder {
namespace {

constexpr int kIconSize = 256;
constexpr std::size_t kMaxIconBytes = 0x20000; // control NCA icon size limit

struct JpegInfo {
    int marker{};      // SOFn marker byte (0xC0 baseline, 0xC2 progressive, ...)
    int width{};
    int height{};
    int components{};
};

bool ParseJpeg(std::span<const std::uint8_t> d, JpegInfo& out) {
    if (d.size() < 4 || d[0] != 0xFF || d[1] != 0xD8) return false;

    std::size_t i = 2;
    while (i + 4 <= d.size()) {
        if (d[i] != 0xFF) { i++; continue; }
        const int m = d[i + 1];
        if (m == 0xFF) { i++; continue; }                       // padding
        if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7)) { i += 2; continue; }
        const std::size_t len = (std::size_t(d[i + 2]) << 8) | d[i + 3];
        if (len < 2) return false;
        if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) {
            if (i + 10 > d.size()) return false;
            out.marker = m;
            out.height = (d[i + 5] << 8) | d[i + 6];
            out.width = (d[i + 7] << 8) | d[i + 8];
            out.components = d[i + 9];
            return true;
        }
        if (m == 0xDA) return false;                            // reached the scan before any SOF
        i += 2 + len;
    }
    return false;
}

} // namespace

bool IsHomeCompatibleIcon(std::span<const std::uint8_t> jpeg) {
    JpegInfo info;
    if (!ParseJpeg(jpeg, info)) return false;
    const bool baseline = info.marker == 0xC0 || info.marker == 0xC1;
    return baseline && info.components == 3 && info.width == kIconSize && info.height == kIconSize &&
           jpeg.size() <= kMaxIconBytes;
}

IconAction MakeHomeIcon(std::vector<std::uint8_t>& icon) {
    if (IsHomeCompatibleIcon(icon)) return IconAction::AlreadyCompatible;

    int w = 0, h = 0, n = 0;
    stbi_uc* pixels = stbi_load_from_memory(icon.data(), (int)icon.size(), &w, &h, &n, 3);
    if (!pixels) return IconAction::Failed;

    std::vector<std::uint8_t> rgb(pixels, pixels + std::size_t(w) * h * 3);
    stbi_image_free(pixels);

    if (w != kIconSize || h != kIconSize) {
        std::vector<std::uint8_t> resized(std::size_t(kIconSize) * kIconSize * 3);
        if (!stbir_resize_uint8_linear(rgb.data(), w, h, w * 3, resized.data(), kIconSize, kIconSize,
                                       kIconSize * 3, STBIR_RGB)) {
            return IconAction::Failed;
        }
        rgb.swap(resized);
    }

    const auto write_cb = [](void* context, void* data, int size) {
        auto buf = static_cast<std::vector<std::uint8_t>*>(context);
        const auto offset = buf->size();
        buf->resize(offset + size);
        std::memcpy(buf->data() + offset, data, size);
    };

    // stb writes baseline JPEG. Lower the quality only if the result would exceed the size limit.
    for (const int quality : {93, 85, 70, 50}) {
        std::vector<std::uint8_t> out;
        out.reserve(64 * 1024);
        if (!stbi_write_jpg_to_func(write_cb, &out, kIconSize, kIconSize, 3, rgb.data(), quality)) {
            return IconAction::Failed;
        }
        if (out.size() <= kMaxIconBytes) {
            icon.swap(out);
            return IconAction::Converted;
        }
    }
    return IconAction::Failed;
}

} // namespace morph::forwarder
