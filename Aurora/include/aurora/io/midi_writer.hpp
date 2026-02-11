#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "aurora/core/renderer.hpp"
#include "aurora/core/timebase.hpp"

namespace aurora::io {

bool WriteMidiFormat1(const std::filesystem::path& path, const std::vector<aurora::core::MidiTrackData>& tracks,
                      const aurora::core::TempoMap& tempo_map, uint64_t total_samples, int sample_rate,
                      std::string* error);

}  // namespace aurora::io

