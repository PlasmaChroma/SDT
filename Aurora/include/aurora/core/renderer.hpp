#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "aurora/lang/ast.hpp"

namespace aurora::core {

struct RenderOptions {
  uint64_t seed = 0;
  int sample_rate_override = 0;
  std::function<void(double)> progress_callback;
};

struct AudioStem {
  std::string name;
  int channels = 1;
  std::vector<float> samples;
};

struct MidiNote {
  int channel = 0;
  int note = 60;
  uint8_t velocity = 100;
  uint64_t start_sample = 0;
  uint64_t end_sample = 0;
};

struct MidiCCPoint {
  int channel = 0;
  int cc = 74;
  uint64_t sample = 0;
  uint8_t value = 0;
};

struct MidiTrackData {
  std::string name;
  std::vector<MidiNote> notes;
  std::vector<MidiCCPoint> ccs;
};

struct RenderMetadata {
  int sample_rate = 48000;
  int block_size = 256;
  uint64_t total_samples = 0;
  double duration_seconds = 0.0;
};

struct RenderResult {
  std::vector<AudioStem> patch_stems;
  std::vector<AudioStem> bus_stems;
  AudioStem master;
  std::vector<MidiTrackData> midi_tracks;
  RenderMetadata metadata;
  std::vector<std::string> warnings;
};

class Renderer {
 public:
  RenderResult Render(const aurora::lang::AuroraFile& file, const RenderOptions& options) const;
};

}  // namespace aurora::core
