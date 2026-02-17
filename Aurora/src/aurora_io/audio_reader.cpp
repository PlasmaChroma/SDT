#include "aurora/io/audio_reader.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace aurora::io {
namespace {

uint16_t ReadU16Le(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8)); }

uint32_t ReadU32Le(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

int32_t ReadS24Le(const uint8_t* p) {
  int32_t value = static_cast<int32_t>(p[0]) | (static_cast<int32_t>(p[1]) << 8) | (static_cast<int32_t>(p[2]) << 16);
  if ((value & 0x00800000) != 0) {
    value |= 0xFF000000;
  }
  return value;
}

bool HasAudioExtension(const std::filesystem::path& path, const std::string& ext) {
  std::string e = path.extension().string();
  std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return e == ext;
}

bool ReadWavPcmOrFloat(const std::filesystem::path& path, aurora::core::AudioStem* stem, int* sample_rate, std::string* error) {
  std::ifstream in(path, std::ios::binary);
  if (!in.is_open()) {
    if (error != nullptr) {
      *error = "Failed to open WAV file: " + path.string();
    }
    return false;
  }

  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (bytes.size() < 44) {
    if (error != nullptr) {
      *error = "WAV file too small: " + path.string();
    }
    return false;
  }
  if (std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
    if (error != nullptr) {
      *error = "Not a RIFF/WAVE file: " + path.string();
    }
    return false;
  }

  uint16_t audio_format = 0;
  uint16_t channels = 0;
  uint32_t sr = 0;
  uint16_t bits_per_sample = 0;
  const uint8_t* data_ptr = nullptr;
  uint32_t data_size = 0;

  size_t cursor = 12;
  while (cursor + 8 <= bytes.size()) {
    const char* chunk_id = reinterpret_cast<const char*>(bytes.data() + cursor);
    const uint32_t chunk_size = ReadU32Le(bytes.data() + cursor + 4);
    const size_t chunk_data = cursor + 8;
    if (chunk_data + chunk_size > bytes.size()) {
      break;
    }
    if (std::memcmp(chunk_id, "fmt ", 4) == 0 && chunk_size >= 16) {
      audio_format = ReadU16Le(bytes.data() + chunk_data);
      channels = ReadU16Le(bytes.data() + chunk_data + 2);
      sr = ReadU32Le(bytes.data() + chunk_data + 4);
      bits_per_sample = ReadU16Le(bytes.data() + chunk_data + 14);
    } else if (std::memcmp(chunk_id, "data", 4) == 0) {
      data_ptr = bytes.data() + chunk_data;
      data_size = chunk_size;
    }
    cursor = chunk_data + chunk_size + (chunk_size % 2U);
  }

  if (data_ptr == nullptr || channels == 0 || sr == 0 || bits_per_sample == 0) {
    if (error != nullptr) {
      *error = "Malformed WAV file: missing required chunks in " + path.string();
    }
    return false;
  }

  if (channels > 2) {
    if (error != nullptr) {
      *error = "Only mono/stereo WAV files are supported: " + path.string();
    }
    return false;
  }

  const uint32_t bytes_per_sample = bits_per_sample / 8;
  if (bytes_per_sample == 0) {
    if (error != nullptr) {
      *error = "Unsupported WAV bit depth in " + path.string();
    }
    return false;
  }

  const uint32_t bytes_per_frame = bytes_per_sample * channels;
  if (bytes_per_frame == 0 || (data_size % bytes_per_frame) != 0U) {
    if (error != nullptr) {
      *error = "WAV data is not frame-aligned: " + path.string();
    }
    return false;
  }

  const size_t sample_count = static_cast<size_t>(data_size / bytes_per_sample);
  stem->samples.assign(sample_count, 0.0F);
  stem->channels = static_cast<int>(channels);
  stem->name = path.stem().string();
  *sample_rate = static_cast<int>(sr);

  for (size_t i = 0; i < sample_count; ++i) {
    const uint8_t* p = data_ptr + i * bytes_per_sample;
    float value = 0.0F;
    if (audio_format == 1) {
      if (bits_per_sample == 16) {
        const int16_t s = static_cast<int16_t>(ReadU16Le(p));
        value = static_cast<float>(static_cast<double>(s) / 32768.0);
      } else if (bits_per_sample == 24) {
        const int32_t s = ReadS24Le(p);
        value = static_cast<float>(static_cast<double>(s) / 8388608.0);
      } else if (bits_per_sample == 32) {
        const int32_t s = static_cast<int32_t>(ReadU32Le(p));
        value = static_cast<float>(static_cast<double>(s) / 2147483648.0);
      } else {
        if (error != nullptr) {
          *error = "Unsupported PCM bit depth in WAV: " + std::to_string(bits_per_sample);
        }
        return false;
      }
    } else if (audio_format == 3) {
      if (bits_per_sample != 32) {
        if (error != nullptr) {
          *error = "Unsupported float WAV bit depth: " + std::to_string(bits_per_sample);
        }
        return false;
      }
      float s = 0.0F;
      std::memcpy(&s, p, sizeof(float));
      value = s;
    } else {
      if (error != nullptr) {
        *error = "Unsupported WAV format code: " + std::to_string(audio_format);
      }
      return false;
    }
    stem->samples[i] = value;
  }

  return true;
}

std::string EscapeSingleQuotes(const std::string& value) {
  std::string out;
  out.reserve(value.size() + 8U);
  for (char c : value) {
    if (c == '\'') {
      out += "'\\''";
    } else {
      out.push_back(c);
    }
  }
  return out;
}

bool ReadFlacViaFfmpeg(const std::filesystem::path& path, aurora::core::AudioStem* stem, int* sample_rate, std::string* error) {
  const std::filesystem::path tmp_wav = std::filesystem::temp_directory_path() /
                                        ("aurora_flac_decode_" + std::to_string(std::rand()) + ".wav");

  const std::string input_escaped = EscapeSingleQuotes(path.string());
  const std::string tmp_escaped = EscapeSingleQuotes(tmp_wav.string());
  const std::string command =
      "ffmpeg -v error -y -i '" + input_escaped + "' -f wav '" + tmp_escaped + "' >/dev/null 2>&1";
  const int code = std::system(command.c_str());
  if (code != 0) {
    if (error != nullptr) {
      *error = "Failed to decode FLAC file (ffmpeg returned non-zero): " + path.string();
    }
    return false;
  }

  std::string wav_error;
  const bool ok = ReadWavPcmOrFloat(tmp_wav, stem, sample_rate, &wav_error);
  std::error_code ec;
  std::filesystem::remove(tmp_wav, ec);
  if (!ok) {
    if (error != nullptr) {
      *error = "Failed to parse ffmpeg-decoded FLAC WAV: " + wav_error;
    }
    return false;
  }
  stem->name = path.stem().string();
  return true;
}

}  // namespace

bool ReadAudioFile(const std::filesystem::path& path, aurora::core::AudioStem* stem, int* sample_rate, std::string* error) {
  if (stem == nullptr || sample_rate == nullptr) {
    if (error != nullptr) {
      *error = "Invalid audio reader arguments.";
    }
    return false;
  }

  if (HasAudioExtension(path, ".wav")) {
    return ReadWavPcmOrFloat(path, stem, sample_rate, error);
  }

  if (HasAudioExtension(path, ".flac") || HasAudioExtension(path, ".mp3") || HasAudioExtension(path, ".aiff") ||
      HasAudioExtension(path, ".aif")) {
    return ReadFlacViaFfmpeg(path, stem, sample_rate, error);
  }

  if (error != nullptr) {
    *error = "Unsupported audio file extension: " + path.string();
  }
  return false;
}

}  // namespace aurora::io
