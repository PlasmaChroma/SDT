#pragma once

#include <filesystem>
#include <string>

#include "aurora/core/renderer.hpp"

namespace aurora::io {

bool WriteRenderJson(const std::filesystem::path& path, const aurora::core::RenderResult& result, std::string* error);

}  // namespace aurora::io

