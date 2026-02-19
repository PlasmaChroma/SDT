#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace aurora::io {

bool WritePngRgb8(const std::filesystem::path& path, int width, int height, const std::vector<uint8_t>& rgb,
                  std::string* error);

}  // namespace aurora::io
