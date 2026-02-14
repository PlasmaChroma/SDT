#include "aurora/io/json_writer.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

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

struct StemStats {
  uint64_t frame_count = 0;
  uint64_t sample_count = 0;
  double peak = 0.0;
  double rms = 0.0;
};

StemStats ComputeStemStats(const aurora::core::AudioStem& stem) {
  StemStats stats;
  stats.sample_count = static_cast<uint64_t>(stem.samples.size());
  if (stem.channels > 0) {
    stats.frame_count = static_cast<uint64_t>(stem.samples.size() / static_cast<size_t>(stem.channels));
  }
  if (stem.samples.empty()) {
    return stats;
  }

  double sum_sq = 0.0;
  double peak = 0.0;
  for (const float sample : stem.samples) {
    const double v = static_cast<double>(sample);
    const double a = std::fabs(v);
    if (a > peak) {
      peak = a;
    }
    sum_sq += v * v;
  }
  stats.peak = peak;
  stats.rms = std::sqrt(sum_sq / static_cast<double>(stem.samples.size()));
  return stats;
}

void WriteStemDetailArray(std::ofstream& out, const std::string& key, const std::vector<aurora::core::AudioStem>& stems) {
  out << "  \"" << key << "\": [\n";
  for (size_t i = 0; i < stems.size(); ++i) {
    const auto& stem = stems[i];
    const StemStats stats = ComputeStemStats(stem);
    out << "    {\n";
    out << "      \"name\": \"" << EscapeJson(stem.name) << "\",\n";
    out << "      \"channels\": " << stem.channels << ",\n";
    out << "      \"frame_count\": " << stats.frame_count << ",\n";
    out << "      \"sample_count\": " << stats.sample_count << ",\n";
    out << "      \"peak\": " << stats.peak << ",\n";
    out << "      \"rms\": " << stats.rms << "\n";
    out << "    }";
    if (i + 1 < stems.size()) {
      out << ",";
    }
    out << "\n";
  }
  out << "  ]";
}

void WriteStemDetailObject(std::ofstream& out, const std::string& key, const aurora::core::AudioStem& stem) {
  const StemStats stats = ComputeStemStats(stem);
  out << "  \"" << key << "\": {\n";
  out << "    \"name\": \"" << EscapeJson(stem.name) << "\",\n";
  out << "    \"channels\": " << stem.channels << ",\n";
  out << "    \"frame_count\": " << stats.frame_count << ",\n";
  out << "    \"sample_count\": " << stats.sample_count << ",\n";
  out << "    \"peak\": " << stats.peak << ",\n";
  out << "    \"rms\": " << stats.rms << "\n";
  out << "  }";
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
  out << std::setprecision(9);
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
  WriteStemDetailArray(out, "patch_stem_details", result.patch_stems);
  out << ",\n";
  WriteStemDetailArray(out, "bus_stem_details", result.bus_stems);
  out << ",\n";
  WriteStemDetailObject(out, "master_stem", result.master);
  out << ",\n";
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
