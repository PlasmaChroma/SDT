#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "aurora/core/analyzer.hpp"
#include "aurora/core/renderer.hpp"
#include "aurora/core/timebase.hpp"
#include "aurora/io/analysis_writer.hpp"
#include "aurora/io/audio_reader.hpp"
#include "aurora/io/json_writer.hpp"
#include "aurora/io/midi_writer.hpp"
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
};

struct AnalyzeCliOptions {
  std::vector<std::filesystem::path> positional;
  bool stems_mode = false;
  std::optional<std::filesystem::path> mix_file;
  std::optional<std::filesystem::path> out_path;
  int analyze_threads = 0;
  std::string intent;
};

void PrintUsage() {
  std::cerr << "Usage:\n";
  std::cerr << "  aurora render <file.au> [--seed N] [--sr 44100|48000|96000] [--out <dir>] [--analyze]";
  std::cerr << " [--analysis-out <path>] [--analyze-threads N] [--intent sleep|ritual|dub]\n";
  std::cerr << "  aurora analyze <input.wav|input.flac|input.mp3|input.aiff> [--out <analysis.json>] [--analyze-threads N]";
  std::cerr << " [--intent sleep|ritual|dub]\n";
  std::cerr << "  aurora analyze --stems <stem1.wav> <stem2.wav> ... [--mix <mix.wav>] [--out <analysis.json>]";
  std::cerr << " [--analyze-threads N] [--intent sleep|ritual|dub]\n";
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
  const aurora::core::AnalysisReport report =
      aurora::core::AnalyzeFiles(stems, mix, mix_sample_rate, mode, analysis_options);

  const std::filesystem::path out_path = options.out_path.value_or(std::filesystem::current_path() / "analysis.json");
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
    const aurora::core::AnalysisReport report = aurora::core::AnalyzeRender(rendered, analysis_options);
    const std::filesystem::path out_path = options.analysis_out.value_or(meta_dir / "analysis.json");
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
