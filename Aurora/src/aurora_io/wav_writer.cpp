#include "aurora/io/wav_writer.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>

namespace aurora::io {
namespace {

void WriteU16(std::ofstream& out, uint16_t v) {
  out.put(static_cast<char>(v & 0xFF));
  out.put(static_cast<char>((v >> 8) & 0xFF));
}

void WriteU32(std::ofstream& out, uint32_t v) {
  out.put(static_cast<char>(v & 0xFF));
  out.put(static_cast<char>((v >> 8) & 0xFF));
  out.put(static_cast<char>((v >> 16) & 0xFF));
  out.put(static_cast<char>((v >> 24) & 0xFF));
}

}  // namespace

bool WriteWavFloat32(const std::filesystem::path& path, const aurora::core::AudioStem& stem, int sample_rate,
                     std::string* error) {
  if (stem.channels < 1 || stem.channels > 2) {
    if (error != nullptr) {
      *error = "Only mono/stereo stems are supported.";
    }
    return false;
  }
  if (stem.samples.empty()) {
    if (error != nullptr) {
      *error = "Stem has no samples.";
    }
    return false;
  }

  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary);
  if (!out.is_open()) {
    if (error != nullptr) {
      *error = "Failed to open WAV file for writing: " + path.string();
    }
    return false;
  }

  const uint16_t num_channels = static_cast<uint16_t>(stem.channels);
  const uint32_t num_frames = static_cast<uint32_t>(stem.samples.size() / static_cast<size_t>(stem.channels));
  const uint16_t bits_per_sample = 32;
  const uint16_t bytes_per_sample = bits_per_sample / 8;
  const uint32_t byte_rate = static_cast<uint32_t>(sample_rate) * num_channels * bytes_per_sample;
  const uint16_t block_align = static_cast<uint16_t>(num_channels * bytes_per_sample);
  const uint32_t data_bytes = num_frames * block_align;
  const uint32_t riff_size = 4 + (8 + 16) + (8 + data_bytes);

  out.write("RIFF", 4);
  WriteU32(out, riff_size);
  out.write("WAVE", 4);

  out.write("fmt ", 4);
  WriteU32(out, 16);
  WriteU16(out, 3);  // IEEE float
  WriteU16(out, num_channels);
  WriteU32(out, static_cast<uint32_t>(sample_rate));
  WriteU32(out, byte_rate);
  WriteU16(out, block_align);
  WriteU16(out, bits_per_sample);

  out.write("data", 4);
  WriteU32(out, data_bytes);
  out.write(reinterpret_cast<const char*>(stem.samples.data()), static_cast<std::streamsize>(data_bytes));

  if (!out.good()) {
    if (error != nullptr) {
      *error = "Failed while writing WAV data: " + path.string();
    }
    return false;
  }
  return true;
}

}  // namespace aurora::io

