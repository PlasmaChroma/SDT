#pragma once

#include <filesystem>
#include <string>

#include "aurora/core/analyzer.hpp"

namespace aurora::io {

bool WriteAnalysisJson(const std::filesystem::path& path, const aurora::core::AnalysisReport& report, std::string* error);

}  // namespace aurora::io
