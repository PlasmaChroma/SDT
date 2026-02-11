#pragma once

#include <filesystem>
#include <string>

#include "aurora/core/renderer.hpp"

namespace aurora::io {

bool WriteWavFloat32(const std::filesystem::path& path, const aurora::core::AudioStem& stem, int sample_rate,
                     std::string* error);

}  // namespace aurora::io

