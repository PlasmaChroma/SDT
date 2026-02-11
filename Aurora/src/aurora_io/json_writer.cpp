#include "aurora/io/json_writer.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace aurora::io {
namespace {

std::string EscapeJson(const std::string& in) {
  std::ostringstream out;
  for (const char ch : in) {
    switch (ch) {
      case '\"':
        out << "\\\"";
        break;
      case '\\':
        out << "\\\\";
        break;
      case '\n':
        out << "\\n";
        break;
      case '\r':
        out << "\\r";
        break;
      case '\t':
        out << "\\t";
        break;
      default:
        out << ch;
        break;
    }
  }
  return out.str();
}

}  // namespace

bool WriteRenderJson(const std::filesystem::path& path, const aurora::core::RenderResult& result, std::string* error) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path);
  if (!out.is_open()) {
    if (error != nullptr) {
      *error = "Failed to open JSON file for writing: " + path.string();
    }
    return false;
  }

  out << "{\n";
  out << "  \"sample_rate\": " << result.metadata.sample_rate << ",\n";
  out << "  \"block_size\": " << result.metadata.block_size << ",\n";
  out << "  \"total_samples\": " << result.metadata.total_samples << ",\n";
  out << "  \"duration_seconds\": " << result.metadata.duration_seconds << ",\n";
  out << "  \"patch_stems\": [\n";
  for (size_t i = 0; i < result.patch_stems.size(); ++i) {
    out << "    \"" << EscapeJson(result.patch_stems[i].name) << "\"";
    if (i + 1 < result.patch_stems.size()) {
      out << ",";
    }
    out << "\n";
  }
  out << "  ],\n";
  out << "  \"bus_stems\": [\n";
  for (size_t i = 0; i < result.bus_stems.size(); ++i) {
    out << "    \"" << EscapeJson(result.bus_stems[i].name) << "\"";
    if (i + 1 < result.bus_stems.size()) {
      out << ",";
    }
    out << "\n";
  }
  out << "  ],\n";
  out << "  \"midi_tracks\": [\n";
  for (size_t i = 0; i < result.midi_tracks.size(); ++i) {
    out << "    \"" << EscapeJson(result.midi_tracks[i].name) << "\"";
    if (i + 1 < result.midi_tracks.size()) {
      out << ",";
    }
    out << "\n";
  }
  out << "  ],\n";
  out << "  \"warnings\": [\n";
  for (size_t i = 0; i < result.warnings.size(); ++i) {
    out << "    \"" << EscapeJson(result.warnings[i]) << "\"";
    if (i + 1 < result.warnings.size()) {
      out << ",";
    }
    out << "\n";
  }
  out << "  ]\n";
  out << "}\n";

  if (!out.good()) {
    if (error != nullptr) {
      *error = "Failed while writing JSON metadata: " + path.string();
    }
    return false;
  }
  return true;
}

}  // namespace aurora::io

