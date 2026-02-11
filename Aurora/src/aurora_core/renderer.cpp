#include "aurora/core/renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "aurora/core/rng.hpp"
#include "aurora/core/timebase.hpp"

namespace aurora::core {
namespace {

constexpr double kPi = 3.14159265358979323846;

double Clamp(double v, double lo, double hi) { return std::max(lo, std::min(v, hi)); }

double DbToLinear(double db) { return std::pow(10.0, db / 20.0); }

std::string ValueToText(const aurora::lang::ParamValue& value) {
  if (value.kind == aurora::lang::ParamValue::Kind::kString || value.kind == aurora::lang::ParamValue::Kind::kIdentifier) {
    return value.string_value;
  }
  return value.DebugString();
}

double ValueToNumber(const aurora::lang::ParamValue& value, double fallback = 0.0) {
  if (value.kind == aurora::lang::ParamValue::Kind::kNumber) {
    return value.number_value;
  }
  if (value.kind == aurora::lang::ParamValue::Kind::kUnitNumber) {
    return value.unit_number_value.value;
  }
  return fallback;
}

aurora::lang::UnitNumber ValueToUnit(const aurora::lang::ParamValue& value, const std::string& default_unit = "s") {
  if (value.kind == aurora::lang::ParamValue::Kind::kUnitNumber) {
    return value.unit_number_value;
  }
  if (value.kind == aurora::lang::ParamValue::Kind::kNumber) {
    return aurora::lang::UnitNumber{value.number_value, default_unit};
  }
  return aurora::lang::UnitNumber{0.0, default_unit};
}

bool StartsWith(const std::string& value, const std::string& prefix) {
  return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

bool EndsWith(const std::string& value, const std::string& suffix) {
  return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::vector<int> BuildEuclideanPattern(int pulses, int steps, int rotation) {
  std::vector<int> out;
  if (steps <= 0) {
    return out;
  }
  pulses = std::max(0, std::min(pulses, steps));
  out.assign(static_cast<size_t>(steps), 0);
  for (int i = 0; i < steps; ++i) {
    out[static_cast<size_t>(i)] = ((i * pulses) % steps) < pulses ? 1 : 0;
  }
  rotation %= steps;
  if (rotation < 0) {
    rotation += steps;
  }
  std::rotate(out.begin(), out.begin() + rotation, out.end());
  return out;
}

struct ResolvedPitch {
  double frequency = 440.0;
  int midi = 69;
};

int NoteNameToMidi(const std::string& note_text) {
  if (note_text.empty()) {
    return 69;
  }
  const char letter = static_cast<char>(std::toupper(static_cast<unsigned char>(note_text[0])));
  int semitone = 0;
  switch (letter) {
    case 'C':
      semitone = 0;
      break;
    case 'D':
      semitone = 2;
      break;
    case 'E':
      semitone = 4;
      break;
    case 'F':
      semitone = 5;
      break;
    case 'G':
      semitone = 7;
      break;
    case 'A':
      semitone = 9;
      break;
    case 'B':
      semitone = 11;
      break;
    default:
      return 69;
  }

  size_t idx = 1;
  if (idx < note_text.size()) {
    if (note_text[idx] == '#') {
      ++semitone;
      ++idx;
    } else if (note_text[idx] == 'b' || note_text[idx] == 'B') {
      --semitone;
      ++idx;
    }
  }

  int octave = 4;
  if (idx < note_text.size()) {
    try {
      octave = std::stoi(note_text.substr(idx));
    } catch (...) {
      octave = 4;
    }
  }
  return (octave + 1) * 12 + semitone;
}

double MidiToFrequency(int midi) { return 440.0 * std::pow(2.0, (static_cast<double>(midi) - 69.0) / 12.0); }

ResolvedPitch ResolvePitchValue(const aurora::lang::ParamValue& value) {
  if (value.kind == aurora::lang::ParamValue::Kind::kUnitNumber) {
    if (value.unit_number_value.unit == "Hz") {
      const double hz = std::max(1.0, value.unit_number_value.value);
      return ResolvedPitch{hz, static_cast<int>(std::round(69.0 + 12.0 * std::log2(hz / 440.0)))};
    }
    const int midi = static_cast<int>(std::round(value.unit_number_value.value));
    return ResolvedPitch{MidiToFrequency(midi), midi};
  }
  if (value.kind == aurora::lang::ParamValue::Kind::kNumber) {
    const int midi = static_cast<int>(std::round(value.number_value));
    return ResolvedPitch{MidiToFrequency(midi), midi};
  }
  if (value.kind == aurora::lang::ParamValue::Kind::kIdentifier || value.kind == aurora::lang::ParamValue::Kind::kString) {
    const int midi = NoteNameToMidi(value.string_value);
    return ResolvedPitch{MidiToFrequency(midi), midi};
  }
  return ResolvedPitch{};
}

struct AutomationLane {
  std::string curve;
  std::vector<std::pair<uint64_t, double>> points;
};

double EvaluateLane(const AutomationLane& lane, uint64_t sample) {
  if (lane.points.empty()) {
    return 0.0;
  }
  if (sample <= lane.points.front().first) {
    return lane.points.front().second;
  }
  if (sample >= lane.points.back().first) {
    return lane.points.back().second;
  }

  for (size_t i = 0; i + 1 < lane.points.size(); ++i) {
    const auto [x0, y0] = lane.points[i];
    const auto [x1, y1] = lane.points[i + 1];
    if (sample < x0 || sample > x1) {
      continue;
    }
    const double t = static_cast<double>(sample - x0) / static_cast<double>(x1 - x0);
    if (lane.curve == "step") {
      return y0;
    }
    if (lane.curve == "exp") {
      const double s0 = std::max(0.0001, y0);
      const double s1 = std::max(0.0001, y1);
      return s0 * std::pow(s1 / s0, t);
    }
    if (lane.curve == "smooth") {
      const double s = t * t * (3.0 - 2.0 * t);
      return y0 + (y1 - y0) * s;
    }
    return y0 + (y1 - y0) * t;
  }
  return lane.points.back().second;
}

struct PlayOccurrence {
  std::string patch;
  uint64_t start_sample = 0;
  uint64_t dur_samples = 0;
  double velocity = 1.0;
  std::vector<ResolvedPitch> pitches;
};

struct SeqDensity {
  double rate_multiplier = 1.0;
  double prob_multiplier = 1.0;
  int max_events_per_minute = 32;
};

SeqDensity DensityFromPreset(const std::string& preset) {
  if (preset == "very_low") {
    return SeqDensity{0.5, 0.6, 8};
  }
  if (preset == "low") {
    return SeqDensity{0.75, 0.8, 16};
  }
  if (preset == "high") {
    return SeqDensity{1.25, 1.15, 64};
  }
  return SeqDensity{};
}

double SilenceProbability(const std::string& preset) {
  if (preset == "long") {
    return 0.60;
  }
  if (preset == "medium") {
    return 0.35;
  }
  if (preset == "short") {
    return 0.15;
  }
  return 0.0;
}

struct SectionConstraintState {
  std::string density = "medium";
  std::string silence;
};

SectionConstraintState ResolveSectionConstraints(const aurora::lang::SectionDefinition& section) {
  SectionConstraintState state;
  if (const auto it = section.directives.find("pack"); it != section.directives.end()) {
    const std::string pack = ValueToText(it->second);
    if (pack == "resist_resolution") {
      state.density = "low";
      state.silence = "medium";
    } else if (pack == "long_breath") {
      state.density = "very_low";
      state.silence = "long";
    } else if (pack == "sparse_events") {
      state.density = "very_low";
    } else if (pack == "monolithic_decl") {
      state.density = "low";
      state.silence = "long";
    }
  }
  if (const auto it = section.directives.find("density"); it != section.directives.end()) {
    state.density = ValueToText(it->second);
  }
  if (const auto it = section.directives.find("silence"); it != section.directives.end()) {
    state.silence = ValueToText(it->second);
  }
  return state;
}

struct ExpansionResult {
  std::vector<PlayOccurrence> plays;
  std::map<std::string, std::map<std::string, AutomationLane>> automation;
  uint64_t timeline_end = 0;
};

std::vector<aurora::lang::ParamValue> SeqPitchList(const std::map<std::string, aurora::lang::ParamValue>& fields) {
  const auto it = fields.find("pitch");
  if (it == fields.end()) {
    return {aurora::lang::ParamValue::Identifier("C4")};
  }
  if (it->second.kind == aurora::lang::ParamValue::Kind::kList) {
    return it->second.list_values;
  }
  return {it->second};
}

double ParamAsSeconds(const aurora::lang::ParamValue& value, const TempoMap& tempo_map) {
  return ToSeconds(ValueToUnit(value), tempo_map);
}

double FieldSecondsOr(const std::map<std::string, aurora::lang::ParamValue>& fields, const std::string& key,
                      double fallback_seconds, const TempoMap& tempo_map) {
  const auto it = fields.find(key);
  if (it == fields.end()) {
    return fallback_seconds;
  }
  return ParamAsSeconds(it->second, tempo_map);
}

double FieldNumberOr(const std::map<std::string, aurora::lang::ParamValue>& fields, const std::string& key, double fallback) {
  const auto it = fields.find(key);
  if (it == fields.end()) {
    return fallback;
  }
  return ValueToNumber(it->second, fallback);
}

std::string FieldTextOr(const std::map<std::string, aurora::lang::ParamValue>& fields, const std::string& key,
                        const std::string& fallback) {
  const auto it = fields.find(key);
  if (it == fields.end()) {
    return fallback;
  }
  return ValueToText(it->second);
}

struct BurstConfig {
  double probability = 0.0;
  int count = 0;
  double spread_seconds = 0.0;
};

BurstConfig ParseBurst(const std::map<std::string, aurora::lang::ParamValue>& fields, const TempoMap& tempo_map) {
  BurstConfig cfg;
  const auto it = fields.find("burst");
  if (it == fields.end() || it->second.kind != aurora::lang::ParamValue::Kind::kObject) {
    return cfg;
  }
  const auto& obj = it->second.object_values;
  if (const auto p = obj.find("prob"); p != obj.end()) {
    cfg.probability = Clamp(ValueToNumber(p->second, 0.0), 0.0, 1.0);
  }
  if (const auto c = obj.find("count"); c != obj.end()) {
    cfg.count = static_cast<int>(std::round(ValueToNumber(c->second, 0.0)));
  }
  if (const auto s = obj.find("spread"); s != obj.end()) {
    cfg.spread_seconds = ParamAsSeconds(s->second, tempo_map);
  }
  return cfg;
}

std::vector<double> ParseWeights(const std::map<std::string, aurora::lang::ParamValue>& fields, size_t expected_count) {
  std::vector<double> out;
  const auto it = fields.find("weights");
  if (it == fields.end() || it->second.kind != aurora::lang::ParamValue::Kind::kList) {
    return out;
  }
  for (const auto& v : it->second.list_values) {
    out.push_back(std::max(0.0, ValueToNumber(v, 0.0)));
  }
  if (out.size() < expected_count) {
    out.resize(expected_count, 1.0);
  }
  return out;
}

size_t PickPitchIndex(const std::string& strategy, size_t step_index, const std::vector<double>& weights, PCG32& rng) {
  if (strategy == "cycle") {
    if (weights.empty()) {
      return 0;
    }
    return step_index % weights.size();
  }
  if (strategy == "weighted" && !weights.empty()) {
    double total = 0.0;
    for (const double w : weights) {
      total += std::max(0.0, w);
    }
    if (total <= 0.0) {
      return 0;
    }
    const double needle = rng.Uniform(0.0, total);
    double running = 0.0;
    for (size_t i = 0; i < weights.size(); ++i) {
      running += std::max(0.0, weights[i]);
      if (needle <= running) {
        return i;
      }
    }
    return weights.size() - 1;
  }
  if (weights.empty()) {
    return 0;
  }
  const uint32_t r = rng.NextUInt();
  return static_cast<size_t>(r % static_cast<uint32_t>(weights.size()));
}

bool SeqStepActive(const aurora::lang::ParamValue* pattern_value, size_t step_index, std::vector<int>* euclid_cache) {
  if (pattern_value == nullptr) {
    return true;
  }
  if (pattern_value->kind == aurora::lang::ParamValue::Kind::kString ||
      pattern_value->kind == aurora::lang::ParamValue::Kind::kIdentifier) {
    const std::string pattern = pattern_value->string_value;
    if (pattern.empty()) {
      return true;
    }
    const char ch = pattern[step_index % pattern.size()];
    return ch == 'x' || ch == 'X' || ch == '*' || ch == '1';
  }
  if (pattern_value->kind == aurora::lang::ParamValue::Kind::kCall && pattern_value->string_value == "euclid") {
    if (euclid_cache->empty()) {
      const auto& args = pattern_value->list_values;
      const int k = args.size() > 0 ? static_cast<int>(std::round(ValueToNumber(args[0], 0.0))) : 0;
      const int n = args.size() > 1 ? static_cast<int>(std::round(ValueToNumber(args[1], 0.0))) : 1;
      const int rot = args.size() > 2 ? static_cast<int>(std::round(ValueToNumber(args[2], 0.0))) : 0;
      *euclid_cache = BuildEuclideanPattern(k, n, rot);
    }
    if (euclid_cache->empty()) {
      return false;
    }
    return (*euclid_cache)[step_index % euclid_cache->size()] != 0;
  }
  return true;
}

void AddSeqHit(std::vector<PlayOccurrence>* plays, std::deque<double>* rolling_times, double absolute_seconds, uint64_t start_sample,
               uint64_t dur_samples, const std::string& patch, double velocity, const ResolvedPitch& pitch, int max_events_per_minute) {
  while (!rolling_times->empty() && absolute_seconds - rolling_times->front() > 60.0) {
    rolling_times->pop_front();
  }
  if (max_events_per_minute > 0 && static_cast<int>(rolling_times->size()) >= max_events_per_minute) {
    return;
  }
  rolling_times->push_back(absolute_seconds);
  PlayOccurrence occurrence;
  occurrence.patch = patch;
  occurrence.start_sample = start_sample;
  occurrence.dur_samples = dur_samples;
  occurrence.velocity = velocity;
  occurrence.pitches.push_back(pitch);
  plays->push_back(std::move(occurrence));
}

ExpansionResult ExpandScore(const aurora::lang::AuroraFile& file, const TempoMap& tempo_map, int sample_rate, uint64_t seed) {
  ExpansionResult out;

  for (const auto& section : file.sections) {
    const SectionConstraintState constraints = ResolveSectionConstraints(section);
    const SeqDensity density = DensityFromPreset(constraints.density);
    const double silence_prob = SilenceProbability(constraints.silence);

    const uint64_t section_start = ToSamples(section.at, tempo_map, sample_rate);
    const uint64_t section_dur = ToSamples(section.dur, tempo_map, sample_rate);
    out.timeline_end = std::max(out.timeline_end, section_start + section_dur);

    for (const auto& event : section.events) {
      if (std::holds_alternative<aurora::lang::PlayEvent>(event)) {
        const auto& play = std::get<aurora::lang::PlayEvent>(event);
        PlayOccurrence occurrence;
        occurrence.patch = play.patch;
        occurrence.start_sample = ToSamples(play.at, tempo_map, sample_rate);
        occurrence.dur_samples = std::max<uint64_t>(1, ToSamples(play.dur, tempo_map, sample_rate));
        occurrence.velocity = Clamp(play.vel, 0.0, 1.5);
        for (const auto& p : play.pitch_values) {
          occurrence.pitches.push_back(ResolvePitchValue(p));
        }
        if (occurrence.pitches.empty()) {
          occurrence.pitches.push_back(ResolvePitchValue(aurora::lang::ParamValue::Identifier("C4")));
        }
        out.timeline_end = std::max(out.timeline_end, occurrence.start_sample + occurrence.dur_samples);
        out.plays.push_back(std::move(occurrence));
        continue;
      }

      if (std::holds_alternative<aurora::lang::AutomateEvent>(event)) {
        const auto& automate = std::get<aurora::lang::AutomateEvent>(event);
        const std::string target = automate.target;
        std::vector<std::string> parts;
        size_t start = 0;
        while (start < target.size()) {
          const size_t dot = target.find('.', start);
          if (dot == std::string::npos) {
            parts.push_back(target.substr(start));
            break;
          }
          parts.push_back(target.substr(start, dot - start));
          start = dot + 1;
        }
        if (parts.size() < 4 || parts[0] != "patch") {
          continue;
        }
        const std::string patch_name = parts[1];
        const std::string key = parts[2] + "." + parts[3];
        AutomationLane lane;
        lane.curve = automate.curve;
        for (const auto& [time, value] : automate.points) {
          const uint64_t sample = ToSamples(time, tempo_map, sample_rate);
          lane.points.push_back({sample, ValueToNumber(value, 0.0)});
        }
        std::sort(lane.points.begin(), lane.points.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        out.automation[patch_name][key] = std::move(lane);
        continue;
      }
      if (std::holds_alternative<aurora::lang::SeqEvent>(event)) {
        const auto& seq = std::get<aurora::lang::SeqEvent>(event);
        const auto& fields = seq.fields;

        double at_s = static_cast<double>(section_start) / static_cast<double>(sample_rate);
        double dur_s = static_cast<double>(section_dur) / static_cast<double>(sample_rate);
        if (const auto it = fields.find("at"); it != fields.end()) {
          at_s = ParamAsSeconds(it->second, tempo_map);
        }
        if (const auto it = fields.find("dur"); it != fields.end()) {
          dur_s = ParamAsSeconds(it->second, tempo_map);
        }

        double rate_s = FieldSecondsOr(fields, "rate", 1.0, tempo_map);
        rate_s = std::max(0.001, rate_s * density.rate_multiplier);
        const double prob = Clamp(FieldNumberOr(fields, "prob", 1.0) * density.prob_multiplier, 0.0, 1.0);
        const double velocity = Clamp(FieldNumberOr(fields, "vel", 0.8), 0.0, 1.0);
        const double jitter_s = std::max(0.0, FieldSecondsOr(fields, "jitter", 0.0, tempo_map));
        const double swing = Clamp(FieldNumberOr(fields, "swing", 0.5), 0.0, 1.0);
        const int seq_max = static_cast<int>(std::round(FieldNumberOr(fields, "max", static_cast<double>(density.max_events_per_minute))));
        const int max_per_minute = std::min(seq_max, density.max_events_per_minute);
        const double event_len_s = std::max(0.030, std::min(rate_s * 0.9, 0.35));

        const std::vector<aurora::lang::ParamValue> pitch_values = SeqPitchList(fields);
        std::vector<double> weights = ParseWeights(fields, pitch_values.size());
        if (weights.empty()) {
          weights.resize(pitch_values.size(), 1.0);
        }

        const std::string pick = FieldTextOr(fields, "pick", "uniform");
        const auto pattern_it = fields.find("pattern");
        const aurora::lang::ParamValue* pattern = pattern_it != fields.end() ? &pattern_it->second : nullptr;
        std::vector<int> euclid_pattern;

        const BurstConfig burst = ParseBurst(fields, tempo_map);

        PCG32 rng(Hash64FromParts(seed, "seq", section.name, seq.patch));
        std::deque<double> rolling_times;
        const size_t step_count = static_cast<size_t>(std::max(0.0, std::floor(dur_s / rate_s)));
        for (size_t step = 0; step < step_count; ++step) {
          if (!SeqStepActive(pattern, step, &euclid_pattern)) {
            continue;
          }
          if (rng.NextUnit() >= prob) {
            continue;
          }
          if (silence_prob > 0.0 && rng.NextUnit() < silence_prob) {
            continue;
          }

          double time_s = at_s + static_cast<double>(step) * rate_s;
          if (step % 2U == 1U) {
            time_s += (swing - 0.5) * rate_s;
          }
          const double jitter = Clamp(rng.Uniform(-jitter_s, jitter_s), -0.49 * rate_s, 0.49 * rate_s);
          time_s += jitter;
          time_s = Clamp(time_s, at_s, at_s + dur_s);

          const size_t pick_index = PickPitchIndex(pick, step, weights, rng);
          const ResolvedPitch pitch = ResolvePitchValue(pitch_values[pick_index % pitch_values.size()]);
          const uint64_t start_sample = static_cast<uint64_t>(std::llround(time_s * sample_rate));
          const auto event_len_samples = static_cast<uint64_t>(
              std::max<int64_t>(1, static_cast<int64_t>(std::llround(event_len_s * static_cast<double>(sample_rate)))));
          const uint64_t dur_samples = event_len_samples;
          AddSeqHit(&out.plays, &rolling_times, time_s, start_sample, dur_samples, seq.patch, velocity, pitch, max_per_minute);

          if (burst.count > 1 && rng.NextUnit() < burst.probability) {
            const double spread = (burst.spread_seconds > 0.0) ? burst.spread_seconds : (rate_s * 0.8);
            for (int i = 1; i < burst.count; ++i) {
              const double burst_t = time_s + spread * (static_cast<double>(i) / static_cast<double>(burst.count));
              const uint64_t burst_start = static_cast<uint64_t>(std::llround(burst_t * sample_rate));
              AddSeqHit(&out.plays, &rolling_times, burst_t, burst_start, dur_samples, seq.patch, velocity, pitch,
                        max_per_minute);
            }
          }
        }
        out.timeline_end =
            std::max(out.timeline_end, static_cast<uint64_t>(std::llround((at_s + dur_s + event_len_s) * sample_rate)));
      }
    }
  }

  std::sort(out.plays.begin(), out.plays.end(), [](const PlayOccurrence& a, const PlayOccurrence& b) {
    if (a.start_sample == b.start_sample) {
      return a.patch < b.patch;
    }
    return a.start_sample < b.start_sample;
  });
  return out;
}

struct PatchProgram {
  std::string filter_node_id;
  std::string gain_node_id;

  struct Osc {
    std::string type;
    double freq = 0.0;
    double pw = 0.5;
  };
  std::vector<Osc> oscillators;
  bool noise_white = false;
  bool sample_player = false;

  struct Env {
    bool enabled = false;
    double a = 0.01;
    double d = 0.1;
    double s = 0.8;
    double r = 0.2;
  } env;

  struct Filter {
    bool enabled = false;
    std::string mode = "lp";
    double cutoff_hz = 1500.0;
  } filter;

  double gain_db = -6.0;
  std::optional<aurora::lang::SendDefinition> send;
};

double NodeParamNumber(const std::map<std::string, aurora::lang::ParamValue>& params, const std::string& key, double fallback) {
  const auto it = params.find(key);
  if (it == params.end()) {
    return fallback;
  }
  return ValueToNumber(it->second, fallback);
}

std::string NodeParamText(const std::map<std::string, aurora::lang::ParamValue>& params, const std::string& key,
                          const std::string& fallback) {
  const auto it = params.find(key);
  if (it == params.end()) {
    return fallback;
  }
  return ValueToText(it->second);
}

PatchProgram BuildPatchProgram(const aurora::lang::PatchDefinition& patch) {
  PatchProgram program;
  program.send = patch.send;
  for (const auto& node : patch.graph.nodes) {
    if (StartsWith(node.type, "osc_")) {
      PatchProgram::Osc osc;
      osc.type = node.type;
      if (const auto it = node.params.find("freq"); it != node.params.end()) {
        if (it->second.kind == aurora::lang::ParamValue::Kind::kUnitNumber && it->second.unit_number_value.unit == "Hz") {
          osc.freq = it->second.unit_number_value.value;
        } else {
          osc.freq = ValueToNumber(it->second, 0.0);
        }
      }
      osc.pw = NodeParamNumber(node.params, "pw", 0.5);
      program.oscillators.push_back(osc);
    } else if (node.type == "noise_white" || node.type == "noise_pink") {
      program.noise_white = true;
    } else if (node.type == "sample_player" || node.type == "sample_slice") {
      program.sample_player = true;
    } else if (node.type == "env_adsr") {
      program.env.enabled = true;
      if (const auto it = node.params.find("a"); it != node.params.end()) {
        program.env.a = ValueToUnit(it->second).value;
      }
      if (const auto it = node.params.find("d"); it != node.params.end()) {
        program.env.d = ValueToUnit(it->second).value;
      }
      program.env.s = NodeParamNumber(node.params, "s", 0.8);
      if (const auto it = node.params.find("r"); it != node.params.end()) {
        program.env.r = ValueToUnit(it->second).value;
      }
    } else if (node.type == "svf" || node.type == "biquad") {
      program.filter.enabled = true;
      program.filter_node_id = node.id;
      program.filter.mode = NodeParamText(node.params, "mode", NodeParamText(node.params, "type", "lp"));
      if (const auto it = node.params.find("cutoff"); it != node.params.end()) {
        if (it->second.kind == aurora::lang::ParamValue::Kind::kUnitNumber && it->second.unit_number_value.unit == "Hz") {
          program.filter.cutoff_hz = it->second.unit_number_value.value;
        } else {
          program.filter.cutoff_hz = ValueToNumber(it->second, program.filter.cutoff_hz);
        }
      } else if (const auto fit = node.params.find("freq"); fit != node.params.end()) {
        if (fit->second.kind == aurora::lang::ParamValue::Kind::kUnitNumber && fit->second.unit_number_value.unit == "Hz") {
          program.filter.cutoff_hz = fit->second.unit_number_value.value;
        } else {
          program.filter.cutoff_hz = ValueToNumber(fit->second, program.filter.cutoff_hz);
        }
      }
    } else if (node.type == "gain") {
      program.gain_node_id = node.id;
      if (const auto it = node.params.find("gain"); it != node.params.end()) {
        if (it->second.kind == aurora::lang::ParamValue::Kind::kUnitNumber && it->second.unit_number_value.unit == "dB") {
          program.gain_db = it->second.unit_number_value.value;
        } else {
          program.gain_db = ValueToNumber(it->second, program.gain_db);
        }
      }
    }
  }
  if (program.oscillators.empty() && !program.noise_white && !program.sample_player) {
    PatchProgram::Osc fallback;
    fallback.type = "osc_sine";
    fallback.freq = 110.0;
    program.oscillators.push_back(fallback);
  }
  return program;
}

double EnvelopeValue(const PatchProgram::Env& env, double t, double note_dur) {
  if (!env.enabled) {
    return 1.0;
  }
  const double attack = std::max(0.0001, env.a);
  const double decay = std::max(0.0001, env.d);
  const double release = std::max(0.0001, env.r);

  if (t < attack) {
    return Clamp(t / attack, 0.0, 1.0);
  }
  if (t < attack + decay) {
    const double dt = (t - attack) / decay;
    return 1.0 + (env.s - 1.0) * dt;
  }
  if (t < note_dur) {
    return env.s;
  }
  if (t < note_dur + release) {
    const double rt = (t - note_dur) / release;
    return env.s * (1.0 - Clamp(rt, 0.0, 1.0));
  }
  return 0.0;
}

double OscSample(const PatchProgram::Osc& osc, double phase) {
  const double norm = phase - std::floor(phase);
  if (osc.type == "osc_sine") {
    return std::sin(2.0 * kPi * norm);
  }
  if (osc.type == "osc_saw_blep") {
    return 2.0 * norm - 1.0;
  }
  if (osc.type == "osc_tri_blep") {
    return 4.0 * std::fabs(norm - 0.5) - 1.0;
  }
  if (osc.type == "osc_pulse_blep") {
    return (norm < osc.pw) ? 1.0 : -1.0;
  }
  return std::sin(2.0 * kPi * norm);
}

void RenderPlayToStem(std::vector<float>* stem, const PlayOccurrence& play, const PatchProgram& program,
                      const std::map<std::string, AutomationLane>& automation, int sample_rate, uint64_t seed) {
  const size_t stem_samples = stem->size();
  if (play.start_sample >= stem_samples) {
    return;
  }
  const double base_gain = DbToLinear(program.gain_db) * play.velocity;

  for (size_t pitch_index = 0; pitch_index < play.pitches.size(); ++pitch_index) {
    const ResolvedPitch& pitch = play.pitches[pitch_index];
    std::vector<double> phases(program.oscillators.size(), 0.0);
    double filter_state = 0.0;
    PCG32 noise_rng(Hash64FromParts(seed, "voice", play.patch, std::to_string(play.start_sample),
                                    std::to_string(static_cast<int>(pitch_index))));

    const uint64_t fade_samples = static_cast<uint64_t>(std::llround(sample_rate * 0.005));
    for (uint64_t i = 0; i < play.dur_samples; ++i) {
      const uint64_t abs_sample = play.start_sample + i;
      if (abs_sample >= stem_samples) {
        break;
      }
      const double t = static_cast<double>(i) / sample_rate;
      const double note_dur = static_cast<double>(play.dur_samples) / sample_rate;
      double env = EnvelopeValue(program.env, t, note_dur);

      if (i < fade_samples && fade_samples > 0) {
        env *= static_cast<double>(i) / static_cast<double>(fade_samples);
      }
      if (play.dur_samples > fade_samples && i > play.dur_samples - fade_samples && fade_samples > 0) {
        const uint64_t rem = play.dur_samples - i;
        env *= static_cast<double>(rem) / static_cast<double>(fade_samples);
      }

      double sample = 0.0;
      for (size_t osc_idx = 0; osc_idx < program.oscillators.size(); ++osc_idx) {
        const auto& osc = program.oscillators[osc_idx];
        const double freq = osc.freq > 0.0 ? osc.freq : pitch.frequency;
        phases[osc_idx] += freq / static_cast<double>(sample_rate);
        sample += OscSample(osc, phases[osc_idx]);
      }
      if (program.noise_white) {
        sample += noise_rng.Uniform(-1.0, 1.0) * 0.25;
      }
      if (program.sample_player) {
        const double decay = std::exp(-t * 20.0);
        sample += noise_rng.Uniform(-1.0, 1.0) * decay * 0.6;
      }

      if (!program.oscillators.empty()) {
        sample /= static_cast<double>(program.oscillators.size());
      }

      double cutoff = program.filter.cutoff_hz;
      if (!program.filter_node_id.empty()) {
        const std::string key = program.filter_node_id + ".cutoff";
        if (const auto it = automation.find(key); it != automation.end()) {
          cutoff = std::max(20.0, EvaluateLane(it->second, abs_sample));
        }
      }

      if (program.filter.enabled) {
        const double alpha = Clamp(1.0 - std::exp(-2.0 * kPi * cutoff / static_cast<double>(sample_rate)), 0.0, 1.0);
        filter_state += alpha * (sample - filter_state);
        if (program.filter.mode == "hp") {
          sample = sample - filter_state;
        } else {
          sample = filter_state;
        }
      }

      double gain = base_gain;
      if (!program.gain_node_id.empty()) {
        const std::string key = program.gain_node_id + ".gain";
        if (const auto it = automation.find(key); it != automation.end()) {
          gain = DbToLinear(EvaluateLane(it->second, abs_sample)) * play.velocity;
        }
      }
      (*stem)[abs_sample] += static_cast<float>(sample * env * gain);
    }
  }
}

struct BusProgram {
  bool has_reverb = false;
  double mix = 0.3;
  double decay = 4.0;
  double predelay_seconds = 0.02;
};

BusProgram BuildBusProgram(const aurora::lang::BusDefinition& bus) {
  BusProgram program;
  for (const auto& node : bus.graph.nodes) {
    if (node.type == "reverb_algo") {
      program.has_reverb = true;
      program.mix = Clamp(NodeParamNumber(node.params, "mix", program.mix), 0.0, 1.0);
      if (const auto it = node.params.find("decay"); it != node.params.end()) {
        program.decay = std::max(0.1, ValueToUnit(it->second).value);
      }
      if (const auto it = node.params.find("predelay"); it != node.params.end()) {
        program.predelay_seconds = std::max(0.0, ValueToUnit(it->second).value);
      }
    } else if (node.type == "delay") {
      program.has_reverb = true;
      if (const auto it = node.params.find("time"); it != node.params.end()) {
        program.predelay_seconds = std::max(0.001, ValueToUnit(it->second).value);
      }
      program.mix = Clamp(NodeParamNumber(node.params, "mix", 0.35), 0.0, 1.0);
      program.decay = std::max(0.1, NodeParamNumber(node.params, "fb", 0.5) * 8.0);
    }
  }
  return program;
}

void ProcessBusStem(std::vector<float>* buffer, const BusProgram& program, int sample_rate) {
  if (!program.has_reverb || buffer->empty()) {
    return;
  }
  const size_t delay_size = std::max<size_t>(1, static_cast<size_t>(std::llround(program.predelay_seconds * sample_rate)));
  std::vector<float> delay_line(delay_size, 0.0f);
  size_t idx = 0;
  const double feedback = Clamp(1.0 - std::exp(-1.0 / (program.decay * sample_rate * 0.25)), 0.05, 0.98);
  for (size_t n = 0; n < buffer->size(); ++n) {
    const float dry = (*buffer)[n];
    const float wet = delay_line[idx];
    delay_line[idx] = static_cast<float>(dry + wet * feedback);
    idx = (idx + 1) % delay_line.size();
    (*buffer)[n] = static_cast<float>(dry * (1.0 - program.mix) + wet * program.mix);
  }
}

int ParamToCC(const std::string& key) {
  if (EndsWith(key, ".cutoff")) {
    return 74;
  }
  if (EndsWith(key, ".gain")) {
    return 7;
  }
  return 1;
}

uint8_t ParamValueToCC(const std::string& key, double value) {
  if (EndsWith(key, ".cutoff")) {
    const double clamped = Clamp(value, 20.0, 20000.0);
    const double norm = std::log(clamped / 20.0) / std::log(20000.0 / 20.0);
    return static_cast<uint8_t>(std::llround(Clamp(norm, 0.0, 1.0) * 127.0));
  }
  if (EndsWith(key, ".gain")) {
    const double norm = (Clamp(value, -60.0, 12.0) + 60.0) / 72.0;
    return static_cast<uint8_t>(std::llround(norm * 127.0));
  }
  return static_cast<uint8_t>(std::llround(Clamp(value, 0.0, 1.0) * 127.0));
}

}  // namespace

RenderResult Renderer::Render(const aurora::lang::AuroraFile& file, const RenderOptions& options) const {
  RenderResult result;
  result.metadata.sample_rate = options.sample_rate_override > 0 ? options.sample_rate_override : file.globals.sr;
  result.metadata.block_size = file.globals.block;

  const TempoMap tempo_map = BuildTempoMap(file.globals);
  ExpansionResult expanded = ExpandScore(file, tempo_map, result.metadata.sample_rate, options.seed);

  const uint64_t tail_samples = static_cast<uint64_t>(
      std::llround(file.globals.tail_policy.fixed_seconds * static_cast<double>(result.metadata.sample_rate)));
  const uint64_t total_samples =
      RoundUpToBlock(std::max<uint64_t>(expanded.timeline_end, 1) + tail_samples, result.metadata.block_size);
  result.metadata.total_samples = total_samples;
  result.metadata.duration_seconds = static_cast<double>(total_samples) / static_cast<double>(result.metadata.sample_rate);

  std::map<std::string, std::vector<float>> patch_buffers;
  std::map<std::string, PatchProgram> patch_programs;
  for (const auto& patch : file.patches) {
    patch_buffers[patch.name] = std::vector<float>(static_cast<size_t>(total_samples), 0.0f);
    patch_programs[patch.name] = BuildPatchProgram(patch);
  }

  for (const auto& play : expanded.plays) {
    const auto patch_it = patch_programs.find(play.patch);
    const auto stem_it = patch_buffers.find(play.patch);
    if (patch_it == patch_programs.end() || stem_it == patch_buffers.end()) {
      result.warnings.push_back("Event references unknown patch '" + play.patch + "'.");
      continue;
    }
    const auto auto_it = expanded.automation.find(play.patch);
    const std::map<std::string, AutomationLane> empty_auto;
    const auto& automation = (auto_it != expanded.automation.end()) ? auto_it->second : empty_auto;
    RenderPlayToStem(&stem_it->second, play, patch_it->second, automation, result.metadata.sample_rate, options.seed);
  }

  std::map<std::string, std::vector<float>> bus_buffers;
  std::map<std::string, BusProgram> bus_programs;
  for (const auto& bus : file.buses) {
    bus_buffers[bus.name] = std::vector<float>(static_cast<size_t>(total_samples), 0.0f);
    bus_programs[bus.name] = BuildBusProgram(bus);
  }

  for (const auto& patch : file.patches) {
    const auto program_it = patch_programs.find(patch.name);
    if (program_it == patch_programs.end()) {
      continue;
    }
    const auto& program = program_it->second;
    if (!program.send.has_value() || program.send->bus.empty()) {
      continue;
    }
    const auto bus_it = bus_buffers.find(program.send->bus);
    const auto source_it = patch_buffers.find(patch.name);
    if (bus_it == bus_buffers.end() || source_it == patch_buffers.end()) {
      continue;
    }
    const float send_gain = static_cast<float>(DbToLinear(program.send->amount_db));
    for (size_t i = 0; i < source_it->second.size(); ++i) {
      bus_it->second[i] += source_it->second[i] * send_gain;
    }
  }

  for (auto& [bus_name, buffer] : bus_buffers) {
    const auto program_it = bus_programs.find(bus_name);
    if (program_it != bus_programs.end()) {
      ProcessBusStem(&buffer, program_it->second, result.metadata.sample_rate);
    }
  }

  result.patch_stems.reserve(file.patches.size());
  for (const auto& patch : file.patches) {
    AudioStem stem;
    stem.channels = 1;
    stem.name = patch.out_stem.empty() ? patch.name : patch.out_stem;
    stem.samples = patch_buffers[patch.name];
    result.patch_stems.push_back(std::move(stem));
  }

  result.bus_stems.reserve(file.buses.size());
  for (const auto& bus : file.buses) {
    AudioStem stem;
    stem.channels = 1;
    stem.name = bus.out_stem.empty() ? bus.name : bus.out_stem;
    stem.samples = bus_buffers[bus.name];
    result.bus_stems.push_back(std::move(stem));
  }

  result.master.name = "master";
  result.master.channels = 1;
  result.master.samples.assign(static_cast<size_t>(total_samples), 0.0f);
  for (const auto& stem : result.patch_stems) {
    for (size_t i = 0; i < stem.samples.size(); ++i) {
      result.master.samples[i] += stem.samples[i];
    }
  }
  for (const auto& stem : result.bus_stems) {
    for (size_t i = 0; i < stem.samples.size(); ++i) {
      result.master.samples[i] += stem.samples[i];
    }
  }
  for (float& sample : result.master.samples) {
    sample = static_cast<float>(std::tanh(sample));
  }

  std::map<std::string, MidiTrackData> midi_by_patch;
  for (const auto& patch : file.patches) {
    MidiTrackData track;
    track.name = patch.name;
    midi_by_patch[patch.name] = std::move(track);
  }
  for (const auto& play : expanded.plays) {
    auto it = midi_by_patch.find(play.patch);
    if (it == midi_by_patch.end()) {
      continue;
    }
    const int channel = static_cast<int>(std::distance(midi_by_patch.begin(), it)) % 16;
    for (const auto& pitch : play.pitches) {
      MidiNote note;
      note.channel = channel;
      note.note = std::clamp(pitch.midi, 0, 127);
      note.velocity = static_cast<uint8_t>(std::llround(Clamp(play.velocity, 0.0, 1.0) * 127.0));
      note.start_sample = std::min(play.start_sample, total_samples);
      note.end_sample = std::min(play.start_sample + play.dur_samples, total_samples);
      if (note.end_sample <= note.start_sample) {
        note.end_sample = note.start_sample + 1;
      }
      it->second.notes.push_back(note);
    }
  }

  for (const auto& [patch_name, lanes] : expanded.automation) {
    auto it = midi_by_patch.find(patch_name);
    if (it == midi_by_patch.end()) {
      continue;
    }
    const int channel = static_cast<int>(std::distance(midi_by_patch.begin(), it)) % 16;
    for (const auto& [key, lane] : lanes) {
      const int cc = ParamToCC(key);
      for (uint64_t sample = 0; sample < total_samples; sample += static_cast<uint64_t>(result.metadata.block_size)) {
        MidiCCPoint point;
        point.channel = channel;
        point.cc = cc;
        point.sample = sample;
        point.value = ParamValueToCC(key, EvaluateLane(lane, sample));
        it->second.ccs.push_back(point);
      }
    }
  }

  for (auto& [_, track] : midi_by_patch) {
    std::sort(track.notes.begin(), track.notes.end(), [](const MidiNote& a, const MidiNote& b) {
      if (a.start_sample == b.start_sample) {
        return a.note < b.note;
      }
      return a.start_sample < b.start_sample;
    });
    result.midi_tracks.push_back(std::move(track));
  }

  return result;
}

}  // namespace aurora::core
