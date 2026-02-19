#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <map>
#include <cctype>
#include <cmath>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "aurora/core/analyzer.hpp"
#include "aurora/core/renderer.hpp"
#include "aurora/core/spectrogram.hpp"
#include "aurora/core/timebase.hpp"
#include "aurora/io/analysis_writer.hpp"
#include "aurora/io/audio_reader.hpp"
#include "aurora/io/json_writer.hpp"
#include "aurora/io/midi_writer.hpp"
#include "aurora/io/png_writer.hpp"
#include "aurora/io/wav_writer.hpp"
#include "aurora/lang/parser.hpp"
#include "aurora/lang/validation.hpp"

namespace {

struct RenderCliOptions {
  uint64_t seed = 0;
  int sample_rate = 0;
  std::optional<std::filesystem::path> out_root;
  bool analyze = false;
  std::optional<std::filesystem::path> analysis_out;
  int analyze_threads = 0;
  std::string intent;
  bool spectrogram = true;
  std::optional<std::filesystem::path> spectrogram_out;
  std::optional<std::string> spectrogram_config_json;
};

struct AnalyzeCliOptions {
  std::vector<std::filesystem::path> positional;
  bool stems_mode = false;
  std::optional<std::filesystem::path> mix_file;
  std::optional<std::filesystem::path> out_path;
  int analyze_threads = 0;
  std::string intent;
  bool spectrogram = true;
  std::optional<std::filesystem::path> spectrogram_out;
  std::optional<std::string> spectrogram_config_json;
};

void PrintUsage() {
  std::cerr << "Usage:\n";
  std::cerr << "  aurora render <file.au> [--seed N] [--sr 44100|48000|96000] [--out <dir>] [--analyze]";
  std::cerr << " [--analysis-out <path>] [--analyze-threads N] [--intent sleep|ritual|dub]";
  std::cerr << " [--nospectrogram] [--spectrogram-out <dir>] [--spectrogram-config <json>]\n";
  std::cerr << "  aurora analyze <input.wav|input.flac|input.mp3|input.aiff> [--out <analysis.json>] [--analyze-threads N]";
  std::cerr << " [--intent sleep|ritual|dub] [--nospectrogram] [--spectrogram-out <dir>] [--spectrogram-config <json>]\n";
  std::cerr << "  aurora analyze --stems <stem1.wav> <stem2.wav> ... [--mix <mix.wav>] [--out <analysis.json>]";
  std::cerr << " [--analyze-threads N] [--intent sleep|ritual|dub]";
  std::cerr << " [--nospectrogram] [--spectrogram-out <dir>] [--spectrogram-config <json>]\n";
}

bool ParseRenderArgs(int argc, char** argv, std::filesystem::path* file, RenderCliOptions* options, std::string* error) {
  if (argc < 3) {
    *error = "Missing .au file path.";
    return false;
  }
  *file = std::filesystem::path(argv[2]);
  for (int i = 3; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--seed") {
      if (i + 1 >= argc) {
        *error = "Expected value after --seed";
        return false;
      }
      const std::string value = argv[++i];
      try {
        options->seed = static_cast<uint64_t>(std::stoull(value));
      } catch (const std::exception&) {
        *error = "Invalid --seed value: " + value;
        return false;
      }
      continue;
    }
    if (arg == "--sr") {
      if (i + 1 >= argc) {
        *error = "Expected value after --sr";
        return false;
      }
      const std::string value = argv[++i];
      try {
        options->sample_rate = std::stoi(value);
      } catch (const std::exception&) {
        *error = "Invalid --sr value: " + value;
        return false;
      }
      if (options->sample_rate != 44100 && options->sample_rate != 48000 && options->sample_rate != 96000) {
        *error = "Unsupported --sr value: " + value + " (expected 44100, 48000, or 96000)";
        return false;
      }
      continue;
    }
    if (arg == "--out") {
      if (i + 1 >= argc) {
        *error = "Expected value after --out";
        return false;
      }
      options->out_root = std::filesystem::path(argv[++i]);
      continue;
    }
    if (arg == "--analyze") {
      options->analyze = true;
      continue;
    }
    if (arg == "--analysis-out") {
      if (i + 1 >= argc) {
        *error = "Expected value after --analysis-out";
        return false;
      }
      options->analysis_out = std::filesystem::path(argv[++i]);
      options->analyze = true;
      continue;
    }
    if (arg == "--intent") {
      if (i + 1 >= argc) {
        *error = "Expected value after --intent";
        return false;
      }
      options->intent = argv[++i];
      options->analyze = true;
      continue;
    }
    if (arg == "--analyze-threads") {
      if (i + 1 >= argc) {
        *error = "Expected value after --analyze-threads";
        return false;
      }
      const std::string value = argv[++i];
      try {
        options->analyze_threads = std::stoi(value);
      } catch (const std::exception&) {
        *error = "Invalid --analyze-threads value: " + value;
        return false;
      }
      if (options->analyze_threads < 1) {
        *error = "--analyze-threads must be >= 1.";
        return false;
      }
      options->analyze = true;
      continue;
    }
    if (arg == "--nospectrogram") {
      options->spectrogram = false;
      options->analyze = true;
      continue;
    }
    if (arg == "--spectrogram-out") {
      if (i + 1 >= argc) {
        *error = "Expected value after --spectrogram-out";
        return false;
      }
      options->spectrogram_out = std::filesystem::path(argv[++i]);
      options->analyze = true;
      continue;
    }
    if (arg == "--spectrogram-config") {
      if (i + 1 >= argc) {
        *error = "Expected value after --spectrogram-config";
        return false;
      }
      options->spectrogram_config_json = std::string(argv[++i]);
      options->analyze = true;
      continue;
    }
    *error = "Unknown argument: " + arg;
    return false;
  }
  return true;
}

bool ParseAnalyzeArgs(int argc, char** argv, AnalyzeCliOptions* options, std::string* error) {
  if (argc < 3) {
    *error = "Missing audio input path.";
    return false;
  }
  for (int i = 2; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--stems") {
      options->stems_mode = true;
      continue;
    }
    if (arg == "--mix") {
      if (i + 1 >= argc) {
        *error = "Expected value after --mix";
        return false;
      }
      options->mix_file = std::filesystem::path(argv[++i]);
      continue;
    }
    if (arg == "--out") {
      if (i + 1 >= argc) {
        *error = "Expected value after --out";
        return false;
      }
      options->out_path = std::filesystem::path(argv[++i]);
      continue;
    }
    if (arg == "--intent") {
      if (i + 1 >= argc) {
        *error = "Expected value after --intent";
        return false;
      }
      options->intent = argv[++i];
      continue;
    }
    if (arg == "--analyze-threads") {
      if (i + 1 >= argc) {
        *error = "Expected value after --analyze-threads";
        return false;
      }
      const std::string value = argv[++i];
      try {
        options->analyze_threads = std::stoi(value);
      } catch (const std::exception&) {
        *error = "Invalid --analyze-threads value: " + value;
        return false;
      }
      if (options->analyze_threads < 1) {
        *error = "--analyze-threads must be >= 1.";
        return false;
      }
      continue;
    }
    if (arg == "--nospectrogram") {
      options->spectrogram = false;
      continue;
    }
    if (arg == "--spectrogram-out") {
      if (i + 1 >= argc) {
        *error = "Expected value after --spectrogram-out";
        return false;
      }
      options->spectrogram_out = std::filesystem::path(argv[++i]);
      continue;
    }
    if (arg == "--spectrogram-config") {
      if (i + 1 >= argc) {
        *error = "Expected value after --spectrogram-config";
        return false;
      }
      options->spectrogram_config_json = std::string(argv[++i]);
      continue;
    }
    if (!arg.empty() && arg[0] == '-') {
      *error = "Unknown argument: " + arg;
      return false;
    }
    options->positional.push_back(std::filesystem::path(arg));
  }

  if (!options->stems_mode) {
    if (options->positional.size() != 1U) {
      *error = "analyze expects a single input file unless --stems is used.";
      return false;
    }
    return true;
  }

  if (options->positional.empty() && !options->mix_file.has_value()) {
    *error = "--stems mode requires one or more audio file paths.";
    return false;
  }
  return true;
}

bool ReadFile(const std::filesystem::path& path, std::string* contents, std::string* error) {
  std::ifstream in(path, std::ios::binary);
  if (!in.is_open()) {
    if (error != nullptr) {
      *error = "Failed to open .au file: " + path.string();
    }
    return false;
  }
  *contents = std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (!in.good() && !in.eof()) {
    if (error != nullptr) {
      *error = "Failed while reading .au file: " + path.string();
    }
    return false;
  }
  return true;
}

std::string JoinCycle(const std::vector<std::string>& stack, const std::string& back_to) {
  std::ostringstream out;
  bool started = false;
  for (const auto& item : stack) {
    if (!started && item != back_to) {
      continue;
    }
    if (!started) {
      started = true;
    } else {
      out << " -> ";
    }
    out << item;
  }
  if (started) {
    out << " -> " << back_to;
  } else {
    out << back_to;
  }
  return out.str();
}

std::string BasePatchName(const std::string& patch_name) {
  const size_t last_dot = patch_name.find_last_of('.');
  if (last_dot == std::string::npos) {
    return patch_name;
  }
  return patch_name.substr(last_dot + 1);
}

std::filesystem::path ResolveImportPath(const std::filesystem::path& importer_file, const std::string& source) {
  const std::filesystem::path raw(source);
  const std::filesystem::path joined = raw.is_absolute() ? raw : (importer_file.parent_path() / raw);
  std::error_code ec;
  const std::filesystem::path canonical = std::filesystem::weakly_canonical(joined, ec);
  if (!ec) {
    return canonical;
  }
  return joined.lexically_normal();
}

bool ResolveImportsRecursive(const std::filesystem::path& file_path, aurora::lang::AuroraFile* file,
                            std::vector<std::string>* import_stack, std::string* error) {
  if (file == nullptr || import_stack == nullptr) {
    if (error != nullptr) {
      *error = "Internal error: null import resolver state.";
    }
    return false;
  }

  std::set<std::string> alias_names;
  std::set<std::string> local_symbol_names;
  for (const auto& patch : file->patches) {
    local_symbol_names.insert(patch.name);
  }
  for (const auto& bus : file->buses) {
    local_symbol_names.insert(bus.name);
  }

  std::vector<aurora::lang::PatchDefinition> imported_patches;
  for (const auto& import : file->imports) {
    if (import.alias.empty()) {
      if (error != nullptr) {
        *error = "Import alias cannot be empty in file: " + file_path.string();
      }
      return false;
    }
    if (!alias_names.insert(import.alias).second) {
      if (error != nullptr) {
        *error = "Duplicate import alias '" + import.alias + "' in file: " + file_path.string();
      }
      return false;
    }
    if (local_symbol_names.contains(import.alias)) {
      if (error != nullptr) {
        *error = "Import alias '" + import.alias + "' conflicts with local top-level symbol in file: " + file_path.string();
      }
      return false;
    }

    const std::filesystem::path import_path = ResolveImportPath(file_path, import.source);
    const std::string import_key = import_path.string();
    for (const auto& active : *import_stack) {
      if (active == import_key) {
        if (error != nullptr) {
          *error = "Import cycle detected: " + JoinCycle(*import_stack, import_key);
        }
        return false;
      }
    }

    std::string imported_source;
    std::string read_error;
    if (!ReadFile(import_path, &imported_source, &read_error)) {
      if (error != nullptr) {
        *error = "Failed to load import '" + import.source + "' from " + file_path.string() + ": " + read_error;
      }
      return false;
    }

    aurora::lang::ParseResult imported_parse = aurora::lang::ParseAuroraSource(imported_source);
    if (!imported_parse.ok) {
      if (error != nullptr) {
        std::ostringstream msg;
        msg << "Failed to parse import '" << import_path.string() << "'";
        if (!imported_parse.diagnostics.empty()) {
          const auto& d = imported_parse.diagnostics.front();
          msg << " (" << d.line << ":" << d.column << "): " << d.message;
        }
        *error = msg.str();
      }
      return false;
    }

    import_stack->push_back(import_key);
    if (!ResolveImportsRecursive(import_path, &imported_parse.file, import_stack, error)) {
      import_stack->pop_back();
      return false;
    }
    import_stack->pop_back();

    std::set<std::string> exported_names;
    for (const auto& imported_patch : imported_parse.file.patches) {
      const std::string exported_name = import.alias + "." + BasePatchName(imported_patch.name);
      if (!exported_names.insert(exported_name).second) {
        if (error != nullptr) {
          *error = "Import '" + import.alias + "' exports duplicate patch symbol '" + exported_name + "'.";
        }
        return false;
      }
      aurora::lang::PatchDefinition patch_copy = imported_patch;
      patch_copy.name = exported_name;
      imported_patches.push_back(std::move(patch_copy));
    }
  }

  std::set<std::string> all_patch_names;
  for (const auto& patch : file->patches) {
    all_patch_names.insert(patch.name);
  }
  for (const auto& patch : imported_patches) {
    if (!all_patch_names.insert(patch.name).second) {
      if (error != nullptr) {
        *error = "Imported patch name conflicts with existing patch: " + patch.name;
      }
      return false;
    }
    file->patches.push_back(patch);
  }

  return true;
}

std::filesystem::path ResolveOutputPath(const std::string& configured_path, const std::filesystem::path& au_parent,
                                        const std::optional<std::filesystem::path>& cli_out) {
  const std::filesystem::path path(configured_path);
  if (path.is_absolute()) {
    return path;
  }
  if (cli_out.has_value()) {
    return cli_out.value() / path;
  }
  return au_parent / path;
}

std::string FormatElapsed(const std::chrono::steady_clock::time_point& start) {
  const auto now = std::chrono::steady_clock::now();
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
  return std::to_string(ms) + "ms";
}

struct SpectrogramConfigOverrides {
  std::optional<int> window;
  std::optional<int> hop;
  std::optional<int> nfft;
  std::optional<std::string> mode;
  std::optional<std::string> freq_scale;
  std::optional<double> min_hz;
  std::optional<double> max_hz;
  std::optional<double> db_min;
  std::optional<double> db_max;
  std::optional<std::string> colormap;
  std::optional<int> width_px;
  std::optional<int> height_px;
  std::optional<double> gamma;
  std::optional<int> smoothing_bins;
};

bool IsValidEnum(const std::string& value, const std::set<std::string>& allowed) { return allowed.contains(value); }

void SkipWs(const std::string& text, size_t* pos) {
  while (*pos < text.size() && std::isspace(static_cast<unsigned char>(text[*pos])) != 0) {
    ++(*pos);
  }
}

bool ParseJsonString(const std::string& text, size_t* pos, std::string* out, std::string* error) {
  if (*pos >= text.size() || text[*pos] != '"') {
    if (error != nullptr) {
      *error = "Expected JSON string.";
    }
    return false;
  }
  ++(*pos);
  std::string value;
  while (*pos < text.size()) {
    const char c = text[*pos];
    ++(*pos);
    if (c == '"') {
      *out = value;
      return true;
    }
    if (c == '\\') {
      if (*pos >= text.size()) {
        if (error != nullptr) {
          *error = "Invalid JSON escape sequence.";
        }
        return false;
      }
      const char esc = text[*pos];
      ++(*pos);
      switch (esc) {
        case '"':
        case '\\':
        case '/':
          value.push_back(esc);
          break;
        case 'b':
          value.push_back('\b');
          break;
        case 'f':
          value.push_back('\f');
          break;
        case 'n':
          value.push_back('\n');
          break;
        case 'r':
          value.push_back('\r');
          break;
        case 't':
          value.push_back('\t');
          break;
        default:
          if (error != nullptr) {
            *error = "Unsupported JSON escape in spectrogram config.";
          }
          return false;
      }
      continue;
    }
    value.push_back(c);
  }
  if (error != nullptr) {
    *error = "Unterminated JSON string.";
  }
  return false;
}

bool ParseJsonNumberToken(const std::string& text, size_t* pos, std::string* out, std::string* error) {
  const size_t start = *pos;
  if (*pos < text.size() && (text[*pos] == '-' || text[*pos] == '+')) {
    ++(*pos);
  }
  bool any = false;
  while (*pos < text.size() && std::isdigit(static_cast<unsigned char>(text[*pos])) != 0) {
    any = true;
    ++(*pos);
  }
  if (*pos < text.size() && text[*pos] == '.') {
    ++(*pos);
    while (*pos < text.size() && std::isdigit(static_cast<unsigned char>(text[*pos])) != 0) {
      any = true;
      ++(*pos);
    }
  }
  if (*pos < text.size() && (text[*pos] == 'e' || text[*pos] == 'E')) {
    ++(*pos);
    if (*pos < text.size() && (text[*pos] == '+' || text[*pos] == '-')) {
      ++(*pos);
    }
    while (*pos < text.size() && std::isdigit(static_cast<unsigned char>(text[*pos])) != 0) {
      any = true;
      ++(*pos);
    }
  }
  if (!any) {
    if (error != nullptr) {
      *error = "Expected numeric value in spectrogram config.";
    }
    return false;
  }
  *out = text.substr(start, *pos - start);
  return true;
}

bool ParseIntegerToken(const std::string& token, int* out, std::string* error, const std::string& field_name) {
  if (token.find('.') != std::string::npos || token.find('e') != std::string::npos || token.find('E') != std::string::npos) {
    if (error != nullptr) {
      *error = "Expected integer for '" + field_name + "'.";
    }
    return false;
  }
  try {
    const long long value = std::stoll(token);
    if (value < static_cast<long long>(std::numeric_limits<int>::min()) ||
        value > static_cast<long long>(std::numeric_limits<int>::max())) {
      if (error != nullptr) {
        *error = "Integer out of range for '" + field_name + "'.";
      }
      return false;
    }
    *out = static_cast<int>(value);
    return true;
  } catch (const std::exception&) {
    if (error != nullptr) {
      *error = "Invalid integer for '" + field_name + "'.";
    }
    return false;
  }
}

bool ParseDoubleToken(const std::string& token, double* out, std::string* error, const std::string& field_name) {
  try {
    const double value = std::stod(token);
    if (!std::isfinite(value)) {
      if (error != nullptr) {
        *error = "Non-finite number for '" + field_name + "'.";
      }
      return false;
    }
    *out = value;
    return true;
  } catch (const std::exception&) {
    if (error != nullptr) {
      *error = "Invalid number for '" + field_name + "'.";
    }
    return false;
  }
}

bool ParseSpectrogramConfigJson(const std::string& json, SpectrogramConfigOverrides* overrides, std::string* error) {
  if (overrides == nullptr) {
    if (error != nullptr) {
      *error = "Internal error parsing spectrogram config.";
    }
    return false;
  }
  size_t pos = 0;
  SkipWs(json, &pos);
  if (pos >= json.size() || json[pos] != '{') {
    if (error != nullptr) {
      *error = "--spectrogram-config must be a JSON object.";
    }
    return false;
  }
  ++pos;
  std::set<std::string> seen_keys;

  while (true) {
    SkipWs(json, &pos);
    if (pos >= json.size()) {
      if (error != nullptr) {
        *error = "Unterminated JSON object in --spectrogram-config.";
      }
      return false;
    }
    if (json[pos] == '}') {
      ++pos;
      break;
    }

    std::string key;
    if (!ParseJsonString(json, &pos, &key, error)) {
      return false;
    }
    if (!seen_keys.insert(key).second) {
      if (error != nullptr) {
        *error = "Duplicate key in --spectrogram-config: " + key;
      }
      return false;
    }

    SkipWs(json, &pos);
    if (pos >= json.size() || json[pos] != ':') {
      if (error != nullptr) {
        *error = "Expected ':' after key '" + key + "' in --spectrogram-config.";
      }
      return false;
    }
    ++pos;
    SkipWs(json, &pos);
    if (pos >= json.size()) {
      if (error != nullptr) {
        *error = "Missing value for key '" + key + "' in --spectrogram-config.";
      }
      return false;
    }
    if (json.compare(pos, 4, "null") == 0) {
      if (error != nullptr) {
        *error = "null is not allowed for key '" + key + "' in --spectrogram-config.";
      }
      return false;
    }

    auto parse_string = [&](std::optional<std::string>* target) -> bool {
      std::string value;
      if (!ParseJsonString(json, &pos, &value, error)) {
        return false;
      }
      *target = value;
      return true;
    };
    auto parse_int = [&](std::optional<int>* target) -> bool {
      std::string token;
      if (!ParseJsonNumberToken(json, &pos, &token, error)) {
        return false;
      }
      int value = 0;
      if (!ParseIntegerToken(token, &value, error, key)) {
        return false;
      }
      *target = value;
      return true;
    };
    auto parse_double = [&](std::optional<double>* target) -> bool {
      std::string token;
      if (!ParseJsonNumberToken(json, &pos, &token, error)) {
        return false;
      }
      double value = 0.0;
      if (!ParseDoubleToken(token, &value, error, key)) {
        return false;
      }
      *target = value;
      return true;
    };

    if (key == "window") {
      if (!parse_int(&overrides->window)) {
        return false;
      }
    } else if (key == "hop") {
      if (!parse_int(&overrides->hop)) {
        return false;
      }
    } else if (key == "nfft") {
      if (!parse_int(&overrides->nfft)) {
        return false;
      }
    } else if (key == "mode") {
      if (!parse_string(&overrides->mode)) {
        return false;
      }
    } else if (key == "freq_scale") {
      if (!parse_string(&overrides->freq_scale)) {
        return false;
      }
    } else if (key == "min_hz") {
      if (!parse_double(&overrides->min_hz)) {
        return false;
      }
    } else if (key == "max_hz") {
      if (!parse_double(&overrides->max_hz)) {
        return false;
      }
    } else if (key == "db_min") {
      if (!parse_double(&overrides->db_min)) {
        return false;
      }
    } else if (key == "db_max") {
      if (!parse_double(&overrides->db_max)) {
        return false;
      }
    } else if (key == "colormap") {
      if (!parse_string(&overrides->colormap)) {
        return false;
      }
    } else if (key == "width_px") {
      if (!parse_int(&overrides->width_px)) {
        return false;
      }
    } else if (key == "height_px") {
      if (!parse_int(&overrides->height_px)) {
        return false;
      }
    } else if (key == "gamma") {
      if (!parse_double(&overrides->gamma)) {
        return false;
      }
    } else if (key == "smoothing_bins") {
      if (!parse_int(&overrides->smoothing_bins)) {
        return false;
      }
    } else {
      if (error != nullptr) {
        *error = "Unknown key in --spectrogram-config: " + key;
      }
      return false;
    }

    SkipWs(json, &pos);
    if (pos >= json.size()) {
      if (error != nullptr) {
        *error = "Unterminated JSON object in --spectrogram-config.";
      }
      return false;
    }
    if (json[pos] == ',') {
      ++pos;
      continue;
    }
    if (json[pos] == '}') {
      ++pos;
      break;
    }
    if (error != nullptr) {
      *error = "Expected ',' or '}' in --spectrogram-config.";
    }
    return false;
  }

  SkipWs(json, &pos);
  if (pos != json.size()) {
    if (error != nullptr) {
      *error = "Unexpected trailing characters in --spectrogram-config.";
    }
    return false;
  }
  return true;
}

bool IsPowerOfTwo(int value) { return value > 0 && (value & (value - 1)) == 0; }

bool BuildSpectrogramConfig(int sample_rate, const std::optional<std::string>& config_json, aurora::core::SpectrogramConfig* out,
                            std::string* error) {
  if (out == nullptr) {
    if (error != nullptr) {
      *error = "Internal error resolving spectrogram config.";
    }
    return false;
  }
  aurora::core::SpectrogramConfig config;
  config.max_hz = std::min(20000.0, 0.49 * static_cast<double>(sample_rate));
  SpectrogramConfigOverrides overrides;
  if (config_json.has_value()) {
    if (!ParseSpectrogramConfigJson(*config_json, &overrides, error)) {
      return false;
    }
  }

  auto apply = [](const auto& maybe, auto* field) {
    if (maybe.has_value()) {
      *field = *maybe;
    }
  };
  apply(overrides.window, &config.window);
  apply(overrides.hop, &config.hop);
  apply(overrides.nfft, &config.nfft);
  apply(overrides.mode, &config.mode);
  apply(overrides.freq_scale, &config.freq_scale);
  apply(overrides.min_hz, &config.min_hz);
  apply(overrides.max_hz, &config.max_hz);
  apply(overrides.db_min, &config.db_min);
  apply(overrides.db_max, &config.db_max);
  apply(overrides.colormap, &config.colormap);
  apply(overrides.width_px, &config.width_px);
  apply(overrides.height_px, &config.height_px);
  apply(overrides.gamma, &config.gamma);
  apply(overrides.smoothing_bins, &config.smoothing_bins);

  const std::set<std::string> modes = {"mixdown", "channels"};
  const std::set<std::string> scales = {"log", "linear"};
  const std::set<std::string> colormaps = {"magma", "inferno", "viridis", "plasma"};
  if (!IsValidEnum(config.mode, modes)) {
    if (error != nullptr) {
      *error = "spectrogram mode must be one of: mixdown, channels";
    }
    return false;
  }
  if (!IsValidEnum(config.freq_scale, scales)) {
    if (error != nullptr) {
      *error = "spectrogram freq_scale must be one of: log, linear";
    }
    return false;
  }
  if (!IsValidEnum(config.colormap, colormaps)) {
    if (error != nullptr) {
      *error = "spectrogram colormap must be one of: magma, inferno, viridis, plasma";
    }
    return false;
  }
  if (config.window < 2 || config.hop < 1 || config.nfft < config.window || !IsPowerOfTwo(config.nfft)) {
    if (error != nullptr) {
      *error = "Invalid spectrogram FFT parameters (require window>=2, hop>=1, nfft>=window and power-of-two).";
    }
    return false;
  }
  if (config.min_hz <= 0.0 || config.max_hz <= config.min_hz) {
    if (error != nullptr) {
      *error = "Invalid spectrogram frequency range (require 0 < min_hz < max_hz).";
    }
    return false;
  }
  if (overrides.max_hz.has_value() && config.max_hz > 0.49 * static_cast<double>(sample_rate)) {
    if (error != nullptr) {
      *error = "spectrogram max_hz cannot exceed 0.49 * sample_rate.";
    }
    return false;
  }
  if (config.db_max <= config.db_min) {
    if (error != nullptr) {
      *error = "spectrogram requires db_max > db_min.";
    }
    return false;
  }
  if (config.width_px < 2 || config.height_px < 2) {
    if (error != nullptr) {
      *error = "spectrogram requires width_px >= 2 and height_px >= 2.";
    }
    return false;
  }
  if (config.gamma <= 0.0) {
    if (error != nullptr) {
      *error = "spectrogram gamma must be > 0.";
    }
    return false;
  }
  if (config.smoothing_bins < 0) {
    if (error != nullptr) {
      *error = "spectrogram smoothing_bins must be >= 0.";
    }
    return false;
  }
  *out = config;
  return true;
}

std::string SanitizeTargetName(const std::string& in) {
  std::string out;
  out.reserve(in.size());
  for (const char c : in) {
    const unsigned char uc = static_cast<unsigned char>(c);
    if (std::iscntrl(uc) != 0 || c == '/' || c == '\\') {
      out.push_back('_');
      continue;
    }
    out.push_back(c);
  }
  if (out.empty()) {
    return "unnamed";
  }
  return out;
}

std::vector<float> Mixdown(const aurora::core::AudioStem& stem) {
  if (stem.channels <= 1) {
    return stem.samples;
  }
  std::vector<float> mono;
  mono.resize(stem.samples.size() / 2U);
  for (size_t i = 0, j = 0; i + 1 < stem.samples.size(); i += 2, ++j) {
    mono[j] = 0.5F * (stem.samples[i] + stem.samples[i + 1]);
  }
  return mono;
}

std::vector<float> ExtractChannel(const aurora::core::AudioStem& stem, int channel_index) {
  std::vector<float> mono;
  if (stem.channels <= 1) {
    mono = stem.samples;
    return mono;
  }
  mono.resize(stem.samples.size() / 2U);
  for (size_t i = static_cast<size_t>(channel_index), j = 0; i < stem.samples.size(); i += 2, ++j) {
    mono[j] = stem.samples[i];
  }
  return mono;
}

std::string RelativeToAnalysisRoot(const std::filesystem::path& absolute_path, const std::filesystem::path& analysis_root) {
  const std::filesystem::path abs_out = std::filesystem::absolute(absolute_path);
  const std::filesystem::path abs_root = std::filesystem::absolute(analysis_root);
  const std::filesystem::path rel = abs_out.lexically_relative(abs_root);
  if (rel.empty()) {
    return abs_out.generic_string();
  }
  return rel.generic_string();
}

aurora::core::SpectrogramArtifact BuildBaseArtifact(const aurora::core::SpectrogramConfig& config, int sample_rate) {
  aurora::core::SpectrogramArtifact out;
  out.present = true;
  out.enabled = false;
  out.mode = config.mode;
  out.sr = sample_rate;
  out.window = config.window;
  out.hop = config.hop;
  out.nfft = config.nfft;
  out.freq_scale = config.freq_scale;
  out.min_hz = config.min_hz;
  out.max_hz = config.max_hz;
  out.db_min = config.db_min;
  out.db_max = config.db_max;
  out.colormap = config.colormap;
  out.width_px = config.width_px;
  out.height_px = config.height_px;
  out.gamma = config.gamma;
  out.smoothing_bins = config.smoothing_bins;
  return out;
}

void MarkSpectrogramDisabled(aurora::core::FileAnalysis* item, const aurora::core::SpectrogramConfig& config, int sample_rate) {
  item->spectrogram = BuildBaseArtifact(config, sample_rate);
  item->spectrogram.enabled = false;
}

void PopulateSpectrograms(const std::vector<aurora::core::AudioStem>& stems, const aurora::core::AudioStem& mix, int sample_rate,
                         const aurora::core::SpectrogramConfig& config, const std::filesystem::path& spectrogram_dir,
                         const std::filesystem::path& analysis_root, int max_parallel_jobs, aurora::core::AnalysisReport* report,
                         const std::string& mode_label, const std::chrono::steady_clock::time_point& start_time) {
  auto log_step = [&](const std::string& msg) {
    std::cerr << "[aurora +" << FormatElapsed(start_time) << "] " << msg << "\n";
  };
  auto render_target = [&](const aurora::core::AudioStem& stem, const std::string& target_name) {
    aurora::core::SpectrogramArtifact artifact = BuildBaseArtifact(config, sample_rate);
    const std::string safe_name = SanitizeTargetName(target_name);

    auto write_png_for_signal = [&](const std::vector<float>& mono, const std::filesystem::path& out_path) -> bool {
      std::vector<uint8_t> rgb;
      std::string err;
      if (!aurora::core::RenderSpectrogramRgb(mono, sample_rate, config, &rgb, &err)) {
        artifact.enabled = false;
        artifact.error = err;
        return false;
      }
      if (!aurora::io::WritePngRgb8(out_path, config.width_px, config.height_px, rgb, &err)) {
        artifact.enabled = false;
        artifact.error = err;
        return false;
      }
      return true;
    };

    if (config.mode == "channels" && stem.channels == 2) {
      const std::filesystem::path left_path = spectrogram_dir / (safe_name + ".L.spectrogram.png");
      const std::filesystem::path right_path = spectrogram_dir / (safe_name + ".R.spectrogram.png");
      const bool ok_left = write_png_for_signal(ExtractChannel(stem, 0), left_path);
      const bool ok_right = ok_left ? write_png_for_signal(ExtractChannel(stem, 1), right_path) : false;
      if (ok_left && ok_right) {
        artifact.enabled = true;
        artifact.path = RelativeToAnalysisRoot(left_path, analysis_root);
        artifact.paths = {
            RelativeToAnalysisRoot(left_path, analysis_root),
            RelativeToAnalysisRoot(right_path, analysis_root),
        };
      }
      return artifact;
    }

    const std::filesystem::path out_path = spectrogram_dir / (safe_name + ".spectrogram.png");
    if (write_png_for_signal(Mixdown(stem), out_path)) {
      artifact.enabled = true;
      artifact.path = RelativeToAnalysisRoot(out_path, analysis_root);
      artifact.paths = {artifact.path};
    }
    return artifact;
  };

  log_step("Generating spectrograms (" + mode_label + ")");
  if (report == nullptr) {
    return;
  }
  std::vector<aurora::core::SpectrogramArtifact> stem_artifacts(stems.size());

  report->mix.spectrogram = BuildBaseArtifact(config, sample_rate);
  const int default_jobs = std::max(1U, std::thread::hardware_concurrency());
  const int requested_jobs = max_parallel_jobs > 0 ? max_parallel_jobs : default_jobs;
  const int max_jobs = std::max(1, requested_jobs);
  const size_t total_targets = stems.size() + 1U;

  if (max_jobs == 1 || total_targets <= 1U) {
    report->mix.spectrogram = render_target(mix, "mix");
    for (size_t i = 0; i < stems.size(); ++i) {
      stem_artifacts[i] = render_target(stems[i], stems[i].name);
    }
  } else {
    std::vector<aurora::core::SpectrogramArtifact> artifacts(total_targets);
    std::atomic<size_t> next_target{0U};
    const size_t worker_count = std::min(static_cast<size_t>(max_jobs), total_targets);
    std::vector<std::thread> workers;
    workers.reserve(worker_count);
    for (size_t worker = 0; worker < worker_count; ++worker) {
      workers.emplace_back([&]() {
        while (true) {
          const size_t target_index = next_target.fetch_add(1U);
          if (target_index >= total_targets) {
            break;
          }
          if (target_index == 0U) {
            artifacts[target_index] = render_target(mix, "mix");
          } else {
            const size_t stem_index = target_index - 1U;
            artifacts[target_index] = render_target(stems[stem_index], stems[stem_index].name);
          }
        }
      });
    }
    for (auto& worker : workers) {
      worker.join();
    }
    report->mix.spectrogram = std::move(artifacts[0]);
    for (size_t i = 0; i < stems.size(); ++i) {
      stem_artifacts[i] = std::move(artifacts[i + 1U]);
    }
  }

  const size_t stem_count = std::min(stems.size(), report->stems.size());
  for (size_t i = 0; i < stem_count; ++i) {
    report->stems[i].spectrogram = std::move(stem_artifacts[i]);
  }
}

int RunAnalyzeCommand(const AnalyzeCliOptions& options, const std::chrono::steady_clock::time_point& start_time) {
  auto log_step = [&](const std::string& msg) {
    std::cerr << "[aurora +" << FormatElapsed(start_time) << "] " << msg << "\n";
  };

  aurora::core::AnalysisOptions analysis_options;
  analysis_options.max_parallel_jobs = options.analyze_threads;
  analysis_options.intent = options.intent;

  aurora::core::AudioStem mix;
  std::vector<aurora::core::AudioStem> stems;
  int mix_sample_rate = 0;
  std::string mode;

  if (!options.stems_mode) {
    std::string error;
    log_step("Loading audio: " + options.positional.front().string());
    if (!aurora::io::ReadAudioFile(options.positional.front(), &mix, &mix_sample_rate, &error)) {
      std::cerr << "Analyze error: " << error << "\n";
      return 3;
    }
    mode = "standalone_analysis";
  } else {
    std::filesystem::path mix_path;
    std::vector<std::filesystem::path> stem_paths = options.positional;

    if (options.mix_file.has_value()) {
      mix_path = options.mix_file.value();
    } else {
      mix_path = stem_paths.back();
      stem_paths.pop_back();
    }

    std::string error;
    log_step("Loading mix audio: " + mix_path.string());
    if (!aurora::io::ReadAudioFile(mix_path, &mix, &mix_sample_rate, &error)) {
      std::cerr << "Analyze error: " << error << "\n";
      return 3;
    }

    for (const auto& stem_path : stem_paths) {
      aurora::core::AudioStem stem;
      int stem_sr = 0;
      log_step("Loading stem audio: " + stem_path.string());
      if (!aurora::io::ReadAudioFile(stem_path, &stem, &stem_sr, &error)) {
        std::cerr << "Analyze error: " << error << "\n";
        return 3;
      }
      if (stem_sr != mix_sample_rate) {
        std::cerr << "Analyze error: sample-rate mismatch between stem '" << stem_path.string() << "' (" << stem_sr
                  << ") and mix (" << mix_sample_rate << ").\n";
        return 3;
      }
      stems.push_back(std::move(stem));
    }
    mode = "hybrid_stems";
  }

  log_step("Running analysis");
  aurora::core::AnalysisReport report =
      aurora::core::AnalyzeFiles(stems, mix, mix_sample_rate, mode, analysis_options);

  const std::filesystem::path out_path = options.out_path.value_or(std::filesystem::current_path() / "analysis.json");
  const std::filesystem::path analysis_root = out_path.parent_path();
  aurora::core::SpectrogramConfig spectrogram_config;
  std::string spectrogram_error;
  if (!BuildSpectrogramConfig(mix_sample_rate, options.spectrogram_config_json, &spectrogram_config, &spectrogram_error)) {
    std::cerr << "Analyze error: " << spectrogram_error << "\n";
    return 2;
  }
  if (!options.spectrogram) {
    MarkSpectrogramDisabled(&report.mix, spectrogram_config, mix_sample_rate);
    for (auto& stem_analysis : report.stems) {
      MarkSpectrogramDisabled(&stem_analysis, spectrogram_config, mix_sample_rate);
    }
  } else {
    const std::filesystem::path spectrogram_out =
        options.spectrogram_out.value_or(analysis_root / "spectrograms");
    PopulateSpectrograms(stems, mix, mix_sample_rate, spectrogram_config, spectrogram_out, analysis_root,
                         options.analyze_threads, &report, "analyze", start_time);
  }

  std::string write_error;
  if (!aurora::io::WriteAnalysisJson(out_path, report, &write_error)) {
    std::cerr << "Analyze error: " << write_error << "\n";
    return 6;
  }

  log_step("Done");
  std::cout << "Analysis complete\n";
  std::cout << "  mode: " << report.mode << "\n";
  std::cout << "  sample_rate: " << report.sample_rate << "\n";
  std::cout << "  mix_lufs: " << report.mix.loudness.integrated_lufs << "\n";
  std::cout << "  output: " << out_path.string() << "\n";
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  const auto start_time = std::chrono::steady_clock::now();
  auto log_step = [&](const std::string& msg) {
    std::cerr << "[aurora +" << FormatElapsed(start_time) << "] " << msg << "\n";
  };

  if (argc < 2) {
    PrintUsage();
    return 2;
  }

  const std::string command = argv[1];
  if (command == "analyze") {
    AnalyzeCliOptions options;
    std::string cli_error;
    if (!ParseAnalyzeArgs(argc, argv, &options, &cli_error)) {
      std::cerr << "Argument error: " << cli_error << "\n";
      PrintUsage();
      return 2;
    }
    return RunAnalyzeCommand(options, start_time);
  }

  if (command != "render") {
    std::cerr << "Unsupported command: " << command << "\n";
    PrintUsage();
    return 2;
  }

  std::filesystem::path au_file;
  RenderCliOptions options;
  std::string cli_error;
  if (!ParseRenderArgs(argc, argv, &au_file, &options, &cli_error)) {
    std::cerr << "Argument error: " << cli_error << "\n";
    PrintUsage();
    return 2;
  }

  log_step("Reading source: " + au_file.string());
  std::string source;
  std::string read_error;
  if (!ReadFile(au_file, &source, &read_error)) {
    std::cerr << read_error << "\n";
    return 3;
  }

  log_step("Parsing");
  aurora::lang::ParseResult parse = aurora::lang::ParseAuroraSource(source);
  if (!parse.ok) {
    for (const auto& d : parse.diagnostics) {
      std::cerr << au_file.string() << ":" << d.line << ":" << d.column << ": parse error: " << d.message << "\n";
    }
    return 4;
  }

  log_step("Resolving imports");
  {
    std::error_code ec;
    const std::filesystem::path canonical_au_file = std::filesystem::weakly_canonical(au_file, ec);
    const std::string root_key = (ec ? au_file.lexically_normal() : canonical_au_file).string();
    std::vector<std::string> import_stack;
    import_stack.push_back(root_key);
    std::string import_error;
    if (!ResolveImportsRecursive(au_file, &parse.file, &import_stack, &import_error)) {
      std::cerr << "import error: " << import_error << "\n";
      return 4;
    }
  }

  log_step("Validating");
  const aurora::lang::ValidationResult validation = aurora::lang::Validate(parse.file);
  for (const auto& warning : validation.warnings) {
    std::cerr << "warning: " << warning << "\n";
  }
  if (!validation.ok) {
    for (const auto& err : validation.errors) {
      std::cerr << "validation error: " << err << "\n";
    }
    return 5;
  }

  log_step("Rendering audio/MIDI");
  aurora::core::Renderer renderer;
  aurora::core::RenderOptions render_options;
  render_options.seed = options.seed;
  render_options.sample_rate_override = options.sample_rate;
  int last_render_pct = -5;
  render_options.progress_callback = [&](double pct) {
    int rounded = static_cast<int>(pct + 0.5);
    if (rounded < 0) {
      rounded = 0;
    } else if (rounded > 100) {
      rounded = 100;
    }
    if (rounded >= last_render_pct + 5 || rounded == 100) {
      last_render_pct = rounded;
      std::cerr << "[aurora +" << FormatElapsed(start_time) << "] Rendering " << rounded << "%\n";
    }
  };
  aurora::core::RenderResult rendered = renderer.Render(parse.file, render_options);

  const std::filesystem::path au_parent =
      au_file.has_parent_path() ? std::filesystem::absolute(au_file).parent_path() : std::filesystem::current_path();
  const std::filesystem::path stems_dir =
      options.out_root.has_value() ? options.out_root.value() / "stems"
                                   : ResolveOutputPath(parse.file.outputs.stems_dir, au_parent, std::nullopt);
  const std::filesystem::path midi_dir =
      options.out_root.has_value() ? options.out_root.value() / "midi"
                                   : ResolveOutputPath(parse.file.outputs.midi_dir, au_parent, std::nullopt);
  const std::filesystem::path mix_dir =
      options.out_root.has_value() ? options.out_root.value() / "mix"
                                   : ResolveOutputPath(parse.file.outputs.mix_dir, au_parent, std::nullopt);
  const std::filesystem::path meta_dir =
      options.out_root.has_value() ? options.out_root.value() / "meta"
                                   : ResolveOutputPath(parse.file.outputs.meta_dir, au_parent, std::nullopt);

  log_step("Writing outputs");
  std::vector<std::future<std::optional<std::string>>> write_jobs;
  write_jobs.reserve(rendered.patch_stems.size() + rendered.bus_stems.size() + 3U);

  for (const auto& stem : rendered.patch_stems) {
    const auto* stem_ptr = &stem;
    const auto path = stems_dir / (stem.name + ".wav");
    write_jobs.push_back(std::async(std::launch::async, [path, stem_ptr, sr = rendered.metadata.sample_rate]() {
      std::string error;
      if (!aurora::io::WriteWavFloat32(path, *stem_ptr, sr, &error)) {
        return std::optional<std::string>(error);
      }
      return std::optional<std::string>{};
    }));
  }
  for (const auto& stem : rendered.bus_stems) {
    const auto* stem_ptr = &stem;
    const auto path = stems_dir / (stem.name + ".wav");
    write_jobs.push_back(std::async(std::launch::async, [path, stem_ptr, sr = rendered.metadata.sample_rate]() {
      std::string error;
      if (!aurora::io::WriteWavFloat32(path, *stem_ptr, sr, &error)) {
        return std::optional<std::string>(error);
      }
      return std::optional<std::string>{};
    }));
  }
  {
    const auto master_path = mix_dir / parse.file.outputs.master;
    write_jobs.push_back(std::async(std::launch::async, [master_path, &rendered]() {
      std::string error;
      if (!aurora::io::WriteWavFloat32(master_path, rendered.master, rendered.metadata.sample_rate, &error)) {
        return std::optional<std::string>(error);
      }
      return std::optional<std::string>{};
    }));
  }
  {
    const aurora::core::TempoMap tempo_map = aurora::core::BuildTempoMap(parse.file.globals);
    const auto midi_path = midi_dir / "arrangement.mid";
    write_jobs.push_back(std::async(std::launch::async, [midi_path, &rendered, tempo_map]() {
      std::string error;
      if (!aurora::io::WriteMidiFormat1(midi_path, rendered.midi_tracks, tempo_map, rendered.metadata.total_samples,
                                        rendered.metadata.sample_rate, &error)) {
        return std::optional<std::string>(error);
      }
      return std::optional<std::string>{};
    }));
  }
  {
    const auto meta_path = meta_dir / parse.file.outputs.render_json;
    write_jobs.push_back(std::async(std::launch::async, [meta_path, &rendered]() {
      std::string error;
      if (!aurora::io::WriteRenderJson(meta_path, rendered, &error)) {
        return std::optional<std::string>(error);
      }
      return std::optional<std::string>{};
    }));
  }
  for (auto& job : write_jobs) {
    const std::optional<std::string> maybe_error = job.get();
    if (maybe_error.has_value()) {
      std::cerr << "I/O error: " << *maybe_error << "\n";
      return 6;
    }
  }

  std::optional<std::filesystem::path> analysis_path;
  if (options.analyze) {
    log_step("Running integrated analysis");
    aurora::core::AnalysisOptions analysis_options;
    analysis_options.max_parallel_jobs = options.analyze_threads;
    analysis_options.intent = options.intent;
    aurora::core::AnalysisReport report = aurora::core::AnalyzeRender(rendered, analysis_options);
    const std::filesystem::path out_path = options.analysis_out.value_or(meta_dir / "analysis.json");
    const std::filesystem::path analysis_root = out_path.parent_path();
    aurora::core::SpectrogramConfig spectrogram_config;
    std::string spectrogram_error;
    if (!BuildSpectrogramConfig(rendered.metadata.sample_rate, options.spectrogram_config_json, &spectrogram_config,
                                &spectrogram_error)) {
      std::cerr << "Argument error: " << spectrogram_error << "\n";
      return 2;
    }
    if (!options.spectrogram) {
      MarkSpectrogramDisabled(&report.mix, spectrogram_config, rendered.metadata.sample_rate);
      for (auto& stem_analysis : report.stems) {
        MarkSpectrogramDisabled(&stem_analysis, spectrogram_config, rendered.metadata.sample_rate);
      }
    } else {
      std::vector<aurora::core::AudioStem> rendered_stems;
      rendered_stems.reserve(rendered.patch_stems.size() + rendered.bus_stems.size());
      rendered_stems.insert(rendered_stems.end(), rendered.patch_stems.begin(), rendered.patch_stems.end());
      rendered_stems.insert(rendered_stems.end(), rendered.bus_stems.begin(), rendered.bus_stems.end());
      const std::filesystem::path spectrogram_out =
          options.spectrogram_out.value_or(meta_dir / "spectrograms");
      PopulateSpectrograms(rendered_stems, rendered.master, rendered.metadata.sample_rate, spectrogram_config, spectrogram_out,
                           analysis_root, options.analyze_threads, &report, "render", start_time);
    }
    std::string error;
    if (!aurora::io::WriteAnalysisJson(out_path, report, &error)) {
      std::cerr << "I/O error: " << error << "\n";
      return 6;
    }
    analysis_path = out_path;
    std::cout << "  mix_lufs: " << report.mix.loudness.integrated_lufs << "\n";
    std::cout << "  transients_per_minute: " << report.mix.transient.transients_per_minute << "\n";
  }

  log_step("Done");
  std::cout << "Render complete\n";
  std::cout << "  sample_rate: " << rendered.metadata.sample_rate << "\n";
  std::cout << "  total_samples: " << rendered.metadata.total_samples << "\n";
  std::cout << "  stems: " << rendered.patch_stems.size() + rendered.bus_stems.size() << "\n";
  std::cout << "  midi_tracks: " << rendered.midi_tracks.size() << "\n";
  if (analysis_path.has_value()) {
    std::cout << "  analysis: " << analysis_path->string() << "\n";
  }
  return 0;
}
