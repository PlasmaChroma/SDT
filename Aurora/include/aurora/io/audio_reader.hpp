#pragma once

#include <filesystem>
#include <string>

#include "aurora/core/renderer.hpp"

namespace aurora::io {

bool ReadAudioFile(const std::filesystem::path& path, aurora::core::AudioStem* stem, int* sample_rate, std::string* error);

}  // namespace aurora::io
