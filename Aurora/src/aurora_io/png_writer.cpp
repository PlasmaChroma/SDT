#include "aurora/io/png_writer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <zlib.h>

namespace aurora::io {
namespace {

void AppendU32Be(std::vector<uint8_t>* out, uint32_t value) {
  out->push_back(static_cast<uint8_t>((value >> 24) & 0xFFU));
  out->push_back(static_cast<uint8_t>((value >> 16) & 0xFFU));
  out->push_back(static_cast<uint8_t>((value >> 8) & 0xFFU));
  out->push_back(static_cast<uint8_t>(value & 0xFFU));
}

uint32_t Crc32(const uint8_t* bytes, size_t count) {
  static std::array<uint32_t, 256> table = []() {
    std::array<uint32_t, 256> t{};
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int k = 0; k < 8; ++k) {
        c = (c & 1U) != 0U ? (0xEDB88320U ^ (c >> 1U)) : (c >> 1U);
      }
      t[i] = c;
    }
    return t;
  }();

  uint32_t c = 0xFFFFFFFFU;
  for (size_t i = 0; i < count; ++i) {
    c = table[(c ^ static_cast<uint32_t>(bytes[i])) & 0xFFU] ^ (c >> 8U);
  }
  return c ^ 0xFFFFFFFFU;
}

void AppendChunk(std::vector<uint8_t>* png, const char type[4], const std::vector<uint8_t>& data) {
  AppendU32Be(png, static_cast<uint32_t>(data.size()));
  const size_t type_start = png->size();
  png->push_back(static_cast<uint8_t>(type[0]));
  png->push_back(static_cast<uint8_t>(type[1]));
  png->push_back(static_cast<uint8_t>(type[2]));
  png->push_back(static_cast<uint8_t>(type[3]));
  png->insert(png->end(), data.begin(), data.end());
  const uint32_t crc = Crc32(png->data() + type_start, png->size() - type_start);
  AppendU32Be(png, crc);
}

bool BuildZlibDeflate(const std::vector<uint8_t>& raw, int compression_level, std::vector<uint8_t>* out, std::string* error) {
  if (out == nullptr) {
    if (error != nullptr) {
      *error = "Internal PNG error: null zlib output.";
    }
    return false;
  }
  if (compression_level < 0 || compression_level > 9) {
    if (error != nullptr) {
      *error = "PNG compression level must be in [0,9].";
    }
    return false;
  }
  const uLongf bound = compressBound(static_cast<uLong>(raw.size()));
  out->assign(static_cast<size_t>(bound), 0U);
  uLongf actual = bound;
  const int rc = compress2(out->data(), &actual, raw.data(), static_cast<uLong>(raw.size()), compression_level);
  if (rc != Z_OK) {
    if (error != nullptr) {
      *error = "Failed to deflate PNG payload.";
    }
    return false;
  }
  out->resize(static_cast<size_t>(actual));
  return true;
}

}  // namespace

bool WritePngRgb8(const std::filesystem::path& path, int width, int height, const std::vector<uint8_t>& rgb,
                  int compression_level, std::string* error) {
  if (width <= 0 || height <= 0) {
    if (error != nullptr) {
      *error = "Invalid PNG dimensions.";
    }
    return false;
  }
  const size_t expected = static_cast<size_t>(width) * static_cast<size_t>(height) * 3U;
  if (rgb.size() != expected) {
    if (error != nullptr) {
      *error = "PNG RGB buffer size mismatch.";
    }
    return false;
  }

  std::vector<uint8_t> raw;
  raw.reserve(static_cast<size_t>(height) * (1U + static_cast<size_t>(width) * 3U));
  for (int y = 0; y < height; ++y) {
    raw.push_back(0U);
    const size_t row_start = static_cast<size_t>(y) * static_cast<size_t>(width) * 3U;
    raw.insert(raw.end(), rgb.begin() + static_cast<std::ptrdiff_t>(row_start),
               rgb.begin() + static_cast<std::ptrdiff_t>(row_start + static_cast<size_t>(width) * 3U));
  }

  std::vector<uint8_t> png;
  png.reserve(raw.size() + 512U);
  png.insert(png.end(), {137U, 80U, 78U, 71U, 13U, 10U, 26U, 10U});

  std::vector<uint8_t> ihdr;
  ihdr.reserve(13U);
  AppendU32Be(&ihdr, static_cast<uint32_t>(width));
  AppendU32Be(&ihdr, static_cast<uint32_t>(height));
  ihdr.push_back(8U);
  ihdr.push_back(2U);
  ihdr.push_back(0U);
  ihdr.push_back(0U);
  ihdr.push_back(0U);
  AppendChunk(&png, "IHDR", ihdr);

  std::vector<uint8_t> zlib_stream;
  if (!BuildZlibDeflate(raw, compression_level, &zlib_stream, error)) {
    return false;
  }
  AppendChunk(&png, "IDAT", zlib_stream);
  AppendChunk(&png, "IEND", {});

  std::filesystem::create_directories(path.parent_path());
  const std::filesystem::path tmp = path.string() + ".tmp";
  std::ofstream out(tmp, std::ios::binary);
  if (!out.is_open()) {
    if (error != nullptr) {
      *error = "Failed to open PNG for writing: " + tmp.string();
    }
    return false;
  }
  out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
  if (!out.good()) {
    if (error != nullptr) {
      *error = "Failed while writing PNG bytes: " + tmp.string();
    }
    return false;
  }
  out.close();
  if (!out.good()) {
    if (error != nullptr) {
      *error = "Failed closing PNG file: " + tmp.string();
    }
    return false;
  }

  std::error_code ec;
  std::filesystem::rename(tmp, path, ec);
  if (ec) {
    std::filesystem::remove(path, ec);
    ec.clear();
    std::filesystem::rename(tmp, path, ec);
  }
  if (ec) {
    if (error != nullptr) {
      *error = "Failed to finalize PNG file: " + path.string();
    }
    return false;
  }
  return true;
}

bool WritePngIndexed8(const std::filesystem::path& path, int width, int height, const std::vector<uint8_t>& indices,
                      const std::vector<uint8_t>& palette_rgb, int compression_level, std::string* error) {
  if (width <= 0 || height <= 0) {
    if (error != nullptr) {
      *error = "Invalid PNG dimensions.";
    }
    return false;
  }
  const size_t expected = static_cast<size_t>(width) * static_cast<size_t>(height);
  if (indices.size() != expected) {
    if (error != nullptr) {
      *error = "PNG indexed buffer size mismatch.";
    }
    return false;
  }
  if (palette_rgb.size() != 256U * 3U) {
    if (error != nullptr) {
      *error = "Indexed PNG requires 256-color RGB palette.";
    }
    return false;
  }

  std::vector<uint8_t> raw;
  raw.reserve(static_cast<size_t>(height) * (1U + static_cast<size_t>(width)));
  for (int y = 0; y < height; ++y) {
    raw.push_back(0U);
    const size_t row_start = static_cast<size_t>(y) * static_cast<size_t>(width);
    raw.insert(raw.end(), indices.begin() + static_cast<std::ptrdiff_t>(row_start),
               indices.begin() + static_cast<std::ptrdiff_t>(row_start + static_cast<size_t>(width)));
  }

  std::vector<uint8_t> png;
  png.reserve(raw.size() + 1024U);
  png.insert(png.end(), {137U, 80U, 78U, 71U, 13U, 10U, 26U, 10U});

  std::vector<uint8_t> ihdr;
  ihdr.reserve(13U);
  AppendU32Be(&ihdr, static_cast<uint32_t>(width));
  AppendU32Be(&ihdr, static_cast<uint32_t>(height));
  ihdr.push_back(8U);
  ihdr.push_back(3U);
  ihdr.push_back(0U);
  ihdr.push_back(0U);
  ihdr.push_back(0U);
  AppendChunk(&png, "IHDR", ihdr);
  AppendChunk(&png, "PLTE", palette_rgb);

  std::vector<uint8_t> zlib_stream;
  if (!BuildZlibDeflate(raw, compression_level, &zlib_stream, error)) {
    return false;
  }
  AppendChunk(&png, "IDAT", zlib_stream);
  AppendChunk(&png, "IEND", {});

  std::filesystem::create_directories(path.parent_path());
  const std::filesystem::path tmp = path.string() + ".tmp";
  std::ofstream out(tmp, std::ios::binary);
  if (!out.is_open()) {
    if (error != nullptr) {
      *error = "Failed to open PNG for writing: " + tmp.string();
    }
    return false;
  }
  out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
  if (!out.good()) {
    if (error != nullptr) {
      *error = "Failed while writing PNG bytes: " + tmp.string();
    }
    return false;
  }
  out.close();
  if (!out.good()) {
    if (error != nullptr) {
      *error = "Failed closing PNG file: " + tmp.string();
    }
    return false;
  }

  std::error_code ec;
  std::filesystem::rename(tmp, path, ec);
  if (ec) {
    std::filesystem::remove(path, ec);
    ec.clear();
    std::filesystem::rename(tmp, path, ec);
  }
  if (ec) {
    if (error != nullptr) {
      *error = "Failed to finalize PNG file: " + path.string();
    }
    return false;
  }
  return true;
}

}  // namespace aurora::io
