#include <filesystem>
#include <fstream>
#include <chrono>
#include <future>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "aurora/core/renderer.hpp"
#include "aurora/core/timebase.hpp"
#include "aurora/io/json_writer.hpp"
#include "aurora/io/midi_writer.hpp"
#include "aurora/io/wav_writer.hpp"
#include "aurora/lang/parser.hpp"
#include "aurora/lang/validation.hpp"

namespace {

struct CliOptions {
  uint64_t seed = 0;
  int sample_rate = 0;
  std::optional<std::filesystem::path> out_root;
};

void PrintUsage() {
  std::cerr << "Usage:\n";
  std::cerr << "  aurora render <file.au> [--seed N] [--sr 44100|48000|96000] [--out <dir>]\n";
}

bool ParseRenderArgs(int argc, char** argv, std::filesystem::path* file, CliOptions* options, std::string* error) {
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
      options->seed = static_cast<uint64_t>(std::stoull(argv[++i]));
      continue;
    }
    if (arg == "--sr") {
      if (i + 1 >= argc) {
        *error = "Expected value after --sr";
        return false;
      }
      options->sample_rate = std::stoi(argv[++i]);
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
    *error = "Unknown argument: " + arg;
    return false;
  }
  return true;
}

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in.is_open()) {
    return {};
  }
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
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
  if (command != "render") {
    std::cerr << "Unsupported command: " << command << "\n";
    PrintUsage();
    return 2;
  }

  std::filesystem::path au_file;
  CliOptions options;
  std::string cli_error;
  if (!ParseRenderArgs(argc, argv, &au_file, &options, &cli_error)) {
    std::cerr << "Argument error: " << cli_error << "\n";
    PrintUsage();
    return 2;
  }

  log_step("Reading source: " + au_file.string());
  const std::string source = ReadFile(au_file);
  if (source.empty()) {
    std::cerr << "Failed to read .au file: " << au_file << "\n";
    return 3;
  }

  log_step("Parsing");
  const aurora::lang::ParseResult parse = aurora::lang::ParseAuroraSource(source);
  if (!parse.ok) {
    for (const auto& d : parse.diagnostics) {
      std::cerr << au_file.string() << ":" << d.line << ":" << d.column << ": parse error: " << d.message << "\n";
    }
    return 4;
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
    const auto path = stems_dir / (stem.name + ".wav");
    write_jobs.push_back(std::async(std::launch::async, [path, &stem, sr = rendered.metadata.sample_rate]() {
      std::string error;
      if (!aurora::io::WriteWavFloat32(path, stem, sr, &error)) {
        return std::optional<std::string>(error);
      }
      return std::optional<std::string>{};
    }));
  }
  for (const auto& stem : rendered.bus_stems) {
    const auto path = stems_dir / (stem.name + ".wav");
    write_jobs.push_back(std::async(std::launch::async, [path, &stem, sr = rendered.metadata.sample_rate]() {
      std::string error;
      if (!aurora::io::WriteWavFloat32(path, stem, sr, &error)) {
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

  log_step("Done");
  std::cout << "Render complete\n";
  std::cout << "  sample_rate: " << rendered.metadata.sample_rate << "\n";
  std::cout << "  total_samples: " << rendered.metadata.total_samples << "\n";
  std::cout << "  stems: " << rendered.patch_stems.size() + rendered.bus_stems.size() << "\n";
  std::cout << "  midi_tracks: " << rendered.midi_tracks.size() << "\n";
  return 0;
}
