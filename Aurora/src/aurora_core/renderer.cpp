#include "aurora/core/renderer.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
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

double UnitLiteralToSeconds(const aurora::lang::UnitNumber& value) {
  if (value.unit.empty() || value.unit == "s") {
    return value.value;
  }
  if (value.unit == "ms") {
    return value.value * 0.001;
  }
  if (value.unit == "min") {
    return value.value * 60.0;
  }
  if (value.unit == "h") {
    return value.value * 3600.0;
  }
  return value.value;
}

bool StartsWith(const std::string& value, const std::string& prefix) {
  return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

bool EndsWith(const std::string& value, const std::string& suffix) {
  return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::pair<std::string, std::string> SplitNodePort(const std::string& endpoint) {
  const size_t dot = endpoint.find('.');
  if (dot == std::string::npos) {
    return {endpoint, ""};
  }
  return {endpoint.substr(0, dot), endpoint.substr(dot + 1)};
}

std::vector<std::string> SplitByDot(const std::string& value) {
  std::vector<std::string> parts;
  size_t start = 0;
  while (start <= value.size()) {
    const size_t dot = value.find('.', start);
    if (dot == std::string::npos) {
      parts.push_back(value.substr(start));
      break;
    }
    parts.push_back(value.substr(start, dot - start));
    start = dot + 1;
  }
  return parts;
}

std::optional<std::pair<std::string, size_t>> ResolvePatchRefFromTarget(const std::vector<std::string>& parts, size_t patch_index,
                                                                        const std::set<std::string>& patch_names) {
  if (patch_index >= parts.size()) {
    return std::nullopt;
  }
  const std::string single = parts[patch_index];
  if (patch_names.contains(single)) {
    return std::make_pair(single, patch_index + 1U);
  }
  if (patch_index + 1U < parts.size()) {
    const std::string dotted = single + "." + parts[patch_index + 1U];
    if (patch_names.contains(dotted)) {
      return std::make_pair(dotted, patch_index + 2U);
    }
  }
  return std::nullopt;
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
  std::map<std::string, aurora::lang::ParamValue> params;
  uint64_t section_start_sample = 0;
  uint64_t section_end_sample = 0;
  uint64_t xfade_in_samples = 0;
  uint64_t xfade_out_samples = 0;
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

void FlattenEventParamsInto(const std::map<std::string, aurora::lang::ParamValue>& input, const std::string& prefix,
                            std::map<std::string, aurora::lang::ParamValue>* out) {
  for (const auto& [key, value] : input) {
    const std::string full_key = prefix.empty() ? key : (prefix + "." + key);
    if (value.kind == aurora::lang::ParamValue::Kind::kObject) {
      FlattenEventParamsInto(value.object_values, full_key, out);
      continue;
    }
    (*out)[full_key] = value;
  }
}

std::map<std::string, aurora::lang::ParamValue> FlattenEventParams(
    const std::map<std::string, aurora::lang::ParamValue>& input) {
  std::map<std::string, aurora::lang::ParamValue> out;
  FlattenEventParamsInto(input, "", &out);
  return out;
}

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

double ParamAsSeconds(const aurora::lang::ParamValue& value, const TempoMap& tempo_map, double anchor_seconds = 0.0) {
  return OffsetSecondsFrom(anchor_seconds, ValueToUnit(value), tempo_map);
}

double DirectiveSecondsOr(const std::map<std::string, aurora::lang::ParamValue>& directives, const std::string& key,
                          double fallback_seconds, const TempoMap& tempo_map, double anchor_seconds = 0.0) {
  const auto it = directives.find(key);
  if (it == directives.end()) {
    return fallback_seconds;
  }
  return std::max(0.0, ParamAsSeconds(it->second, tempo_map, anchor_seconds));
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

BurstConfig ParseBurst(const std::map<std::string, aurora::lang::ParamValue>& fields, const TempoMap& tempo_map,
                       double anchor_seconds) {
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
    cfg.spread_seconds = ParamAsSeconds(s->second, tempo_map, anchor_seconds);
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
               uint64_t dur_samples, const std::string& patch, double velocity, const ResolvedPitch& pitch,
               const std::map<std::string, aurora::lang::ParamValue>& params, uint64_t section_start_sample,
               uint64_t section_end_sample, uint64_t xfade_in_samples, uint64_t xfade_out_samples, int max_events_per_minute) {
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
  occurrence.params = params;
  occurrence.section_start_sample = section_start_sample;
  occurrence.section_end_sample = section_end_sample;
  occurrence.xfade_in_samples = xfade_in_samples;
  occurrence.xfade_out_samples = xfade_out_samples;
  plays->push_back(std::move(occurrence));
}

ExpansionResult ExpandScore(const aurora::lang::AuroraFile& file, const TempoMap& tempo_map, int sample_rate, uint64_t seed) {
  ExpansionResult out;
  std::set<std::string> patch_names;
  for (const auto& patch : file.patches) {
    patch_names.insert(patch.name);
  }

  for (const auto& section : file.sections) {
    const SectionConstraintState constraints = ResolveSectionConstraints(section);
    const SeqDensity density = DensityFromPreset(constraints.density);
    const double silence_prob = SilenceProbability(constraints.silence);

    const double section_start_s = ToSeconds(section.at, tempo_map);
    const uint64_t section_start = static_cast<uint64_t>(std::llround(section_start_s * static_cast<double>(sample_rate)));
    const double section_dur_s = OffsetSecondsFrom(section_start_s, section.dur, tempo_map);
    const uint64_t section_dur = static_cast<uint64_t>(std::llround(section_dur_s * static_cast<double>(sample_rate)));
    const uint64_t section_end = section_start + section_dur;
    const double xfade_both_s = DirectiveSecondsOr(section.directives, "xfade", 0.0, tempo_map, section_start_s);
    const double xfade_in_s = DirectiveSecondsOr(section.directives, "xfade_in", xfade_both_s, tempo_map, section_start_s);
    const double xfade_out_s = DirectiveSecondsOr(section.directives, "xfade_out", xfade_both_s, tempo_map, section_start_s);
    const uint64_t xfade_in_samples =
        static_cast<uint64_t>(std::llround(xfade_in_s * static_cast<double>(sample_rate)));
    const uint64_t xfade_out_samples =
        static_cast<uint64_t>(std::llround(xfade_out_s * static_cast<double>(sample_rate)));
    out.timeline_end = std::max(out.timeline_end, section_start + section_dur);
    std::map<std::string, std::map<std::string, aurora::lang::ParamValue>> section_set_params_by_patch;

    for (const auto& event : section.events) {
      if (std::holds_alternative<aurora::lang::SetEvent>(event)) {
        const auto& set = std::get<aurora::lang::SetEvent>(event);
        const auto parts = SplitByDot(set.target);
        if (!parts.empty() && parts[0] == "patch") {
          const auto patch_ref = ResolvePatchRefFromTarget(parts, 1U, patch_names);
          if (!patch_ref.has_value() || parts.size() < patch_ref->second + 2U) {
            continue;
          }
          const std::string patch_name = patch_ref->first;
          std::string key = parts[patch_ref->second];
          for (size_t i = patch_ref->second + 1U; i < parts.size(); ++i) {
            key += "." + parts[i];
          }
          section_set_params_by_patch[patch_name][key] = set.value;
        }
        continue;
      }

      if (std::holds_alternative<aurora::lang::PlayEvent>(event)) {
        const auto& play = std::get<aurora::lang::PlayEvent>(event);
        PlayOccurrence occurrence;
        occurrence.patch = play.patch;
        const double play_start_s = section_start_s + OffsetSecondsFrom(section_start_s, play.at, tempo_map);
        occurrence.start_sample = static_cast<uint64_t>(std::llround(play_start_s * static_cast<double>(sample_rate)));
        const double play_dur_s = OffsetSecondsFrom(play_start_s, play.dur, tempo_map);
        occurrence.dur_samples =
            std::max<uint64_t>(1, static_cast<uint64_t>(std::llround(play_dur_s * static_cast<double>(sample_rate))));
        occurrence.velocity = Clamp(play.vel, 0.0, 1.5);
        for (const auto& p : play.pitch_values) {
          occurrence.pitches.push_back(ResolvePitchValue(p));
        }
        if (occurrence.pitches.empty()) {
          occurrence.pitches.push_back(ResolvePitchValue(aurora::lang::ParamValue::Identifier("C4")));
        }
        if (const auto set_it = section_set_params_by_patch.find(play.patch); set_it != section_set_params_by_patch.end()) {
          occurrence.params = set_it->second;
        }
        const auto play_params = FlattenEventParams(play.params);
        for (const auto& [k, v] : play_params) {
          occurrence.params[k] = v;
        }
        occurrence.section_start_sample = section_start;
        occurrence.section_end_sample = section_end;
        occurrence.xfade_in_samples = xfade_in_samples;
        occurrence.xfade_out_samples = xfade_out_samples;
        out.timeline_end = std::max(out.timeline_end, occurrence.start_sample + occurrence.dur_samples);
        out.plays.push_back(std::move(occurrence));
        continue;
      }

      if (std::holds_alternative<aurora::lang::AutomateEvent>(event)) {
        const auto& automate = std::get<aurora::lang::AutomateEvent>(event);
        const auto parts = SplitByDot(automate.target);
        if (parts.empty() || parts[0] != "patch") {
          continue;
        }
        const auto patch_ref = ResolvePatchRefFromTarget(parts, 1U, patch_names);
        if (!patch_ref.has_value() || parts.size() < patch_ref->second + 2U) {
          continue;
        }
        const std::string patch_name = patch_ref->first;
        const std::string key = parts[patch_ref->second] + "." + parts[patch_ref->second + 1U];
        AutomationLane lane;
        lane.curve = automate.curve;
        for (const auto& [time, value] : automate.points) {
          const double point_s = section_start_s + OffsetSecondsFrom(section_start_s, time, tempo_map);
          const uint64_t sample = static_cast<uint64_t>(std::llround(point_s * static_cast<double>(sample_rate)));
          lane.points.push_back({sample, ValueToNumber(value, 0.0)});
        }
        std::sort(lane.points.begin(), lane.points.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        out.automation[patch_name][key] = std::move(lane);
        continue;
      }
      if (std::holds_alternative<aurora::lang::SeqEvent>(event)) {
        const auto& seq = std::get<aurora::lang::SeqEvent>(event);
        const auto& fields = seq.fields;

        double at_s = section_start_s;
        double dur_s = section_dur_s;
        if (const auto it = fields.find("at"); it != fields.end()) {
          at_s = section_start_s + OffsetSecondsFrom(section_start_s, ValueToUnit(it->second), tempo_map);
        }
        if (const auto it = fields.find("dur"); it != fields.end()) {
          dur_s = OffsetSecondsFrom(at_s, ValueToUnit(it->second), tempo_map);
        }

        double rate_s = 1.0;
        if (const auto it = fields.find("rate"); it != fields.end()) {
          rate_s = OffsetSecondsFrom(at_s, ValueToUnit(it->second), tempo_map);
        }
        rate_s = std::max(0.001, rate_s * density.rate_multiplier);
        const double prob = Clamp(FieldNumberOr(fields, "prob", 1.0) * density.prob_multiplier, 0.0, 1.0);
        const double velocity = Clamp(FieldNumberOr(fields, "vel", 0.8), 0.0, 1.0);
        double jitter_s = 0.0;
        if (const auto it = fields.find("jitter"); it != fields.end()) {
          jitter_s = std::max(0.0, OffsetSecondsFrom(at_s, ValueToUnit(it->second), tempo_map));
        }
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
        std::map<std::string, aurora::lang::ParamValue> seq_event_params;
        if (const auto set_it = section_set_params_by_patch.find(seq.patch); set_it != section_set_params_by_patch.end()) {
          seq_event_params = set_it->second;
        }
        if (const auto params_it = fields.find("params");
            params_it != fields.end() && params_it->second.kind == aurora::lang::ParamValue::Kind::kObject) {
          const auto seq_params = FlattenEventParams(params_it->second.object_values);
          for (const auto& [k, v] : seq_params) {
            seq_event_params[k] = v;
          }
        }

        const BurstConfig burst = ParseBurst(fields, tempo_map, at_s);

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
          AddSeqHit(&out.plays, &rolling_times, time_s, start_sample, dur_samples, seq.patch, velocity, pitch, seq_event_params,
                    section_start, section_end, xfade_in_samples, xfade_out_samples, max_per_minute);

          if (burst.count > 1 && rng.NextUnit() < burst.probability) {
            const double spread = (burst.spread_seconds > 0.0) ? burst.spread_seconds : (rate_s * 0.8);
            for (int i = 1; i < burst.count; ++i) {
              const double burst_t = time_s + spread * (static_cast<double>(i) / static_cast<double>(burst.count));
              const uint64_t burst_start = static_cast<uint64_t>(std::llround(burst_t * sample_rate));
              AddSeqHit(&out.plays, &rolling_times, burst_t, burst_start, dur_samples, seq.patch, velocity, pitch, seq_event_params,
                        section_start, section_end, xfade_in_samples, xfade_out_samples, max_per_minute);
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

void ApplyMonoPolicies(const aurora::lang::AuroraFile& file, std::vector<PlayOccurrence>* plays) {
  if (plays == nullptr || plays->empty()) {
    return;
  }
  std::map<std::string, const aurora::lang::PatchDefinition*> patch_by_name;
  for (const auto& patch : file.patches) {
    patch_by_name[patch.name] = &patch;
  }

  std::map<std::string, std::vector<size_t>> by_patch;
  for (size_t i = 0; i < plays->size(); ++i) {
    by_patch[(*plays)[i].patch].push_back(i);
  }

  for (const auto& [patch_name, indices] : by_patch) {
    const auto patch_it = patch_by_name.find(patch_name);
    if (patch_it == patch_by_name.end()) {
      continue;
    }
    const auto& patch = *patch_it->second;
    if (!patch.mono) {
      continue;
    }
    if (indices.empty()) {
      continue;
    }
    auto primary_midi = [&](const PlayOccurrence& p) {
      if (!p.pitches.empty()) {
        return p.pitches.front().midi;
      }
      return 69;
    };
    auto keep_current_on_overlap = [&](const PlayOccurrence& current, const PlayOccurrence& candidate) {
      const std::string mode = patch.voice_steal;
      if (mode == "first") {
        return true;
      }
      if (mode == "highest") {
        return primary_midi(current) >= primary_midi(candidate);
      }
      if (mode == "lowest") {
        return primary_midi(current) <= primary_midi(candidate);
      }
      // Default mono priority: most recent note wins (`last`/`oldest`/unknown).
      return false;
    };

    size_t active_idx = indices.front();
    bool first_note = true;
    for (size_t n = 0; n < indices.size(); ++n) {
      const size_t cur_idx = indices[n];
      auto& cur = (*plays)[cur_idx];
      if (cur.dur_samples == 0) {
        continue;
      }

      if (first_note) {
        first_note = false;
        if (patch.retrig == "never") {
          cur.params["__env_no_attack"] = aurora::lang::ParamValue::Bool(false);
        }
        active_idx = cur_idx;
        continue;
      }

      auto& active = (*plays)[active_idx];
      const uint64_t active_end = active.start_sample + active.dur_samples;
      const bool overlap = cur.start_sample < active_end;

      if (overlap) {
        if (keep_current_on_overlap(active, cur)) {
          cur.dur_samples = 0;
          continue;
        }
        const uint64_t new_active_dur =
            (cur.start_sample > active.start_sample) ? (cur.start_sample - active.start_sample) : 1;
        active.dur_samples = std::max<uint64_t>(1, new_active_dur);
        if ((patch.legato && patch.retrig == "legato") || patch.retrig == "never") {
          cur.params["__env_no_attack"] = aurora::lang::ParamValue::Bool(true);
        }
        active_idx = cur_idx;
        continue;
      }

      if (patch.retrig == "never") {
        cur.params["__env_no_attack"] = aurora::lang::ParamValue::Bool(true);
      }
      active_idx = cur_idx;
    }
  }

  plays->erase(std::remove_if(plays->begin(), plays->end(), [](const PlayOccurrence& p) { return p.dur_samples == 0; }),
               plays->end());
}

struct PatchProgram {
  bool mono = false;
  bool legato = false;
  std::string retrig = "always";

  std::string filter_node_id;
  std::string gain_node_id;
  std::string env_node_id;

  struct Osc {
    std::string node_id;
    std::string type;
    double pw = 0.5;
    double detune_semitones = 0.0;
    std::optional<double> freq_hz;
  };
  std::vector<Osc> oscillators;
  bool noise_white = false;
  bool sample_player = false;

  struct Env {
    enum class Mode { kAdsr, kAd, kAr };
    bool enabled = false;
    Mode mode = Mode::kAdsr;
    double a = 0.01;
    double d = 0.1;
    double s = 0.8;
    double r = 0.2;
  } env;

  struct Filter {
    bool enabled = false;
    std::string mode = "lp";
    double cutoff_hz = 1500.0;
    double q = 0.707;
    double res = 0.0;
    double drive = 1.0;
    std::string drive_pos = "pre";
    int slope_db = 12;
    double keytrack = 0.0;
    double env_amt = 0.0;
  } filter;

  struct Binaural {
    bool enabled = false;
    double shift_hz = 0.0;
    double mix = 1.0;
  } binaural;

  struct VoiceSpread {
    bool enabled = false;
    double pan = 0.0;
    double detune_semitones = 0.0;
    double delay_seconds = 0.0;
  } voice_spread;

  struct StagePosition {
    bool enabled = false;
    double pan = 0.0;
    double depth = 0.0;
  } stage_position;

  struct Lfo {
    std::string node_id;
    std::string shape = "sine";
    double rate_hz = 1.0;
    double depth = 1.0;
    double pw = 0.5;
    double phase = 0.0;
    bool unipolar = false;
  };
  std::vector<Lfo> lfos;

  struct Vca {
    bool enabled = false;
    std::string node_id;
    double gain = 1.0;
    double cv = 1.0;
    std::string curve = "linear";
    double curve_amount = 2.0;
  } vca;

  struct RingMod {
    bool enabled = false;
    std::string node_id;
    std::string shape = "sine";
    std::string mode = "balanced";
    double freq_hz = 35.0;
    double pw = 0.5;
    double depth = 1.0;
    double mix = 1.0;
    double bias = 0.0;
  } ring_mod;

  struct Softclip {
    bool enabled = false;
    std::string node_id;
    double drive = 1.0;
    double mix = 1.0;
    double bias = 0.0;
  } softclip;

  struct AudioMix {
    bool enabled = false;
    std::string node_id;
    double gain = 1.0;
    double mix = 1.0;
    double bias = 0.0;
  } audio_mix;

  struct Comb {
    bool enabled = false;
    std::string node_id;
    double time_seconds = 0.03;
    double feedback = 0.55;
    double mix = 0.4;
    double damp = 0.3;
  } comb;

  struct Pan {
    bool enabled = false;
    std::string node_id;
    double pos = 0.0;
    std::string law = "equal_power";
    double width = 1.0;
  } pan;

  struct StereoWidth {
    bool enabled = false;
    std::string node_id;
    double width = 1.0;
    bool saturate = false;
  } stereo_width;

  struct Depth {
    bool enabled = false;
    std::string node_id;
    double distance = 0.0;
    double air_absorption = 0.7;
    double early_reflection_send = 0.25;
  } depth;

  struct Decorrelate {
    bool enabled = false;
    std::string node_id;
    double time_seconds = 0.0008;
    double mix = 1.0;
  } decorrelate;

  struct ModRoute {
    enum class SourceKind { kEnv, kLfo, kCvNode };
    enum class Op { kSet, kAdd, kMul };

    SourceKind source_kind = SourceKind::kEnv;
    std::string source_node_id;
    std::string target_key;
    std::string rate = "control";
    Op op = Op::kAdd;
    bool use_range = false;
    double min = 0.0;
    double max = 1.0;
    double scale = 1.0;
    double offset = 0.0;
    bool invert = false;
    double bias = 0.0;
    std::string curve = "linear";
  };
  std::vector<ModRoute> mod_routes;

  struct CvInputRoute {
    std::string source_node_id;
    std::string to_port;
  };

  struct CvNode {
    std::string node_id;
    std::string type;
    std::string op = "and";
    double scale = 1.0;
    double offset = 0.0;
    double a = 1.0;
    double b = 1.0;
    double bias = 0.0;
    double rise_seconds = 0.01;
    double fall_seconds = 0.01;
    double min = 0.0;
    double max = 1.0;
    double threshold = 0.5;
    double hysteresis = 0.0;
    double high = 1.0;
    double low = 0.0;
    std::vector<CvInputRoute> inputs;
  };
  std::vector<CvNode> cv_nodes;

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

double ParseDetuneSemitones(const aurora::lang::ParamValue& value) {
  if (value.kind == aurora::lang::ParamValue::Kind::kUnitNumber) {
    const std::string& unit = value.unit_number_value.unit;
    if (unit == "c") {
      return value.unit_number_value.value / 100.0;
    }
    if (unit == "st") {
      return value.unit_number_value.value;
    }
    return 0.0;
  }
  if (value.kind == aurora::lang::ParamValue::Kind::kNumber) {
    // Bare numeric detune is interpreted as cents.
    return value.number_value / 100.0;
  }
  return 0.0;
}

bool IsCvNodeType(const std::string& node_type) {
  return node_type == "cv_scale" || node_type == "cv_offset" || node_type == "cv_mix" || node_type == "cv_slew" ||
         node_type == "cv_clip" || node_type == "cv_invert" || node_type == "cv_sample_hold" || node_type == "cv_cmp" ||
         node_type == "cv_logic";
}

bool NodeIsControlSource(const std::string& node_type) {
  return node_type == "env_adsr" || node_type == "env_ad" || node_type == "env_ar" || node_type == "lfo" ||
         IsCvNodeType(node_type);
}

PatchProgram::ModRoute::Op ParseModOp(const std::map<std::string, aurora::lang::ParamValue>& map_obj,
                                      const std::string& target_port) {
  const auto type_it = map_obj.find("type");
  if (type_it == map_obj.end()) {
    if (target_port == "cv") {
      return PatchProgram::ModRoute::Op::kSet;
    }
    return PatchProgram::ModRoute::Op::kAdd;
  }
  const std::string type = ValueToText(type_it->second);
  if (type == "set" || type == "range" || type == "db" || type == "hz" || type == "lin") {
    return PatchProgram::ModRoute::Op::kSet;
  }
  if (type == "mul") {
    return PatchProgram::ModRoute::Op::kMul;
  }
  return PatchProgram::ModRoute::Op::kAdd;
}

double LfoWave(const std::string& shape, double phase, double pw) {
  const double p = phase - std::floor(phase);
  if (shape == "triangle" || shape == "tri") {
    return 4.0 * std::fabs(p - 0.5) - 1.0;
  }
  if (shape == "saw") {
    return 2.0 * p - 1.0;
  }
  if (shape == "square" || shape == "pulse") {
    return (p < Clamp(pw, 0.01, 0.99)) ? 1.0 : -1.0;
  }
  return std::sin(2.0 * kPi * p);
}

PatchProgram BuildPatchProgram(const aurora::lang::PatchDefinition& patch) {
  enum class PortKind { kUnknown, kAudioIn, kControlIn, kAudioOut, kControlOut };
  const auto classify_port = [](const std::string& node_type, const std::string& port_name, bool is_source) -> PortKind {
    if (is_source) {
      if (port_name.empty() || port_name == "out") {
        if (NodeIsControlSource(node_type)) {
          return PortKind::kControlOut;
        }
        return PortKind::kAudioOut;
      }
      return NodeIsControlSource(node_type) ? PortKind::kControlOut : PortKind::kAudioOut;
    }
    if (StartsWith(port_name, "in")) {
      if (IsCvNodeType(node_type)) {
        return PortKind::kControlIn;
      }
      return PortKind::kAudioIn;
    }
    if (port_name == "gate" || port_name == "trigger" || port_name == "cv") {
      return PortKind::kControlIn;
    }
    return PortKind::kControlIn;
  };

  PatchProgram program;
  program.mono = patch.mono;
  program.legato = patch.legato;
  program.retrig = patch.retrig;
  program.send = patch.send;
  program.binaural.enabled = patch.binaural.enabled;
  program.binaural.shift_hz = patch.binaural.shift_hz;
  program.binaural.mix = Clamp(patch.binaural.mix, 0.0, 1.0);
  program.voice_spread.enabled = patch.voice_spread.enabled;
  program.voice_spread.pan = Clamp(patch.voice_spread.pan, 0.0, 1.0);
  program.voice_spread.detune_semitones = patch.voice_spread.detune_semitones;
  program.voice_spread.delay_seconds = std::max(0.0, patch.voice_spread.delay_seconds);
  program.stage_position.enabled = patch.stage_position.enabled;
  program.stage_position.pan = Clamp(patch.stage_position.pan, -1.0, 1.0);
  program.stage_position.depth = Clamp(patch.stage_position.depth, 0.0, 1.0);
  if (program.stage_position.enabled) {
    program.pan.pos = program.stage_position.pan;
    program.depth.distance = program.stage_position.depth;
  }
  std::map<std::string, std::string> node_types;
  for (const auto& node : patch.graph.nodes) {
    node_types[node.id] = node.type;
    if (StartsWith(node.type, "osc_")) {
      PatchProgram::Osc osc;
      osc.node_id = node.id;
      osc.type = node.type;
      osc.pw = NodeParamNumber(node.params, "pw", 0.5);
      if (const auto it = node.params.find("freq"); it != node.params.end()) {
        if (it->second.kind == aurora::lang::ParamValue::Kind::kUnitNumber && it->second.unit_number_value.unit == "Hz") {
          osc.freq_hz = std::max(1.0, it->second.unit_number_value.value);
        } else {
          osc.freq_hz = std::max(1.0, ValueToNumber(it->second, 0.0));
        }
      }
      if (const auto it = node.params.find("detune"); it != node.params.end()) {
        osc.detune_semitones += ParseDetuneSemitones(it->second);
      }
      if (const auto it = node.params.find("transpose"); it != node.params.end()) {
        // Transpose is semitones by default for bare numbers.
        if (it->second.kind == aurora::lang::ParamValue::Kind::kUnitNumber) {
          if (it->second.unit_number_value.unit == "st") {
            osc.detune_semitones += it->second.unit_number_value.value;
          } else if (it->second.unit_number_value.unit == "c") {
            osc.detune_semitones += it->second.unit_number_value.value / 100.0;
          }
        } else {
          osc.detune_semitones += ValueToNumber(it->second, 0.0);
        }
      }
      program.oscillators.push_back(osc);
    } else if (node.type == "noise_white" || node.type == "noise_pink") {
      program.noise_white = true;
    } else if (node.type == "sample_player" || node.type == "sample_slice") {
      program.sample_player = true;
    } else if (node.type == "env_adsr" || node.type == "env_ad" || node.type == "env_ar") {
      program.env.enabled = true;
      program.env_node_id = node.id;
      if (node.type == "env_ad") {
        program.env.mode = PatchProgram::Env::Mode::kAd;
      } else if (node.type == "env_ar") {
        program.env.mode = PatchProgram::Env::Mode::kAr;
      } else {
        program.env.mode = PatchProgram::Env::Mode::kAdsr;
      }
      if (const auto it = node.params.find("a"); it != node.params.end()) {
        program.env.a = UnitLiteralToSeconds(ValueToUnit(it->second));
      }
      if (const auto it = node.params.find("d"); it != node.params.end()) {
        program.env.d = UnitLiteralToSeconds(ValueToUnit(it->second));
      }
      program.env.s = NodeParamNumber(node.params, "s", 0.8);
      if (const auto it = node.params.find("r"); it != node.params.end()) {
        program.env.r = UnitLiteralToSeconds(ValueToUnit(it->second));
      }
      if (program.env.mode == PatchProgram::Env::Mode::kAd) {
        program.env.s = 0.0;
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
      program.filter.q = std::max(0.05, NodeParamNumber(node.params, "q", program.filter.q));
      program.filter.res = Clamp(NodeParamNumber(node.params, "res", program.filter.res), 0.0, 1.0);
      program.filter.drive = std::max(0.0, NodeParamNumber(node.params, "drive", program.filter.drive));
      program.filter.drive_pos =
          NodeParamText(node.params, "drive_pos", NodeParamText(node.params, "drive_stage", program.filter.drive_pos));
      if (const auto it = node.params.find("slope"); it != node.params.end()) {
        int slope = 12;
        if (it->second.kind == aurora::lang::ParamValue::Kind::kNumber) {
          slope = static_cast<int>(std::llround(it->second.number_value));
        } else if (it->second.kind == aurora::lang::ParamValue::Kind::kUnitNumber) {
          slope = static_cast<int>(std::llround(it->second.unit_number_value.value));
        } else {
          const std::string text = ValueToText(it->second);
          slope = (text.find("24") != std::string::npos) ? 24 : 12;
        }
        program.filter.slope_db = (slope >= 24) ? 24 : 12;
      }
      program.filter.keytrack = NodeParamNumber(node.params, "keytrack", program.filter.keytrack);
      if (const auto it = node.params.find("env_amt"); it != node.params.end()) {
        if (it->second.kind == aurora::lang::ParamValue::Kind::kUnitNumber && it->second.unit_number_value.unit == "Hz") {
          program.filter.env_amt = it->second.unit_number_value.value;
        } else {
          program.filter.env_amt = ValueToNumber(it->second, program.filter.env_amt);
        }
      } else if (const auto fit = node.params.find("env_amount"); fit != node.params.end()) {
        if (fit->second.kind == aurora::lang::ParamValue::Kind::kUnitNumber && fit->second.unit_number_value.unit == "Hz") {
          program.filter.env_amt = fit->second.unit_number_value.value;
        } else {
          program.filter.env_amt = ValueToNumber(fit->second, program.filter.env_amt);
        }
      }
    } else if (node.type == "lfo") {
      PatchProgram::Lfo lfo;
      lfo.node_id = node.id;
      lfo.shape = NodeParamText(node.params, "shape", "sine");
      if (const auto it = node.params.find("rate"); it != node.params.end()) {
        if (it->second.kind == aurora::lang::ParamValue::Kind::kUnitNumber && it->second.unit_number_value.unit == "Hz") {
          lfo.rate_hz = std::max(0.0, it->second.unit_number_value.value);
        } else {
          lfo.rate_hz = std::max(0.0, ValueToNumber(it->second, lfo.rate_hz));
        }
      } else if (const auto fit = node.params.find("freq"); fit != node.params.end()) {
        if (fit->second.kind == aurora::lang::ParamValue::Kind::kUnitNumber && fit->second.unit_number_value.unit == "Hz") {
          lfo.rate_hz = std::max(0.0, fit->second.unit_number_value.value);
        } else {
          lfo.rate_hz = std::max(0.0, ValueToNumber(fit->second, lfo.rate_hz));
        }
      }
      lfo.depth = std::max(0.0, NodeParamNumber(node.params, "depth", 1.0));
      lfo.pw = Clamp(NodeParamNumber(node.params, "pw", 0.5), 0.01, 0.99);
      lfo.phase = NodeParamNumber(node.params, "phase", 0.0);
      if (const auto it = node.params.find("unipolar"); it != node.params.end() &&
                                                  it->second.kind == aurora::lang::ParamValue::Kind::kBool) {
        lfo.unipolar = it->second.bool_value;
      }
      program.lfos.push_back(std::move(lfo));
    } else if (IsCvNodeType(node.type)) {
      PatchProgram::CvNode cv;
      cv.node_id = node.id;
      cv.type = node.type;
      cv.op = NodeParamText(node.params, "op", cv.op);
      cv.scale = NodeParamNumber(node.params, "scale", cv.scale);
      cv.offset = NodeParamNumber(node.params, "offset", cv.offset);
      cv.a = NodeParamNumber(node.params, "a", cv.a);
      cv.b = NodeParamNumber(node.params, "b", cv.b);
      cv.bias = NodeParamNumber(node.params, "bias", cv.bias);
      if (const auto it = node.params.find("rise"); it != node.params.end()) {
        cv.rise_seconds = std::max(0.0001, UnitLiteralToSeconds(ValueToUnit(it->second)));
      }
      if (const auto it = node.params.find("fall"); it != node.params.end()) {
        cv.fall_seconds = std::max(0.0001, UnitLiteralToSeconds(ValueToUnit(it->second)));
      }
      cv.min = NodeParamNumber(node.params, "min", cv.min);
      cv.max = NodeParamNumber(node.params, "max", cv.max);
      cv.threshold = NodeParamNumber(node.params, "threshold", cv.threshold);
      cv.hysteresis = std::max(0.0, NodeParamNumber(node.params, "hysteresis", cv.hysteresis));
      cv.high = NodeParamNumber(node.params, "high", cv.high);
      cv.low = NodeParamNumber(node.params, "low", cv.low);
      program.cv_nodes.push_back(std::move(cv));
    } else if (node.type == "vca") {
      program.vca.enabled = true;
      program.vca.node_id = node.id;
      if (const auto it = node.params.find("gain"); it != node.params.end()) {
        if (it->second.kind == aurora::lang::ParamValue::Kind::kUnitNumber && it->second.unit_number_value.unit == "dB") {
          program.vca.gain = DbToLinear(it->second.unit_number_value.value);
        } else {
          program.vca.gain = std::max(0.0, ValueToNumber(it->second, program.vca.gain));
        }
      }
      program.vca.cv = Clamp(NodeParamNumber(node.params, "cv", 1.0), 0.0, 1.0);
      program.vca.curve = NodeParamText(node.params, "curve", NodeParamText(node.params, "response", program.vca.curve));
      program.vca.curve_amount =
          std::max(0.2, NodeParamNumber(node.params, "curve_amt", NodeParamNumber(node.params, "curve_amount", 2.0)));
    } else if (node.type == "ring_mod" || node.type == "ring_mod_diode") {
      program.ring_mod.enabled = true;
      program.ring_mod.node_id = node.id;
      program.ring_mod.shape = NodeParamText(node.params, "shape", program.ring_mod.shape);
      program.ring_mod.mode = NodeParamText(node.params, "mode", program.ring_mod.mode);
      if (node.type == "ring_mod_diode") {
        program.ring_mod.mode = "diode";
      }
      if (const auto it = node.params.find("freq"); it != node.params.end()) {
        if (it->second.kind == aurora::lang::ParamValue::Kind::kUnitNumber && it->second.unit_number_value.unit == "Hz") {
          program.ring_mod.freq_hz = std::max(0.0, it->second.unit_number_value.value);
        } else {
          program.ring_mod.freq_hz = std::max(0.0, ValueToNumber(it->second, program.ring_mod.freq_hz));
        }
      } else if (const auto fit = node.params.find("rate"); fit != node.params.end()) {
        if (fit->second.kind == aurora::lang::ParamValue::Kind::kUnitNumber && fit->second.unit_number_value.unit == "Hz") {
          program.ring_mod.freq_hz = std::max(0.0, fit->second.unit_number_value.value);
        } else {
          program.ring_mod.freq_hz = std::max(0.0, ValueToNumber(fit->second, program.ring_mod.freq_hz));
        }
      }
      program.ring_mod.pw = Clamp(NodeParamNumber(node.params, "pw", program.ring_mod.pw), 0.01, 0.99);
      program.ring_mod.depth = std::max(0.0, NodeParamNumber(node.params, "depth", program.ring_mod.depth));
      program.ring_mod.mix = Clamp(NodeParamNumber(node.params, "mix", program.ring_mod.mix), 0.0, 1.0);
      program.ring_mod.bias = NodeParamNumber(node.params, "bias", program.ring_mod.bias);
    } else if (node.type == "softclip") {
      program.softclip.enabled = true;
      program.softclip.node_id = node.id;
      program.softclip.drive = std::max(0.0, NodeParamNumber(node.params, "drive", program.softclip.drive));
      program.softclip.mix = Clamp(NodeParamNumber(node.params, "mix", program.softclip.mix), 0.0, 1.0);
      program.softclip.bias = NodeParamNumber(node.params, "bias", program.softclip.bias);
    } else if (node.type == "audio_mix") {
      program.audio_mix.enabled = true;
      program.audio_mix.node_id = node.id;
      if (const auto it = node.params.find("gain"); it != node.params.end()) {
        if (it->second.kind == aurora::lang::ParamValue::Kind::kUnitNumber && it->second.unit_number_value.unit == "dB") {
          program.audio_mix.gain = DbToLinear(it->second.unit_number_value.value);
        } else {
          program.audio_mix.gain = ValueToNumber(it->second, program.audio_mix.gain);
        }
      }
      program.audio_mix.mix = Clamp(NodeParamNumber(node.params, "mix", program.audio_mix.mix), 0.0, 1.0);
      program.audio_mix.bias = NodeParamNumber(node.params, "bias", program.audio_mix.bias);
    } else if (node.type == "comb") {
      program.comb.enabled = true;
      program.comb.node_id = node.id;
      if (const auto it = node.params.find("time"); it != node.params.end()) {
        program.comb.time_seconds = std::max(0.001, UnitLiteralToSeconds(ValueToUnit(it->second)));
      }
      program.comb.feedback = Clamp(NodeParamNumber(node.params, "fb", program.comb.feedback), -0.99, 0.99);
      program.comb.mix = Clamp(NodeParamNumber(node.params, "mix", program.comb.mix), 0.0, 1.0);
      program.comb.damp = Clamp(NodeParamNumber(node.params, "damp", program.comb.damp), 0.0, 1.0);
    } else if (node.type == "pan") {
      program.pan.enabled = true;
      program.pan.node_id = node.id;
      program.pan.pos = Clamp(NodeParamNumber(node.params, "pos", program.pan.pos), -1.0, 1.0);
      program.pan.law = NodeParamText(node.params, "law", program.pan.law);
      program.pan.width = Clamp(NodeParamNumber(node.params, "width", program.pan.width), 0.0, 2.0);
    } else if (node.type == "stereo_width") {
      program.stereo_width.enabled = true;
      program.stereo_width.node_id = node.id;
      program.stereo_width.width = Clamp(NodeParamNumber(node.params, "width", program.stereo_width.width), 0.0, 2.0);
      if (const auto it = node.params.find("saturate"); it != node.params.end() &&
                                                  it->second.kind == aurora::lang::ParamValue::Kind::kBool) {
        program.stereo_width.saturate = it->second.bool_value;
      }
    } else if (node.type == "depth") {
      program.depth.enabled = true;
      program.depth.node_id = node.id;
      program.depth.distance = Clamp(NodeParamNumber(node.params, "distance", program.depth.distance), 0.0, 1.0);
      program.depth.air_absorption =
          Clamp(NodeParamNumber(node.params, "air_absorption", program.depth.air_absorption), 0.0, 1.0);
      program.depth.early_reflection_send =
          Clamp(NodeParamNumber(node.params, "early_reflection_send", program.depth.early_reflection_send), 0.0, 1.0);
    } else if (node.type == "decorrelate") {
      program.decorrelate.enabled = true;
      program.decorrelate.node_id = node.id;
      if (const auto it = node.params.find("time"); it != node.params.end()) {
        program.decorrelate.time_seconds = Clamp(UnitLiteralToSeconds(ValueToUnit(it->second)), 0.0002, 0.01);
      }
      program.decorrelate.mix = Clamp(NodeParamNumber(node.params, "mix", program.decorrelate.mix), 0.0, 1.0);
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
  for (const auto& conn : patch.graph.connections) {
    const auto [src_node, src_port] = SplitNodePort(conn.from);
    const auto [dst_node, dst_port] = SplitNodePort(conn.to);
    if (src_node.empty() || dst_node.empty() || dst_port.empty()) {
      continue;
    }
    const auto src_type_it = node_types.find(src_node);
    const auto dst_type_it = node_types.find(dst_node);
    if (src_type_it == node_types.end()) {
      continue;
    }
    if (dst_type_it == node_types.end()) {
      continue;
    }
    const PortKind dst_kind = classify_port(dst_type_it->second, dst_port, false);

    if (IsCvNodeType(dst_type_it->second) && (dst_kind == PortKind::kControlIn)) {
      for (auto& cv : program.cv_nodes) {
        if (cv.node_id == dst_node) {
          cv.inputs.push_back(PatchProgram::CvInputRoute{src_node, dst_port});
          break;
        }
      }
      continue;
    }

    const bool source_is_control = NodeIsControlSource(src_type_it->second);
    if (!source_is_control && dst_kind == PortKind::kControlIn) {
      continue;
    }
    if (dst_kind == PortKind::kAudioIn) {
      continue;
    }
    PatchProgram::ModRoute route;
    route.source_node_id = src_node;
    if (src_type_it->second == "lfo") {
      route.source_kind = PatchProgram::ModRoute::SourceKind::kLfo;
    } else if (src_type_it->second == "env_adsr" || src_type_it->second == "env_ad" || src_type_it->second == "env_ar") {
      route.source_kind = PatchProgram::ModRoute::SourceKind::kEnv;
    } else {
      route.source_kind = PatchProgram::ModRoute::SourceKind::kCvNode;
    }
    route.target_key = dst_node + "." + dst_port;
    route.rate = conn.rate.empty() ? "audio" : conn.rate;
    route.op = ParseModOp(conn.map, dst_port);
    if (const auto min_it = conn.map.find("min"); min_it != conn.map.end()) {
      route.min = ValueToNumber(min_it->second, route.min);
      route.use_range = true;
    }
    if (const auto max_it = conn.map.find("max"); max_it != conn.map.end()) {
      route.max = ValueToNumber(max_it->second, route.max);
      route.use_range = true;
    }
    if (const auto scale_it = conn.map.find("scale"); scale_it != conn.map.end()) {
      route.scale = ValueToNumber(scale_it->second, route.scale);
    }
    if (const auto offset_it = conn.map.find("offset"); offset_it != conn.map.end()) {
      route.offset = ValueToNumber(offset_it->second, route.offset);
    }
    if (const auto invert_it = conn.map.find("invert"); invert_it != conn.map.end()) {
      if (invert_it->second.kind == aurora::lang::ParamValue::Kind::kBool) {
        route.invert = invert_it->second.bool_value;
      } else {
        route.invert = ValueToNumber(invert_it->second, 0.0) != 0.0;
      }
    }
    if (const auto bias_it = conn.map.find("bias"); bias_it != conn.map.end()) {
      route.bias = ValueToNumber(bias_it->second, route.bias);
    }
    if (const auto curve_it = conn.map.find("curve"); curve_it != conn.map.end()) {
      route.curve = ValueToText(curve_it->second);
    }
    program.mod_routes.push_back(std::move(route));
  }
  if (program.oscillators.empty() && !program.noise_white && !program.sample_player) {
    PatchProgram::Osc fallback;
    fallback.type = "osc_sine";
    program.oscillators.push_back(fallback);
  }
  return program;
}

double EnvelopeValue(const PatchProgram::Env& env, double t, double note_dur, bool no_attack) {
  if (!env.enabled) {
    return 1.0;
  }
  const double attack = std::max(0.0001, env.a);
  const double decay = std::max(0.0001, env.d);
  const double release = std::max(0.0001, env.r);

  if (env.mode == PatchProgram::Env::Mode::kAd) {
    if (!no_attack && t < attack) {
      return Clamp(t / attack, 0.0, 1.0);
    }
    const double td = t - (no_attack ? 0.0 : attack);
    if (td < decay) {
      const double d = Clamp(td / decay, 0.0, 1.0);
      return 1.0 - d;
    }
    return 0.0;
  }

  if (env.mode == PatchProgram::Env::Mode::kAr) {
    if (!no_attack && t < attack) {
      return Clamp(t / attack, 0.0, 1.0);
    }
    if (t < note_dur) {
      return 1.0;
    }
    const double rt = (t - note_dur) / release;
    return 1.0 - Clamp(rt, 0.0, 1.0);
  }

  if (!no_attack && t < attack) {
    return Clamp(t / attack, 0.0, 1.0);
  }
  if (!no_attack && t < attack + decay) {
    const double dt = (t - attack) / decay;
    return 1.0 + (env.s - 1.0) * dt;
  }
  if (t < note_dur) {
    return no_attack ? env.s : env.s;
  }
  if (t < note_dur + release) {
    const double rt = (t - note_dur) / release;
    return env.s * (1.0 - Clamp(rt, 0.0, 1.0));
  }
  return 0.0;
}

double OscSample(const std::string& osc_type, double phase, double pulse_width) {
  const double norm = phase - std::floor(phase);
  if (osc_type == "osc_sine") {
    return std::sin(2.0 * kPi * norm);
  }
  if (osc_type == "osc_saw_blep") {
    return 2.0 * norm - 1.0;
  }
  if (osc_type == "osc_tri_blep") {
    return 4.0 * std::fabs(norm - 0.5) - 1.0;
  }
  if (osc_type == "osc_pulse_blep") {
    return (norm < pulse_width) ? 1.0 : -1.0;
  }
  return std::sin(2.0 * kPi * norm);
}

void RenderPlayToStem(aurora::core::AudioStem* stem, const PlayOccurrence& play, const PatchProgram& program,
                      const std::map<std::string, AutomationLane>& automation, int sample_rate, int block_size,
                      uint64_t seed) {
  if (stem == nullptr || stem->channels < 1 || stem->samples.empty()) {
    return;
  }
  const size_t stem_frames = stem->samples.size() / static_cast<size_t>(stem->channels);
  if (play.start_sample >= stem_frames) {
    return;
  }
  const double base_gain = DbToLinear(program.gain_db) * play.velocity;
  struct ValueRoute {
    const AutomationLane* lane = nullptr;
    const aurora::lang::ParamValue* param = nullptr;
  };
  const auto make_route = [&](const std::string& key) {
    ValueRoute route;
    if (const auto lane_it = automation.find(key); lane_it != automation.end()) {
      route.lane = &lane_it->second;
    }
    if (const auto param_it = play.params.find(key); param_it != play.params.end()) {
      route.param = &param_it->second;
    }
    return route;
  };

  const auto resolve_number = [&](const ValueRoute& route, double fallback, uint64_t sample) {
    if (route.param != nullptr) {
      return ValueToNumber(*route.param, fallback);
    }
    if (route.lane != nullptr) {
      return EvaluateLane(*route.lane, sample);
    }
    return fallback;
  };

  const auto resolve_seconds = [&](const ValueRoute& route, double fallback, uint64_t sample) {
    if (route.param != nullptr) {
      return std::max(0.0001, UnitLiteralToSeconds(ValueToUnit(*route.param)));
    }
    if (route.lane != nullptr) {
      return std::max(0.0001, EvaluateLane(*route.lane, sample));
    }
    return std::max(0.0001, fallback);
  };

  const auto resolve_semitones = [&](const ValueRoute& route, double fallback, bool numeric_is_cents, uint64_t sample) {
    if (route.param != nullptr) {
      if (numeric_is_cents) {
        return ParseDetuneSemitones(*route.param);
      }
      if (route.param->kind == aurora::lang::ParamValue::Kind::kUnitNumber) {
        if (route.param->unit_number_value.unit == "c") {
          return route.param->unit_number_value.value / 100.0;
        }
        return route.param->unit_number_value.value;
      }
      return ValueToNumber(*route.param, fallback);
    }
    if (route.lane != nullptr) {
      const double v = EvaluateLane(*route.lane, sample);
      return numeric_is_cents ? (v / 100.0) : v;
    }
    return fallback;
  };

  std::unordered_map<std::string, int> lfo_index_by_id;
  for (int i = 0; i < static_cast<int>(program.lfos.size()); ++i) {
    lfo_index_by_id[program.lfos[static_cast<size_t>(i)].node_id] = i;
  }
  std::unordered_map<std::string, int> cv_index_by_id;
  for (int i = 0; i < static_cast<int>(program.cv_nodes.size()); ++i) {
    cv_index_by_id[program.cv_nodes[static_cast<size_t>(i)].node_id] = i;
  }
  std::map<std::string, std::vector<size_t>> routes_by_target;
  for (size_t i = 0; i < program.mod_routes.size(); ++i) {
    routes_by_target[program.mod_routes[i].target_key].push_back(i);
  }
  const auto find_target_routes = [&](const std::string& key) -> const std::vector<size_t>* {
    const auto it = routes_by_target.find(key);
    return it == routes_by_target.end() ? nullptr : &it->second;
  };
  const auto lfo_value = [&](const PatchProgram::Lfo& lfo, double t_seconds) {
    const double phase = lfo.phase + t_seconds * lfo.rate_hz;
    double out = LfoWave(lfo.shape, phase, lfo.pw);
    if (lfo.unipolar) {
      out = 0.5 * (out + 1.0);
    }
    return out * lfo.depth;
  };
  std::vector<double> cv_state(program.cv_nodes.size(), 0.0);
  std::vector<bool> cv_state_valid(program.cv_nodes.size(), false);
  std::vector<bool> cv_gate_high(program.cv_nodes.size(), false);
  std::vector<bool> cv_gate_high_valid(program.cv_nodes.size(), false);
  const auto slew_toward = [&](double current, double target, double seconds, double dt) {
    const double tau = std::max(0.0001, seconds);
    const double alpha = 1.0 - std::exp(-dt / tau);
    return current + (target - current) * Clamp(alpha, 0.0, 1.0);
  };
  const auto apply_curve = [&](double x, const std::string& curve) {
    const double c = Clamp(x, 0.0, 1.0);
    if (curve == "step") {
      return c >= 0.5 ? 1.0 : 0.0;
    }
    if (curve == "smooth") {
      return c * c * (3.0 - 2.0 * c);
    }
    if (curve == "exp") {
      return c * c;
    }
    return c;
  };
  struct SourceRef {
    enum class Kind { kNone, kEnv, kLfo, kCv };
    Kind kind = Kind::kNone;
    int index = -1;
  };
  const auto resolve_source_ref = [&](const std::string& node_id) {
    SourceRef ref;
    if (!program.env_node_id.empty() && node_id == program.env_node_id) {
      ref.kind = SourceRef::Kind::kEnv;
      return ref;
    }
    if (const auto it = lfo_index_by_id.find(node_id); it != lfo_index_by_id.end()) {
      ref.kind = SourceRef::Kind::kLfo;
      ref.index = it->second;
      return ref;
    }
    if (const auto it = cv_index_by_id.find(node_id); it != cv_index_by_id.end()) {
      ref.kind = SourceRef::Kind::kCv;
      ref.index = it->second;
      return ref;
    }
    return ref;
  };
  struct CvInputRef {
    SourceRef source;
    bool in2 = false;
  };
  std::vector<std::vector<CvInputRef>> cv_inputs(program.cv_nodes.size());
  for (size_t i = 0; i < program.cv_nodes.size(); ++i) {
    for (const auto& input : program.cv_nodes[i].inputs) {
      cv_inputs[i].push_back(CvInputRef{resolve_source_ref(input.source_node_id), input.to_port == "in2" || input.to_port == "b"});
    }
  }
  std::vector<SourceRef> route_source_refs(program.mod_routes.size());
  for (size_t i = 0; i < program.mod_routes.size(); ++i) {
    route_source_refs[i] = resolve_source_ref(program.mod_routes[i].source_node_id);
  }
  std::vector<double> control_eval_cache(program.cv_nodes.size(), 0.0);
  std::vector<uint64_t> control_eval_cache_sample(program.cv_nodes.size(), std::numeric_limits<uint64_t>::max());
  std::vector<bool> control_eval_visiting(program.cv_nodes.size(), false);
  std::vector<double> route_last_value(program.mod_routes.size(), 0.0);
  std::vector<bool> route_last_value_valid(program.mod_routes.size(), false);
  std::function<double(const SourceRef&, double, double, uint64_t)> eval_control_source =
      [&](const SourceRef& source, double env_value, double t_seconds, uint64_t abs_sample) -> double {
    if (source.kind == SourceRef::Kind::kEnv) {
      return env_value;
    }
    if (source.kind == SourceRef::Kind::kLfo) {
      if (source.index < 0 || source.index >= static_cast<int>(program.lfos.size())) {
        return 0.0;
      }
      return lfo_value(program.lfos[static_cast<size_t>(source.index)], t_seconds);
    }
    if (source.kind != SourceRef::Kind::kCv) {
      return 0.0;
    }
    if (source.index < 0 || source.index >= static_cast<int>(program.cv_nodes.size())) {
      return 0.0;
    }
    const size_t cv_index = static_cast<size_t>(source.index);
    if (control_eval_cache_sample[cv_index] == abs_sample) {
      return control_eval_cache[cv_index];
    }
    if (control_eval_visiting[cv_index]) {
      return cv_state_valid[cv_index] ? cv_state[cv_index] : 0.0;
    }
    control_eval_visiting[cv_index] = true;
    const PatchProgram::CvNode& cv = program.cv_nodes[cv_index];
    double in1 = 0.0;
    double in2 = 0.0;
    for (const auto& input : cv_inputs[cv_index]) {
      const double v = eval_control_source(input.source, env_value, t_seconds, abs_sample);
      if (input.in2) {
        in2 += v;
      } else {
        in1 += v;
      }
    }
    double out = in1;
    if (cv.type == "cv_scale") {
      out = in1 * cv.scale + cv.bias;
    } else if (cv.type == "cv_offset") {
      out = in1 + cv.offset;
    } else if (cv.type == "cv_mix") {
      out = in1 * cv.a + in2 * cv.b + cv.bias;
    } else if (cv.type == "cv_invert") {
      out = (cv.bias - in1) * cv.scale + cv.offset;
    } else if (cv.type == "cv_sample_hold") {
      const double h = std::max(0.0, cv.hysteresis);
      const double rise = cv.threshold + 0.5 * h;
      const double fall = cv.threshold - 0.5 * h;
      bool trig_high = cv_gate_high_valid[cv_index] ? cv_gate_high[cv_index] : false;
      if (!trig_high && in2 >= rise) {
        trig_high = true;
      } else if (trig_high && in2 <= fall) {
        trig_high = false;
      }
      const bool had_prev = cv_gate_high_valid[cv_index];
      const bool rising = had_prev ? (!cv_gate_high[cv_index] && trig_high) : true;
      const double held = cv_state_valid[cv_index] ? cv_state[cv_index] : in1;
      out = rising ? in1 : held;
      cv_gate_high[cv_index] = trig_high;
      cv_gate_high_valid[cv_index] = true;
    } else if (cv.type == "cv_cmp") {
      const double h = std::max(0.0, cv.hysteresis);
      const double rise = cv.threshold + 0.5 * h;
      const double fall = cv.threshold - 0.5 * h;
      bool gate = cv_gate_high_valid[cv_index] ? cv_gate_high[cv_index] : false;
      if (!gate && in1 >= rise) {
        gate = true;
      } else if (gate && in1 <= fall) {
        gate = false;
      }
      cv_gate_high[cv_index] = gate;
      cv_gate_high_valid[cv_index] = true;
      out = gate ? cv.high : cv.low;
    } else if (cv.type == "cv_logic") {
      const bool a = in1 >= cv.threshold;
      const bool b = in2 >= cv.threshold;
      bool gate = false;
      if (cv.op == "or") {
        gate = a || b;
      } else if (cv.op == "xor") {
        gate = (a != b);
      } else if (cv.op == "nand") {
        gate = !(a && b);
      } else if (cv.op == "nor") {
        gate = !(a || b);
      } else if (cv.op == "xnor") {
        gate = (a == b);
      } else {
        gate = a && b;
      }
      out = gate ? cv.high : cv.low;
    } else if (cv.type == "cv_clip") {
      const double lo = std::min(cv.min, cv.max);
      const double hi = std::max(cv.min, cv.max);
      out = Clamp(in1 + cv.bias, lo, hi);
    } else if (cv.type == "cv_slew") {
      const double target = in1 + cv.bias;
      const double prev = cv_state_valid[cv_index] ? cv_state[cv_index] : target;
      const double dt = 1.0 / static_cast<double>(sample_rate);
      const double time = (target >= prev) ? cv.rise_seconds : cv.fall_seconds;
      out = slew_toward(prev, target, time, dt);
    }
    control_eval_visiting[cv_index] = false;
    control_eval_cache[cv_index] = out;
    control_eval_cache_sample[cv_index] = abs_sample;
    cv_state[cv_index] = out;
    cv_state_valid[cv_index] = true;
    return out;
  };
  const auto apply_mod = [&](const std::vector<size_t>* route_indices, double base_value, double env_value,
                             double t_seconds, uint64_t abs_sample) {
    if (route_indices == nullptr || route_indices->empty()) {
      return base_value;
    }
    double out = base_value;
    const uint64_t block = static_cast<uint64_t>(std::max(1, block_size));
    for (const size_t route_index : *route_indices) {
      const auto& route = program.mod_routes[route_index];
      const bool audio_rate = route.rate == "audio";
      const bool should_update =
          audio_rate || !route_last_value_valid[route_index] || ((abs_sample % block) == 0ULL);
      if (should_update) {
        double source_value = eval_control_source(route_source_refs[route_index], env_value, t_seconds, abs_sample);
        if (route.invert) {
          source_value = 1.0 - source_value;
        }
        source_value += route.bias;
        source_value = apply_curve(source_value, route.curve);
        route_last_value[route_index] = source_value;
        route_last_value_valid[route_index] = true;
      }
      const double source_value = route_last_value[route_index];
      double mapped = source_value;
      if (route.use_range) {
        mapped = route.min + Clamp(source_value, 0.0, 1.0) * (route.max - route.min);
      }
      mapped = mapped * route.scale + route.offset;
      if (route.op == PatchProgram::ModRoute::Op::kSet) {
        out = mapped;
      } else if (route.op == PatchProgram::ModRoute::Op::kMul) {
        out *= mapped;
      } else {
        out += mapped;
      }
    }
    return out;
  };

  struct OscRouting {
    const PatchProgram::Osc* osc = nullptr;
    ValueRoute freq;
    ValueRoute detune;
    ValueRoute transpose;
    ValueRoute pw;
    ValueRoute binaural_shift;
    ValueRoute binaural_mix;
    const std::vector<size_t>* freq_mod_routes = nullptr;
    const std::vector<size_t>* detune_mod_routes = nullptr;
    const std::vector<size_t>* transpose_mod_routes = nullptr;
    const std::vector<size_t>* pw_mod_routes = nullptr;
    const std::vector<size_t>* binaural_shift_mod_routes = nullptr;
    const std::vector<size_t>* binaural_mix_mod_routes = nullptr;
  };

  std::vector<OscRouting> osc_routes;
  osc_routes.reserve(program.oscillators.size());
  for (const auto& osc : program.oscillators) {
    const std::string prefix = osc.node_id + ".";
    OscRouting route;
    route.osc = &osc;
    route.freq = make_route(prefix + "freq");
    route.detune = make_route(prefix + "detune");
    route.transpose = make_route(prefix + "transpose");
    route.pw = make_route(prefix + "pw");
    route.binaural_shift = make_route(prefix + "binaural_shift");
    route.binaural_mix = make_route(prefix + "binaural_mix");
    route.freq_mod_routes = find_target_routes(prefix + "freq");
    route.detune_mod_routes = find_target_routes(prefix + "detune");
    route.transpose_mod_routes = find_target_routes(prefix + "transpose");
    route.pw_mod_routes = find_target_routes(prefix + "pw");
    route.binaural_shift_mod_routes = find_target_routes(prefix + "binaural_shift");
    route.binaural_mix_mod_routes = find_target_routes(prefix + "binaural_mix");
    osc_routes.push_back(route);
  }

  const ValueRoute env_a = make_route(program.env_node_id + ".a");
  const ValueRoute env_d = make_route(program.env_node_id + ".d");
  const ValueRoute env_s = make_route(program.env_node_id + ".s");
  const ValueRoute env_r = make_route(program.env_node_id + ".r");
  const ValueRoute filt_cutoff = make_route(program.filter_node_id + ".cutoff");
  const ValueRoute filt_freq = make_route(program.filter_node_id + ".freq");
  const ValueRoute filt_q = make_route(program.filter_node_id + ".q");
  const ValueRoute filt_res = make_route(program.filter_node_id + ".res");
  const ValueRoute filt_drive = make_route(program.filter_node_id + ".drive");
  const ValueRoute filt_keytrack = make_route(program.filter_node_id + ".keytrack");
  const ValueRoute filt_env_amt = make_route(program.filter_node_id + ".env_amt");
  const ValueRoute filt_env_amt_alias = make_route(program.filter_node_id + ".env_amount");
  const ValueRoute gain_db_route = make_route(program.gain_node_id + ".gain");
  const ValueRoute vca_cv_route = make_route(program.vca.node_id + ".cv");
  const ValueRoute vca_gain_route = make_route(program.vca.node_id + ".gain");
  const ValueRoute vca_curve_amount_route = make_route(program.vca.node_id + ".curve_amt");
  const ValueRoute vca_curve_amount_alias_route = make_route(program.vca.node_id + ".curve_amount");
  const ValueRoute ring_freq_route = make_route(program.ring_mod.node_id + ".freq");
  const ValueRoute ring_mix_route = make_route(program.ring_mod.node_id + ".mix");
  const ValueRoute ring_depth_route = make_route(program.ring_mod.node_id + ".depth");
  const ValueRoute ring_bias_route = make_route(program.ring_mod.node_id + ".bias");
  const ValueRoute ring_pw_route = make_route(program.ring_mod.node_id + ".pw");
  const ValueRoute softclip_drive_route = make_route(program.softclip.node_id + ".drive");
  const ValueRoute softclip_mix_route = make_route(program.softclip.node_id + ".mix");
  const ValueRoute softclip_bias_route = make_route(program.softclip.node_id + ".bias");
  const ValueRoute audio_mix_gain_route = make_route(program.audio_mix.node_id + ".gain");
  const ValueRoute audio_mix_mix_route = make_route(program.audio_mix.node_id + ".mix");
  const ValueRoute audio_mix_bias_route = make_route(program.audio_mix.node_id + ".bias");
  const ValueRoute comb_time_route = make_route(program.comb.node_id + ".time");
  const ValueRoute comb_fb_route = make_route(program.comb.node_id + ".fb");
  const ValueRoute comb_mix_route = make_route(program.comb.node_id + ".mix");
  const ValueRoute comb_damp_route = make_route(program.comb.node_id + ".damp");
  const ValueRoute pan_pos_route = make_route(program.pan.node_id + ".pos");
  const ValueRoute pan_width_route = make_route(program.pan.node_id + ".width");
  const ValueRoute stereo_width_route = make_route(program.stereo_width.node_id + ".width");
  const ValueRoute depth_distance_route = make_route(program.depth.node_id + ".distance");
  const ValueRoute depth_air_abs_route = make_route(program.depth.node_id + ".air_absorption");
  const ValueRoute depth_er_send_route = make_route(program.depth.node_id + ".early_reflection_send");
  const ValueRoute decor_time_route = make_route(program.decorrelate.node_id + ".time");
  const ValueRoute decor_mix_route = make_route(program.decorrelate.node_id + ".mix");
  const std::vector<size_t>* env_a_mod_routes = find_target_routes(program.env_node_id + ".a");
  const std::vector<size_t>* env_d_mod_routes = find_target_routes(program.env_node_id + ".d");
  const std::vector<size_t>* env_s_mod_routes = find_target_routes(program.env_node_id + ".s");
  const std::vector<size_t>* env_r_mod_routes = find_target_routes(program.env_node_id + ".r");
  const std::vector<size_t>* filt_cutoff_mod_routes = find_target_routes(program.filter_node_id + ".cutoff");
  const std::vector<size_t>* filt_q_mod_routes = find_target_routes(program.filter_node_id + ".q");
  const std::vector<size_t>* filt_res_mod_routes = find_target_routes(program.filter_node_id + ".res");
  const std::vector<size_t>* filt_drive_mod_routes = find_target_routes(program.filter_node_id + ".drive");
  const std::vector<size_t>* filt_keytrack_mod_routes = find_target_routes(program.filter_node_id + ".keytrack");
  const std::vector<size_t>* filt_env_amt_mod_routes = find_target_routes(program.filter_node_id + ".env_amt");
  const std::vector<size_t>* filt_env_amt_alias_mod_routes = find_target_routes(program.filter_node_id + ".env_amount");
  const std::vector<size_t>* gain_db_mod_routes = find_target_routes(program.gain_node_id + ".gain");
  const std::vector<size_t>* vca_cv_mod_routes = find_target_routes(program.vca.node_id + ".cv");
  const std::vector<size_t>* vca_gain_mod_routes = find_target_routes(program.vca.node_id + ".gain");
  const std::vector<size_t>* vca_curve_amount_mod_routes = find_target_routes(program.vca.node_id + ".curve_amt");
  const std::vector<size_t>* vca_curve_amount_alias_mod_routes = find_target_routes(program.vca.node_id + ".curve_amount");
  const std::vector<size_t>* ring_freq_mod_routes = find_target_routes(program.ring_mod.node_id + ".freq");
  const std::vector<size_t>* ring_mix_mod_routes = find_target_routes(program.ring_mod.node_id + ".mix");
  const std::vector<size_t>* ring_depth_mod_routes = find_target_routes(program.ring_mod.node_id + ".depth");
  const std::vector<size_t>* ring_bias_mod_routes = find_target_routes(program.ring_mod.node_id + ".bias");
  const std::vector<size_t>* ring_pw_mod_routes = find_target_routes(program.ring_mod.node_id + ".pw");
  const std::vector<size_t>* softclip_drive_mod_routes = find_target_routes(program.softclip.node_id + ".drive");
  const std::vector<size_t>* softclip_mix_mod_routes = find_target_routes(program.softclip.node_id + ".mix");
  const std::vector<size_t>* softclip_bias_mod_routes = find_target_routes(program.softclip.node_id + ".bias");
  const std::vector<size_t>* audio_mix_gain_mod_routes = find_target_routes(program.audio_mix.node_id + ".gain");
  const std::vector<size_t>* audio_mix_mix_mod_routes = find_target_routes(program.audio_mix.node_id + ".mix");
  const std::vector<size_t>* audio_mix_bias_mod_routes = find_target_routes(program.audio_mix.node_id + ".bias");
  const std::vector<size_t>* comb_time_mod_routes = find_target_routes(program.comb.node_id + ".time");
  const std::vector<size_t>* comb_fb_mod_routes = find_target_routes(program.comb.node_id + ".fb");
  const std::vector<size_t>* comb_mix_mod_routes = find_target_routes(program.comb.node_id + ".mix");
  const std::vector<size_t>* comb_damp_mod_routes = find_target_routes(program.comb.node_id + ".damp");
  const std::vector<size_t>* pan_pos_mod_routes = find_target_routes(program.pan.node_id + ".pos");
  const std::vector<size_t>* pan_width_mod_routes = find_target_routes(program.pan.node_id + ".width");
  const std::vector<size_t>* stereo_width_mod_routes = find_target_routes(program.stereo_width.node_id + ".width");
  const std::vector<size_t>* depth_distance_mod_routes = find_target_routes(program.depth.node_id + ".distance");
  const std::vector<size_t>* depth_air_abs_mod_routes = find_target_routes(program.depth.node_id + ".air_absorption");
  const std::vector<size_t>* depth_er_send_mod_routes = find_target_routes(program.depth.node_id + ".early_reflection_send");
  const std::vector<size_t>* decor_time_mod_routes = find_target_routes(program.decorrelate.node_id + ".time");
  const std::vector<size_t>* decor_mix_mod_routes = find_target_routes(program.decorrelate.node_id + ".mix");
  const auto no_attack_it = play.params.find("__env_no_attack");
  const bool no_attack = (no_attack_it != play.params.end() && no_attack_it->second.kind == aurora::lang::ParamValue::Kind::kBool &&
                          no_attack_it->second.bool_value);

  for (size_t pitch_index = 0; pitch_index < play.pitches.size(); ++pitch_index) {
    const ResolvedPitch& pitch = play.pitches[pitch_index];
    std::vector<double> phases_left(program.oscillators.size(), 0.0);
    std::vector<double> phases_right(program.oscillators.size(), 0.0);
    double ic1eq_left = 0.0;
    double ic2eq_left = 0.0;
    double ic1eq_right = 0.0;
    double ic2eq_right = 0.0;
    double ic1eq_left_b = 0.0;
    double ic2eq_left_b = 0.0;
    double ic1eq_right_b = 0.0;
    double ic2eq_right_b = 0.0;
    double ring_phase = 0.0;
    std::vector<float> comb_line_l;
    std::vector<float> comb_line_r;
    size_t comb_write_index = 0;
    double comb_lp_l = 0.0;
    double comb_lp_r = 0.0;
    std::vector<float> depth_line_l;
    std::vector<float> depth_line_r;
    size_t depth_write_index = 0;
    double depth_lp_l = 0.0;
    double depth_lp_r = 0.0;
    std::vector<float> decor_line_l;
    std::vector<float> decor_line_r;
    size_t decor_write_index = 0;
    double decor_delay_l = 0.0;
    double decor_delay_r = 0.0;
    if (program.comb.enabled && !program.comb.node_id.empty()) {
      const double reserve_seconds = Clamp(program.comb.time_seconds * 2.0 + 0.05, 0.01, 2.0);
      const size_t reserve_samples = std::max<size_t>(
          2U, static_cast<size_t>(std::llround(reserve_seconds * static_cast<double>(sample_rate))));
      comb_line_l.assign(reserve_samples, 0.0F);
      comb_line_r.assign(reserve_samples, 0.0F);
    }
    if (program.depth.enabled && !program.depth.node_id.empty()) {
      const size_t reserve_samples =
          std::max<size_t>(2U, static_cast<size_t>(std::llround(0.08 * static_cast<double>(sample_rate))));
      depth_line_l.assign(reserve_samples, 0.0F);
      depth_line_r.assign(reserve_samples, 0.0F);
    }
    if (program.decorrelate.enabled && !program.decorrelate.node_id.empty()) {
      const size_t reserve_samples =
          std::max<size_t>(2U, static_cast<size_t>(std::llround(0.012 * static_cast<double>(sample_rate))));
      decor_line_l.assign(reserve_samples, 0.0F);
      decor_line_r.assign(reserve_samples, 0.0F);
      PCG32 decor_rng(Hash64FromParts(seed, "decor", play.patch, std::to_string(play.start_sample),
                                      std::to_string(static_cast<int>(pitch_index))));
      const double base = Clamp(program.decorrelate.time_seconds, 0.0002, 0.01);
      decor_delay_l = Clamp(base * decor_rng.Uniform(0.7, 1.3), 0.0002, 0.01) * static_cast<double>(sample_rate);
      decor_delay_r = Clamp(base * decor_rng.Uniform(0.7, 1.3), 0.0002, 0.01) * static_cast<double>(sample_rate);
    }
    double spread_pan_offset = 0.0;
    double spread_detune_semitones = 0.0;
    uint64_t spread_delay_samples = 0;
    if (program.voice_spread.enabled) {
      PCG32 spread_rng(Hash64FromParts(seed, "voice_spread", play.patch, std::to_string(play.start_sample),
                                       std::to_string(static_cast<int>(pitch_index))));
      const double pan_amount = Clamp(program.voice_spread.pan, 0.0, 1.0);
      spread_pan_offset = spread_rng.Uniform(-pan_amount, pan_amount);
      spread_detune_semitones =
          spread_rng.Uniform(-program.voice_spread.detune_semitones, program.voice_spread.detune_semitones);
      const double delay_seconds = std::max(0.0, program.voice_spread.delay_seconds);
      spread_delay_samples =
          static_cast<uint64_t>(std::llround(spread_rng.Uniform(0.0, delay_seconds) * static_cast<double>(sample_rate)));
    }
    PCG32 noise_rng(Hash64FromParts(seed, "voice", play.patch, std::to_string(play.start_sample),
                                    std::to_string(static_cast<int>(pitch_index))));

    const uint64_t fade_samples = static_cast<uint64_t>(std::llround(sample_rate * 0.005));
    uint64_t render_samples = play.dur_samples;
    if (program.env.enabled) {
      if (program.env.mode == PatchProgram::Env::Mode::kAd) {
        const uint64_t ad = static_cast<uint64_t>(std::llround((std::max(0.0001, program.env.a) + std::max(0.0001, program.env.d)) *
                                                                static_cast<double>(sample_rate)));
        render_samples = std::max(render_samples, ad);
      } else {
        const uint64_t rel =
            static_cast<uint64_t>(std::llround(std::max(0.0001, program.env.r) * static_cast<double>(sample_rate)));
        render_samples += rel;
      }
    }
    render_samples += spread_delay_samples;
    const auto apply_pan_law = [&](double in_l, double in_r, double pos, const std::string& law, double width) {
      const double pan_pos = Clamp(pos, -1.0, 1.0);
      const double pan_width = Clamp(width, 0.0, 2.0);
      const double mid = 0.5 * (in_l + in_r);
      const double side = 0.5 * (in_l - in_r) * pan_width;
      double out_l = mid + side;
      double out_r = mid - side;
      const bool mono_like = std::abs(in_l - in_r) < 1e-12;
      if (mono_like) {
        if (law == "linear") {
          const double norm = (pan_pos + 1.0) * 0.5;
          out_l *= (1.0 - norm);
          out_r *= norm;
        } else {
          const double angle = (pan_pos + 1.0) * (kPi * 0.25);
          out_l *= std::cos(angle);
          out_r *= std::sin(angle);
        }
      } else {
        double bal_l = 1.0;
        double bal_r = 1.0;
        if (law == "linear") {
          if (pan_pos > 0.0) {
            bal_l = 1.0 - pan_pos;
          } else if (pan_pos < 0.0) {
            bal_r = 1.0 + pan_pos;
          }
        } else {
          if (pan_pos > 0.0) {
            bal_l = std::cos(pan_pos * (kPi * 0.5));
          } else if (pan_pos < 0.0) {
            bal_r = std::cos((-pan_pos) * (kPi * 0.5));
          }
        }
        out_l *= Clamp(bal_l, 0.0, 1.0);
        out_r *= Clamp(bal_r, 0.0, 1.0);
      }
      return std::pair<double, double>{out_l, out_r};
    };
    const auto read_delay_tap = [&](const std::vector<float>& line, size_t write_idx, double delay_samples) {
      if (line.empty()) {
        return 0.0;
      }
      const double len = static_cast<double>(line.size());
      double read_pos = static_cast<double>(write_idx) - delay_samples;
      while (read_pos < 0.0) {
        read_pos += len;
      }
      while (read_pos >= len) {
        read_pos -= len;
      }
      const size_t i0 = static_cast<size_t>(read_pos);
      const size_t i1 = (i0 + 1U) % line.size();
      const double frac = read_pos - static_cast<double>(i0);
      return static_cast<double>(line[i0]) * (1.0 - frac) + static_cast<double>(line[i1]) * frac;
    };

    for (uint64_t i = 0; i < render_samples; ++i) {
      if (i < spread_delay_samples) {
        continue;
      }
      const uint64_t voice_i = i - spread_delay_samples;
      const uint64_t abs_sample = play.start_sample + i;
      if (abs_sample >= stem_frames) {
        break;
      }
      const double t = static_cast<double>(voice_i) / sample_rate;
      const double note_dur = static_cast<double>(play.dur_samples) / sample_rate;
      PatchProgram::Env env_state = program.env;
      if (program.env.enabled && !program.env_node_id.empty()) {
        env_state.a =
            std::max(0.0001, apply_mod(env_a_mod_routes, resolve_seconds(env_a, program.env.a, abs_sample), 0.0, t, abs_sample));
        env_state.d =
            std::max(0.0001, apply_mod(env_d_mod_routes, resolve_seconds(env_d, program.env.d, abs_sample), 0.0, t, abs_sample));
        env_state.s = Clamp(apply_mod(env_s_mod_routes, resolve_number(env_s, program.env.s, abs_sample), 0.0, t, abs_sample),
                            0.0, 1.0);
        env_state.r =
            std::max(0.0001, apply_mod(env_r_mod_routes, resolve_seconds(env_r, program.env.r, abs_sample), 0.0, t, abs_sample));
      }
      double env = EnvelopeValue(env_state, t, note_dur, no_attack);
      const bool env_has_release_stage =
          program.env.enabled && program.env.mode != PatchProgram::Env::Mode::kAd;

      if (voice_i < fade_samples && fade_samples > 0) {
        env *= static_cast<double>(voice_i) / static_cast<double>(fade_samples);
      }
      if (!env_has_release_stage && voice_i < play.dur_samples && play.dur_samples > fade_samples &&
          voice_i > play.dur_samples - fade_samples && fade_samples > 0) {
        const uint64_t rem = play.dur_samples - voice_i;
        env *= static_cast<double>(rem) / static_cast<double>(fade_samples);
      }
      if (play.xfade_in_samples > 0 && abs_sample >= play.section_start_sample &&
          abs_sample < play.section_start_sample + play.xfade_in_samples) {
        const uint64_t x = abs_sample - play.section_start_sample;
        env *= Clamp(static_cast<double>(x) / static_cast<double>(play.xfade_in_samples), 0.0, 1.0);
      }
      const uint64_t xfade_out_start =
          (play.section_end_sample > play.xfade_out_samples) ? (play.section_end_sample - play.xfade_out_samples) : 0;
      if (play.xfade_out_samples > 0 && play.section_end_sample > 0 && abs_sample >= xfade_out_start &&
          abs_sample < play.section_end_sample) {
        const uint64_t rem = play.section_end_sample - abs_sample;
        env *= Clamp(static_cast<double>(rem) / static_cast<double>(play.xfade_out_samples), 0.0, 1.0);
      }

      double sample_left = 0.0;
      double sample_right = 0.0;
      for (size_t osc_idx = 0; osc_idx < program.oscillators.size(); ++osc_idx) {
        const auto& route = osc_routes[osc_idx];
        const auto& osc = *route.osc;
        const double detune = resolve_semitones(route.detune, osc.detune_semitones, true, abs_sample);
        const double transpose = resolve_semitones(route.transpose, 0.0, false, abs_sample);
        const double mod_detune = apply_mod(route.detune_mod_routes, detune, env, t, abs_sample);
        const double mod_transpose = apply_mod(route.transpose_mod_routes, transpose, env, t, abs_sample);
        const double pitch_freq =
            std::max(1.0, pitch.frequency * std::pow(2.0, (mod_detune + spread_detune_semitones + mod_transpose) / 12.0));

        // Event pitch is authoritative when present; static osc.freq is fallback only.
        double freq = (play.pitches.empty() && osc.freq_hz.has_value()) ? *osc.freq_hz : pitch_freq;
        if (route.freq.param != nullptr) {
          freq = std::max(1.0, ValueToUnit(*route.freq.param).value);
        } else if (route.freq.lane != nullptr) {
          freq = std::max(1.0, EvaluateLane(*route.freq.lane, abs_sample));
        }
        freq = std::max(1.0, apply_mod(route.freq_mod_routes, freq, env, t, abs_sample));

        const bool binaural_active = program.binaural.enabled;
        const double binaural_shift_hz =
            apply_mod(route.binaural_shift_mod_routes,
                      resolve_number(route.binaural_shift, program.binaural.shift_hz, abs_sample), env, t, abs_sample);
        const double binaural_mix = Clamp(apply_mod(route.binaural_mix_mod_routes,
                                                    resolve_number(route.binaural_mix, program.binaural.mix, abs_sample),
                                                    env, t, abs_sample),
                                          0.0, 1.0);
        double freq_left = freq;
        double freq_right = freq;
        if (binaural_active) {
          const double split_left = std::max(1.0, freq - 0.5 * binaural_shift_hz);
          const double split_right = std::max(1.0, freq + 0.5 * binaural_shift_hz);
          freq_left = freq * (1.0 - binaural_mix) + split_left * binaural_mix;
          freq_right = freq * (1.0 - binaural_mix) + split_right * binaural_mix;
        }

        const double pw = Clamp(apply_mod(route.pw_mod_routes, resolve_number(route.pw, osc.pw, abs_sample), env, t, abs_sample),
                                0.01, 0.99);
        sample_left += OscSample(osc.type, phases_left[osc_idx], pw);
        phases_left[osc_idx] += freq_left / static_cast<double>(sample_rate);
        sample_right += OscSample(osc.type, phases_right[osc_idx], pw);
        phases_right[osc_idx] += freq_right / static_cast<double>(sample_rate);
      }
      if (program.noise_white) {
        const double n = noise_rng.Uniform(-1.0, 1.0) * 0.25;
        sample_left += n;
        sample_right += n;
      }
      if (program.sample_player) {
        const double decay = std::exp(-t * 20.0);
        const double n = noise_rng.Uniform(-1.0, 1.0) * decay * 0.6;
        sample_left += n;
        sample_right += n;
      }

      if (!program.oscillators.empty()) {
        const double inv = 1.0 / static_cast<double>(program.oscillators.size());
        sample_left *= inv;
        sample_right *= inv;
      }

      if (program.ring_mod.enabled && !program.ring_mod.node_id.empty()) {
        const double ring_freq = std::max(
            0.0, apply_mod(ring_freq_mod_routes, resolve_number(ring_freq_route, program.ring_mod.freq_hz, abs_sample), env, t,
                           abs_sample));
        const double ring_depth =
            std::max(0.0, apply_mod(ring_depth_mod_routes, resolve_number(ring_depth_route, program.ring_mod.depth, abs_sample),
                                    env, t, abs_sample));
        const double ring_mix = Clamp(
            apply_mod(ring_mix_mod_routes, resolve_number(ring_mix_route, program.ring_mod.mix, abs_sample), env, t, abs_sample),
            0.0, 1.0);
        const double ring_bias =
            apply_mod(ring_bias_mod_routes, resolve_number(ring_bias_route, program.ring_mod.bias, abs_sample), env, t, abs_sample);
        const double ring_pw = Clamp(
            apply_mod(ring_pw_mod_routes, resolve_number(ring_pw_route, program.ring_mod.pw, abs_sample), env, t, abs_sample), 0.01,
            0.99);
        ring_phase += ring_freq / static_cast<double>(sample_rate);
        const double carrier = LfoWave(program.ring_mod.shape, ring_phase, ring_pw);
        double wet_left = 0.0;
        double wet_right = 0.0;
        if (program.ring_mod.mode == "unbalanced") {
          const double mod = 1.0 + carrier * ring_depth + ring_bias;
          wet_left = sample_left * mod;
          wet_right = sample_right * mod;
        } else if (program.ring_mod.mode == "diode") {
          const double mod = std::max(0.0, std::fabs(carrier) * ring_depth + ring_bias);
          wet_left = sample_left * mod;
          wet_right = sample_right * mod;
        } else {
          const double mod = carrier * ring_depth + ring_bias;
          wet_left = sample_left * mod;
          wet_right = sample_right * mod;
        }
        sample_left = sample_left * (1.0 - ring_mix) + wet_left * ring_mix;
        sample_right = sample_right * (1.0 - ring_mix) + wet_right * ring_mix;
      }

      if (program.softclip.enabled && !program.softclip.node_id.empty()) {
        const double drive = std::max(
            0.0, apply_mod(softclip_drive_mod_routes,
                           resolve_number(softclip_drive_route, program.softclip.drive, abs_sample), env, t, abs_sample));
        const double clip_mix = Clamp(
            apply_mod(softclip_mix_mod_routes, resolve_number(softclip_mix_route, program.softclip.mix, abs_sample), env, t,
                      abs_sample),
            0.0, 1.0);
        const double clip_bias =
            apply_mod(softclip_bias_mod_routes, resolve_number(softclip_bias_route, program.softclip.bias, abs_sample), env, t,
                      abs_sample);
        const double wet_left = std::tanh((sample_left + clip_bias) * drive);
        const double wet_right = std::tanh((sample_right + clip_bias) * drive);
        sample_left = sample_left * (1.0 - clip_mix) + wet_left * clip_mix;
        sample_right = sample_right * (1.0 - clip_mix) + wet_right * clip_mix;
      }

      if (program.audio_mix.enabled && !program.audio_mix.node_id.empty()) {
        const double util_gain =
            apply_mod(audio_mix_gain_mod_routes, resolve_number(audio_mix_gain_route, program.audio_mix.gain, abs_sample), env,
                      t, abs_sample);
        const double util_mix = Clamp(
            apply_mod(audio_mix_mix_mod_routes, resolve_number(audio_mix_mix_route, program.audio_mix.mix, abs_sample), env, t,
                      abs_sample),
            0.0, 1.0);
        const double util_bias =
            apply_mod(audio_mix_bias_mod_routes, resolve_number(audio_mix_bias_route, program.audio_mix.bias, abs_sample), env, t,
                      abs_sample);
        const double wet_left = sample_left * util_gain + util_bias;
        const double wet_right = sample_right * util_gain + util_bias;
        sample_left = sample_left * (1.0 - util_mix) + wet_left * util_mix;
        sample_right = sample_right * (1.0 - util_mix) + wet_right * util_mix;
      }

      double cutoff = resolve_number(filt_cutoff, program.filter.cutoff_hz, abs_sample);
      if (filt_cutoff.param == nullptr && filt_cutoff.lane == nullptr) {
        cutoff = resolve_number(filt_freq, cutoff, abs_sample);
      }
      const double keytrack = apply_mod(filt_keytrack_mod_routes,
                                        resolve_number(filt_keytrack, program.filter.keytrack, abs_sample), env, t, abs_sample);
      const double keytrack_ratio = std::pow(2.0, ((static_cast<double>(pitch.midi) - 60.0) / 12.0) * keytrack);
      cutoff *= keytrack_ratio;
      ValueRoute active_env_amt = filt_env_amt;
      const std::vector<size_t>* active_env_amt_mod_routes = filt_env_amt_mod_routes;
      if (active_env_amt.param == nullptr && active_env_amt.lane == nullptr) {
        active_env_amt = filt_env_amt_alias;
        active_env_amt_mod_routes = filt_env_amt_alias_mod_routes;
      }
      const double env_amt =
          apply_mod(active_env_amt_mod_routes, resolve_number(active_env_amt, program.filter.env_amt, abs_sample), env, t, abs_sample);
      cutoff += env * env_amt;
      cutoff = apply_mod(filt_cutoff_mod_routes, cutoff, env, t, abs_sample);
      cutoff = std::max(20.0, cutoff);

      if (program.filter.enabled) {
        const double nyquist = static_cast<double>(sample_rate) * 0.5;
        cutoff = Clamp(cutoff, 20.0, nyquist * 0.99);

        const double q =
            std::max(0.05, apply_mod(filt_q_mod_routes, resolve_number(filt_q, program.filter.q, abs_sample), env, t, abs_sample));
        const double res = Clamp(apply_mod(filt_res_mod_routes, resolve_number(filt_res, program.filter.res, abs_sample), env, t,
                                           abs_sample),
                                 0.0, 1.0);
        const double drive = std::max(
            0.0, apply_mod(filt_drive_mod_routes, resolve_number(filt_drive, program.filter.drive, abs_sample), env, t, abs_sample));

        // Convert normalized resonance to an additional Q boost, while keeping explicit Q authoritative.
        const double effective_q = Clamp(q * (1.0 + res * 8.0), 0.05, 24.0);
        const double g = std::tan(kPi * cutoff / static_cast<double>(sample_rate));
        const double k = 1.0 / effective_q;
        const double a1 = 1.0 / (1.0 + g * (g + k));
        const double a2 = g * a1;
        const double a3 = g * a2;
        const auto process_filter_sample = [&](double in, double* ic1eq, double* ic2eq) {
          const double v3 = in - *ic2eq;
          const double v1 = a1 * *ic1eq + a2 * v3;
          const double v2 = *ic2eq + a2 * *ic1eq + a3 * v3;
          *ic1eq = 2.0 * v1 - *ic1eq;
          *ic2eq = 2.0 * v2 - *ic2eq;
          const double lp = v2;
          const double bp = v1;
          const double hp = v3 - k * v1 - v2;
          const double notch = hp + lp;
          const std::string& mode = program.filter.mode;
          if (mode == "hp" || mode == "highpass") {
            return hp;
          }
          if (mode == "bp" || mode == "bandpass") {
            return bp;
          }
          if (mode == "notch" || mode == "bandstop") {
            return notch;
          }
          return lp;
        };

        const auto drive_shaper = [&](double in) {
          if (drive <= 0.0 || std::abs(drive - 1.0) <= 0.0001) {
            return in;
          }
          const double norm = std::tanh(drive);
          const double inv_norm = (std::abs(norm) > 0.000001) ? (1.0 / norm) : 1.0;
          return std::tanh(in * drive) * inv_norm;
        };

        const bool post_drive = (program.filter.drive_pos == "post" || program.filter.drive_pos == "after");
        const bool steep_slope = program.filter.slope_db >= 24;
        if (post_drive) {
          double out_l = process_filter_sample(sample_left, &ic1eq_left, &ic2eq_left);
          double out_r = process_filter_sample(sample_right, &ic1eq_right, &ic2eq_right);
          if (steep_slope) {
            out_l = process_filter_sample(out_l, &ic1eq_left_b, &ic2eq_left_b);
            out_r = process_filter_sample(out_r, &ic1eq_right_b, &ic2eq_right_b);
          }
          sample_left = drive_shaper(out_l);
          sample_right = drive_shaper(out_r);
        } else {
          sample_left = process_filter_sample(drive_shaper(sample_left), &ic1eq_left, &ic2eq_left);
          sample_right = process_filter_sample(drive_shaper(sample_right), &ic1eq_right, &ic2eq_right);
          if (steep_slope) {
            sample_left = process_filter_sample(sample_left, &ic1eq_left_b, &ic2eq_left_b);
            sample_right = process_filter_sample(sample_right, &ic1eq_right_b, &ic2eq_right_b);
          }
        }
      }

      if (program.comb.enabled && !program.comb.node_id.empty() && !comb_line_l.empty()) {
        const double comb_time_seconds = std::max(
            0.001, apply_mod(comb_time_mod_routes, resolve_seconds(comb_time_route, program.comb.time_seconds, abs_sample), env, t,
                             abs_sample));
        const size_t comb_delay = static_cast<size_t>(std::llround(comb_time_seconds * static_cast<double>(sample_rate)));
        const size_t max_delay = comb_line_l.size() > 1U ? comb_line_l.size() - 1U : 1U;
        const size_t delay_samples = std::max<size_t>(1U, std::min(comb_delay, max_delay));
        const double comb_fb = Clamp(
            apply_mod(comb_fb_mod_routes, resolve_number(comb_fb_route, program.comb.feedback, abs_sample), env, t, abs_sample),
            -0.99, 0.99);
        const double comb_mix = Clamp(
            apply_mod(comb_mix_mod_routes, resolve_number(comb_mix_route, program.comb.mix, abs_sample), env, t, abs_sample), 0.0,
            1.0);
        const double comb_damp = Clamp(
            apply_mod(comb_damp_mod_routes, resolve_number(comb_damp_route, program.comb.damp, abs_sample), env, t, abs_sample), 0.0,
            1.0);

        const size_t read_index = (comb_write_index + comb_line_l.size() - delay_samples) % comb_line_l.size();
        const double delayed_l = static_cast<double>(comb_line_l[read_index]);
        const double delayed_r = static_cast<double>(comb_line_r[read_index]);
        const double lp_alpha = 1.0 - comb_damp;
        comb_lp_l += (delayed_l - comb_lp_l) * lp_alpha;
        comb_lp_r += (delayed_r - comb_lp_r) * lp_alpha;
        comb_line_l[comb_write_index] = static_cast<float>(sample_left + comb_lp_l * comb_fb);
        comb_line_r[comb_write_index] = static_cast<float>(sample_right + comb_lp_r * comb_fb);
        comb_write_index = (comb_write_index + 1U) % comb_line_l.size();

        sample_left = sample_left * (1.0 - comb_mix) + delayed_l * comb_mix;
        sample_right = sample_right * (1.0 - comb_mix) + delayed_r * comb_mix;
      }

      if (program.decorrelate.enabled && !program.decorrelate.node_id.empty() && !decor_line_l.empty()) {
        const double decor_time =
            Clamp(apply_mod(decor_time_mod_routes, resolve_seconds(decor_time_route, program.decorrelate.time_seconds, abs_sample),
                            env, t, abs_sample),
                  0.0002, 0.01);
        const double decor_mix = Clamp(
            apply_mod(decor_mix_mod_routes, resolve_number(decor_mix_route, program.decorrelate.mix, abs_sample), env, t, abs_sample),
            0.0, 1.0);
        const double active_l = Clamp(0.5 * decor_delay_l + 0.5 * decor_time * static_cast<double>(sample_rate), 1.0,
                                      static_cast<double>(decor_line_l.size() - 1U));
        const double active_r = Clamp(0.5 * decor_delay_r + 0.5 * decor_time * static_cast<double>(sample_rate), 1.0,
                                      static_cast<double>(decor_line_r.size() - 1U));
        const double wet_l = read_delay_tap(decor_line_l, decor_write_index, active_l);
        const double wet_r = read_delay_tap(decor_line_r, decor_write_index, active_r);
        decor_line_l[decor_write_index] = static_cast<float>(sample_left);
        decor_line_r[decor_write_index] = static_cast<float>(sample_right);
        decor_write_index = (decor_write_index + 1U) % decor_line_l.size();
        sample_left = sample_left * (1.0 - decor_mix) + wet_l * decor_mix;
        sample_right = sample_right * (1.0 - decor_mix) + wet_r * decor_mix;
      }

      if (program.voice_spread.enabled && std::abs(spread_pan_offset) > 1e-9) {
        const auto spread_out = apply_pan_law(sample_left, sample_right, spread_pan_offset, "equal_power", 1.0);
        sample_left = spread_out.first;
        sample_right = spread_out.second;
      }

      if (program.pan.enabled && !program.pan.node_id.empty()) {
        const double pan_pos = Clamp(
            apply_mod(pan_pos_mod_routes, resolve_number(pan_pos_route, program.pan.pos, abs_sample), env, t, abs_sample), -1.0, 1.0);
        const double pan_width = Clamp(
            apply_mod(pan_width_mod_routes, resolve_number(pan_width_route, program.pan.width, abs_sample), env, t, abs_sample),
            0.0, 2.0);
        const auto pan_out = apply_pan_law(sample_left, sample_right, pan_pos, program.pan.law, pan_width);
        sample_left = pan_out.first;
        sample_right = pan_out.second;
      }

      if (program.stereo_width.enabled && !program.stereo_width.node_id.empty()) {
        const double width = Clamp(
            apply_mod(stereo_width_mod_routes, resolve_number(stereo_width_route, program.stereo_width.width, abs_sample), env, t,
                      abs_sample),
            0.0, 2.0);
        const double mid = 0.5 * (sample_left + sample_right);
        const double side = 0.5 * (sample_left - sample_right) * width;
        sample_left = mid + side;
        sample_right = mid - side;
        if (program.stereo_width.saturate) {
          sample_left = std::tanh(sample_left);
          sample_right = std::tanh(sample_right);
        }
      }

      if (program.depth.enabled && !program.depth.node_id.empty() && !depth_line_l.empty()) {
        const double distance = Clamp(apply_mod(depth_distance_mod_routes,
                                                resolve_number(depth_distance_route, program.depth.distance, abs_sample), env, t,
                                                abs_sample),
                                      0.0, 1.0);
        const double air_absorption = Clamp(apply_mod(depth_air_abs_mod_routes,
                                                      resolve_number(depth_air_abs_route, program.depth.air_absorption, abs_sample),
                                                      env, t, abs_sample),
                                            0.0, 1.0);
        const double er_send = Clamp(apply_mod(depth_er_send_mod_routes,
                                               resolve_number(depth_er_send_route, program.depth.early_reflection_send, abs_sample),
                                               env, t, abs_sample),
                                     0.0, 1.0);

        const double depth_cutoff = Clamp(20000.0 - (20000.0 - 700.0) * (air_absorption * distance), 150.0, 20000.0);
        const double wc = 2.0 * kPi * std::max(1.0, depth_cutoff);
        const double dt = 1.0 / static_cast<double>(sample_rate);
        const double alpha = Clamp(wc * dt / (1.0 + wc * dt), 0.0, 1.0);
        depth_lp_l += (sample_left - depth_lp_l) * alpha;
        depth_lp_r += (sample_right - depth_lp_r) * alpha;
        sample_left = depth_lp_l;
        sample_right = depth_lp_r;

        const double base_delay_seconds = 0.004 + 0.022 * distance;
        const double tap1 = std::max(1.0, base_delay_seconds * static_cast<double>(sample_rate));
        const double tap2 = std::max(1.0, (base_delay_seconds * 1.67 + 0.0015) * static_cast<double>(sample_rate));
        const double er_l = 0.65 * read_delay_tap(depth_line_l, depth_write_index, tap1) +
                            0.35 * read_delay_tap(depth_line_r, depth_write_index, tap2);
        const double er_r = 0.65 * read_delay_tap(depth_line_r, depth_write_index, tap1) +
                            0.35 * read_delay_tap(depth_line_l, depth_write_index, tap2);
        depth_line_l[depth_write_index] = static_cast<float>(sample_left);
        depth_line_r[depth_write_index] = static_cast<float>(sample_right);
        depth_write_index = (depth_write_index + 1U) % depth_line_l.size();

        const double er_mix = Clamp(er_send * (0.2 + 0.8 * distance), 0.0, 1.0);
        sample_left = sample_left * (1.0 - 0.45 * er_mix) + er_l * er_mix;
        sample_right = sample_right * (1.0 - 0.45 * er_mix) + er_r * er_mix;

        const double depth_gain = DbToLinear(-18.0 * distance);
        sample_left *= depth_gain;
        sample_right *= depth_gain;
      }

      double gain = base_gain;
      if (!program.gain_node_id.empty()) {
        const double gain_db = apply_mod(gain_db_mod_routes, resolve_number(gain_db_route, program.gain_db, abs_sample), env, t,
                                         abs_sample);
        gain = DbToLinear(gain_db) * play.velocity;
      }
      if (program.vca.enabled && !program.vca.node_id.empty()) {
        const double vca_cv = Clamp(apply_mod(vca_cv_mod_routes, resolve_number(vca_cv_route, program.vca.cv, abs_sample), env, t,
                                              abs_sample),
                                    0.0, 1.0);
        const double vca_gain =
            std::max(0.0, apply_mod(vca_gain_mod_routes, resolve_number(vca_gain_route, program.vca.gain, abs_sample), env, t,
                                    abs_sample));
        ValueRoute active_curve_amount = vca_curve_amount_route;
        const std::vector<size_t>* active_curve_amount_mod_routes = vca_curve_amount_mod_routes;
        if (active_curve_amount.param == nullptr && active_curve_amount.lane == nullptr) {
          active_curve_amount = vca_curve_amount_alias_route;
          active_curve_amount_mod_routes = vca_curve_amount_alias_mod_routes;
        }
        const double curve_amount =
            Clamp(apply_mod(active_curve_amount_mod_routes,
                            resolve_number(active_curve_amount, program.vca.curve_amount, abs_sample), env, t, abs_sample),
                  0.2, 8.0);
        double shaped_cv = vca_cv;
        if (program.vca.curve == "exp" || program.vca.curve == "exponential") {
          shaped_cv = std::pow(vca_cv, curve_amount);
        } else if (program.vca.curve == "log" || program.vca.curve == "logarithmic") {
          shaped_cv = 1.0 - std::pow(1.0 - vca_cv, curve_amount);
        }
        gain *= shaped_cv * vca_gain;
      }
      const float out_left = static_cast<float>(sample_left * env * gain);
      const float out_right = static_cast<float>(sample_right * env * gain);
      const size_t frame_index = static_cast<size_t>(abs_sample);
      if (stem->channels == 1) {
        stem->samples[frame_index] += 0.5f * (out_left + out_right);
      } else {
        const size_t base = frame_index * 2U;
        if (base + 1U < stem->samples.size()) {
          stem->samples[base] += out_left;
          stem->samples[base + 1U] += out_right;
        }
      }
    }
  }
}

struct BusProgram {
  int channels = 1;
  bool has_delay = false;
  bool has_reverb = false;

  double delay_time_seconds = 0.35;
  double delay_fb = 0.35;
  double delay_mix = 0.35;
  double delay_hicut_hz = 12000.0;
  double delay_locut_hz = 20.0;
  bool delay_pingpong = false;
  double delay_mod_rate_hz = 0.0;
  double delay_mod_depth_seconds = 0.0;

  double reverb_mix = 0.30;
  double reverb_decay = 4.0;
  double reverb_predelay_seconds = 0.02;
  double reverb_size = 0.7;
  double reverb_width = 1.0;
  double reverb_hicut_hz = 9000.0;
  double reverb_locut_hz = 80.0;
};

double NodeParamHz(const std::map<std::string, aurora::lang::ParamValue>& params, const std::string& key, double fallback) {
  const auto it = params.find(key);
  if (it == params.end()) {
    return fallback;
  }
  if (it->second.kind == aurora::lang::ParamValue::Kind::kUnitNumber && it->second.unit_number_value.unit == "Hz") {
    return std::max(1.0, it->second.unit_number_value.value);
  }
  return std::max(1.0, ValueToNumber(it->second, fallback));
}

BusProgram BuildBusProgram(const aurora::lang::BusDefinition& bus) {
  BusProgram program;
  program.channels = std::clamp(bus.channels, 1, 2);
  for (const auto& node : bus.graph.nodes) {
    if (node.type == "reverb_algo") {
      program.has_reverb = true;
      program.reverb_mix = Clamp(NodeParamNumber(node.params, "mix", program.reverb_mix), 0.0, 1.0);
      if (const auto it = node.params.find("decay"); it != node.params.end()) {
        program.reverb_decay = std::max(0.1, UnitLiteralToSeconds(ValueToUnit(it->second)));
      }
      if (const auto it = node.params.find("predelay"); it != node.params.end()) {
        program.reverb_predelay_seconds = std::max(0.0, UnitLiteralToSeconds(ValueToUnit(it->second)));
      }
      program.reverb_size = Clamp(NodeParamNumber(node.params, "size", program.reverb_size), 0.1, 1.0);
      program.reverb_width = Clamp(NodeParamNumber(node.params, "width", program.reverb_width), 0.0, 1.0);
      program.reverb_hicut_hz = NodeParamHz(node.params, "hicut", program.reverb_hicut_hz);
      program.reverb_locut_hz = NodeParamHz(node.params, "locut", program.reverb_locut_hz);
    } else if (node.type == "delay") {
      program.has_delay = true;
      if (const auto it = node.params.find("time"); it != node.params.end()) {
        program.delay_time_seconds = std::max(0.001, UnitLiteralToSeconds(ValueToUnit(it->second)));
      }
      if (const auto it = node.params.find("mod_depth"); it != node.params.end()) {
        program.delay_mod_depth_seconds = std::max(0.0, UnitLiteralToSeconds(ValueToUnit(it->second)));
      }
      if (const auto it = node.params.find("mod_rate"); it != node.params.end()) {
        const auto v = ValueToUnit(it->second);
        if (v.unit == "Hz") {
          program.delay_mod_rate_hz = std::max(0.0, v.value);
        } else {
          program.delay_mod_rate_hz = std::max(0.0, ValueToNumber(it->second, program.delay_mod_rate_hz));
        }
      }
      program.delay_mix = Clamp(NodeParamNumber(node.params, "mix", program.delay_mix), 0.0, 1.0);
      program.delay_fb = Clamp(NodeParamNumber(node.params, "fb", program.delay_fb), 0.0, 0.99);
      program.delay_hicut_hz = NodeParamHz(node.params, "hicut", program.delay_hicut_hz);
      program.delay_locut_hz = NodeParamHz(node.params, "locut", program.delay_locut_hz);
      if (const auto it = node.params.find("pingpong"); it != node.params.end()) {
        if (it->second.kind == aurora::lang::ParamValue::Kind::kBool) {
          program.delay_pingpong = it->second.bool_value;
        } else {
          program.delay_pingpong = ValueToNumber(it->second, 0.0) != 0.0;
        }
      }
    }
  }
  return program;
}

inline double OnePoleLP(double x, double cutoff_hz, int sample_rate, double* state) {
  const double wc = 2.0 * kPi * std::max(1.0, cutoff_hz);
  const double dt = 1.0 / static_cast<double>(sample_rate);
  const double alpha = wc * dt / (1.0 + wc * dt);
  *state += (x - *state) * Clamp(alpha, 0.0, 1.0);
  return *state;
}

inline double OnePoleHP(double x, double cutoff_hz, int sample_rate, double* lp_state) {
  const double lp = OnePoleLP(x, cutoff_hz, sample_rate, lp_state);
  return x - lp;
}

double ReadDelayTap(const std::vector<float>& line, size_t write_idx, double delay_samples) {
  if (line.empty()) {
    return 0.0;
  }
  const double len = static_cast<double>(line.size());
  double read_pos = static_cast<double>(write_idx) - delay_samples;
  while (read_pos < 0.0) {
    read_pos += len;
  }
  while (read_pos >= len) {
    read_pos -= len;
  }
  const size_t i0 = static_cast<size_t>(read_pos);
  const size_t i1 = (i0 + 1U) % line.size();
  const double frac = read_pos - static_cast<double>(i0);
  return static_cast<double>(line[i0]) * (1.0 - frac) + static_cast<double>(line[i1]) * frac;
}

void ProcessBusStem(aurora::core::AudioStem* stem, const BusProgram& program, int sample_rate) {
  if (stem == nullptr || stem->samples.empty()) {
    return;
  }
  const int channels = std::clamp(stem->channels, 1, 2);
  const size_t frames = stem->samples.size() / static_cast<size_t>(channels);
  if (frames == 0) {
    return;
  }

  std::vector<float> work = stem->samples;

  if (program.has_delay) {
    const double max_delay_seconds =
        std::max(0.001, program.delay_time_seconds + std::max(0.0, program.delay_mod_depth_seconds) + 0.05);
    const size_t delay_size =
        std::max<size_t>(2, static_cast<size_t>(std::ceil(max_delay_seconds * static_cast<double>(sample_rate))));
    std::vector<float> dl(delay_size, 0.0f);
    std::vector<float> dr(delay_size, 0.0f);
    size_t widx = 0;
    double lfo_phase = 0.0;
    double lp_l = 0.0, lp_r = 0.0;
    double hp_lp_l = 0.0, hp_lp_r = 0.0;
    for (size_t f = 0; f < frames; ++f) {
      const size_t bi = f * static_cast<size_t>(channels);
      const double dry_l = static_cast<double>(work[bi]);
      const double dry_r = channels == 2 ? static_cast<double>(work[bi + 1U]) : dry_l;
      const double mod = (program.delay_mod_depth_seconds > 0.0 && program.delay_mod_rate_hz > 0.0)
                             ? std::sin(2.0 * kPi * lfo_phase) * program.delay_mod_depth_seconds
                             : 0.0;
      lfo_phase += program.delay_mod_rate_hz / static_cast<double>(sample_rate);
      if (lfo_phase >= 1.0) {
        lfo_phase -= std::floor(lfo_phase);
      }
      const double dly_l_smp = std::max(1.0, (program.delay_time_seconds + mod) * static_cast<double>(sample_rate));
      const double dly_r_smp = std::max(1.0, (program.delay_time_seconds - mod) * static_cast<double>(sample_rate));
      double wet_l = ReadDelayTap(dl, widx, dly_l_smp);
      double wet_r = ReadDelayTap(dr, widx, dly_r_smp);
      wet_l = OnePoleLP(wet_l, program.delay_hicut_hz, sample_rate, &lp_l);
      wet_r = OnePoleLP(wet_r, program.delay_hicut_hz, sample_rate, &lp_r);
      wet_l = OnePoleHP(wet_l, program.delay_locut_hz, sample_rate, &hp_lp_l);
      wet_r = OnePoleHP(wet_r, program.delay_locut_hz, sample_rate, &hp_lp_r);
      const double fb_l = program.delay_pingpong && channels == 2 ? wet_r : wet_l;
      const double fb_r = program.delay_pingpong && channels == 2 ? wet_l : wet_r;
      dl[widx] = static_cast<float>(dry_l + fb_l * program.delay_fb);
      dr[widx] = static_cast<float>(dry_r + fb_r * program.delay_fb);
      widx = (widx + 1U) % delay_size;
      const double out_l = dry_l * (1.0 - program.delay_mix) + wet_l * program.delay_mix;
      const double out_r = dry_r * (1.0 - program.delay_mix) + wet_r * program.delay_mix;
      work[bi] = static_cast<float>(out_l);
      if (channels == 2) {
        work[bi + 1U] = static_cast<float>(out_r);
      }
    }
  }

  if (program.has_reverb) {
    const int predelay_samples =
        std::max(1, static_cast<int>(std::llround(program.reverb_predelay_seconds * static_cast<double>(sample_rate))));
    std::vector<float> pred_l(static_cast<size_t>(predelay_samples), 0.0f);
    std::vector<float> pred_r(static_cast<size_t>(predelay_samples), 0.0f);
    size_t pred_idx = 0;
    const double size_scale = Clamp(program.reverb_size, 0.1, 1.0);
    const std::array<int, 4> comb_base{1116, 1188, 1277, 1356};
    std::vector<std::vector<float>> comb_l(4), comb_r(4);
    std::array<size_t, 4> comb_idx{0, 0, 0, 0};
    for (size_t i = 0; i < 4; ++i) {
      const int len = std::max(8, static_cast<int>(std::llround(comb_base[i] * size_scale)));
      comb_l[i].assign(static_cast<size_t>(len), 0.0f);
      comb_r[i].assign(static_cast<size_t>(len + (channels == 2 ? 23 : 0)), 0.0f);
    }
    const double fb = Clamp(1.0 - std::exp(-3.0 / (program.reverb_decay * static_cast<double>(sample_rate))), 0.2, 0.97);
    double lp_l = 0.0, lp_r = 0.0;
    double hp_lp_l = 0.0, hp_lp_r = 0.0;
    for (size_t f = 0; f < frames; ++f) {
      const size_t bi = f * static_cast<size_t>(channels);
      const double dry_l = static_cast<double>(work[bi]);
      const double dry_r = channels == 2 ? static_cast<double>(work[bi + 1U]) : dry_l;
      const double in_l = pred_l[pred_idx];
      const double in_r = pred_r[pred_idx];
      pred_l[pred_idx] = static_cast<float>(dry_l);
      pred_r[pred_idx] = static_cast<float>(dry_r);
      pred_idx = (pred_idx + 1U) % pred_l.size();
      double wet_l = 0.0, wet_r = 0.0;
      for (size_t i = 0; i < 4; ++i) {
        auto& cl = comb_l[i];
        auto& cr = comb_r[i];
        const size_t il = comb_idx[i] % cl.size();
        const size_t ir = comb_idx[i] % cr.size();
        const double yl = cl[il];
        const double yr = cr[ir];
        cl[il] = static_cast<float>(in_l + yl * fb);
        cr[ir] = static_cast<float>(in_r + yr * fb);
        comb_idx[i] += 1U;
        wet_l += yl;
        wet_r += yr;
      }
      wet_l *= 0.25;
      wet_r *= 0.25;
      wet_l = OnePoleLP(wet_l, program.reverb_hicut_hz, sample_rate, &lp_l);
      wet_r = OnePoleLP(wet_r, program.reverb_hicut_hz, sample_rate, &lp_r);
      wet_l = OnePoleHP(wet_l, program.reverb_locut_hz, sample_rate, &hp_lp_l);
      wet_r = OnePoleHP(wet_r, program.reverb_locut_hz, sample_rate, &hp_lp_r);
      if (channels == 2) {
        const double mid = 0.5 * (wet_l + wet_r);
        const double side = 0.5 * (wet_l - wet_r) * program.reverb_width;
        wet_l = mid + side;
        wet_r = mid - side;
      }
      work[bi] = static_cast<float>(dry_l * (1.0 - program.reverb_mix) + wet_l * program.reverb_mix);
      if (channels == 2) {
        work[bi + 1U] = static_cast<float>(dry_r * (1.0 - program.reverb_mix) + wet_r * program.reverb_mix);
      }
    }
  }
  stem->samples.swap(work);
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
  ApplyMonoPolicies(file, &expanded.plays);

  std::map<std::string, PatchProgram> patch_programs;
  for (const auto& patch : file.patches) {
    patch_programs[patch.name] = BuildPatchProgram(patch);
  }

  uint64_t timeline_with_env_tails = expanded.timeline_end;
  for (const auto& play : expanded.plays) {
    const auto p_it = patch_programs.find(play.patch);
    if (p_it == patch_programs.end()) {
      continue;
    }
    const auto& env = p_it->second.env;
    uint64_t extra = 0;
    if (env.enabled) {
      if (env.mode == PatchProgram::Env::Mode::kAd) {
        const uint64_t ad =
            static_cast<uint64_t>(std::llround((std::max(0.0001, env.a) + std::max(0.0001, env.d)) * result.metadata.sample_rate));
        const uint64_t note = play.dur_samples;
        extra = (ad > note) ? (ad - note) : 0;
      } else {
        extra = static_cast<uint64_t>(std::llround(std::max(0.0001, env.r) * result.metadata.sample_rate));
      }
    }
    timeline_with_env_tails = std::max<uint64_t>(timeline_with_env_tails, play.start_sample + play.dur_samples + extra);
  }

  const uint64_t tail_samples = static_cast<uint64_t>(
      std::llround(file.globals.tail_policy.fixed_seconds * static_cast<double>(result.metadata.sample_rate)));
  const uint64_t total_samples =
      RoundUpToBlock(std::max<uint64_t>(timeline_with_env_tails, 1) + tail_samples, result.metadata.block_size);
  result.metadata.total_samples = total_samples;
  result.metadata.duration_seconds = static_cast<double>(total_samples) / static_cast<double>(result.metadata.sample_rate);

  std::map<std::string, AudioStem> patch_buffers;
  for (const auto& patch : file.patches) {
    const auto program_it = patch_programs.find(patch.name);
    if (program_it == patch_programs.end()) {
      continue;
    }
    const PatchProgram& program = program_it->second;
    bool has_pan_node = false;
    bool has_stereo_width_node = false;
    bool has_depth_node = false;
    bool has_decorrelate_node = false;
    for (const auto& node : patch.graph.nodes) {
      if (node.type == "pan") {
        has_pan_node = true;
      } else if (node.type == "stereo_width") {
        has_stereo_width_node = true;
      } else if (node.type == "depth") {
        has_depth_node = true;
      } else if (node.type == "decorrelate") {
        has_decorrelate_node = true;
      }
    }
    AudioStem buffer;
    buffer.name = patch.name;
    buffer.channels =
        (program.binaural.enabled || program.pan.enabled || program.stereo_width.enabled || program.depth.enabled ||
         program.decorrelate.enabled || has_pan_node || has_stereo_width_node || has_depth_node || has_decorrelate_node ||
         patch.voice_spread.pan > 0.0)
            ? 2
            : 1;
    buffer.samples.assign(static_cast<size_t>(total_samples) * static_cast<size_t>(buffer.channels), 0.0f);
    patch_buffers[patch.name] = std::move(buffer);
  }

  const uint64_t progress_total_units =
      std::max<uint64_t>(1, static_cast<uint64_t>(expanded.plays.size()) + static_cast<uint64_t>(file.buses.size()) + 1U);
  uint64_t progress_done_units = 0;
  double last_progress_reported = -1.0;
  auto last_progress_time = std::chrono::steady_clock::now();
  auto report_progress = [&](bool force) {
    if (!options.progress_callback) {
      return;
    }
    const double pct = 100.0 * static_cast<double>(progress_done_units) / static_cast<double>(progress_total_units);
    const auto now = std::chrono::steady_clock::now();
    const bool time_due = (now - last_progress_time) >= std::chrono::milliseconds(500);
    const bool step_due = (pct - last_progress_reported) >= 0.5;
    if (!force && !time_due && !step_due) {
      return;
    }
    options.progress_callback(pct);
    last_progress_reported = pct;
    last_progress_time = now;
  };
  report_progress(true);

  std::map<std::string, std::vector<const PlayOccurrence*>> plays_by_patch;
  for (const auto& play : expanded.plays) {
    const auto patch_it = patch_programs.find(play.patch);
    const auto stem_it = patch_buffers.find(play.patch);
    if (patch_it == patch_programs.end() || stem_it == patch_buffers.end()) {
      result.warnings.push_back("Event references unknown patch '" + play.patch + "'.");
      continue;
    }
    plays_by_patch[play.patch].push_back(&play);
  }

  struct PatchFuture {
    std::string patch_name;
    std::future<size_t> future;
  };
  std::vector<PatchFuture> patch_futures;
  for (const auto& patch : file.patches) {
    const std::string patch_name = patch.name;
    const auto plays_it = plays_by_patch.find(patch_name);
    if (plays_it == plays_by_patch.end() || plays_it->second.empty()) {
      continue;
    }
    const std::vector<const PlayOccurrence*> play_list = plays_it->second;
    const int sample_rate = result.metadata.sample_rate;
    const int block_size = result.metadata.block_size;
    patch_futures.push_back(PatchFuture{
        patch_name,
        std::async(std::launch::async, [&patch_programs, &patch_buffers, &expanded, play_list, patch_name, &options, sample_rate,
                                        block_size]() {
          size_t rendered_count = 0;
          const auto patch_it = patch_programs.find(patch_name);
          const auto stem_it = patch_buffers.find(patch_name);
          if (patch_it == patch_programs.end() || stem_it == patch_buffers.end()) {
            return rendered_count;
          }
          const auto auto_it = expanded.automation.find(patch_name);
          const std::map<std::string, AutomationLane> empty_auto;
          const auto& automation = (auto_it != expanded.automation.end()) ? auto_it->second : empty_auto;
          for (const PlayOccurrence* play_ptr : play_list) {
            if (play_ptr == nullptr) {
              continue;
            }
            RenderPlayToStem(&stem_it->second, *play_ptr, patch_it->second, automation, sample_rate, block_size,
                             options.seed);
            ++rendered_count;
          }
          return rendered_count;
        })});
  }
  for (auto& task : patch_futures) {
    progress_done_units += static_cast<uint64_t>(task.future.get());
    report_progress(false);
  }

  std::map<std::string, AudioStem> bus_buffers;
  std::map<std::string, BusProgram> bus_programs;
  for (const auto& bus : file.buses) {
    const BusProgram program = BuildBusProgram(bus);
    AudioStem buffer;
    buffer.name = bus.name;
    buffer.channels = program.channels;
    buffer.samples.assign(static_cast<size_t>(total_samples) * static_cast<size_t>(buffer.channels), 0.0f);
    bus_buffers[bus.name] = std::move(buffer);
    bus_programs[bus.name] = program;
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
    AudioStem& bus_stem = bus_it->second;
    const AudioStem& src_stem = source_it->second;
    const float send_gain = static_cast<float>(DbToLinear(program.send->amount_db));
    const int src_channels = src_stem.channels;
    const int bus_channels = bus_stem.channels;
    for (size_t frame = 0; frame < static_cast<size_t>(total_samples); ++frame) {
      float src_l = 0.0f;
      float src_r = 0.0f;
      if (src_channels == 1) {
        src_l = src_stem.samples[frame];
        src_r = src_l;
      } else {
        const size_t base = frame * 2U;
        src_l = src_stem.samples[base];
        src_r = src_stem.samples[base + 1U];
      }
      if (bus_channels == 1) {
        bus_stem.samples[frame] += 0.5f * (src_l + src_r) * send_gain;
      } else {
        const size_t base = frame * 2U;
        bus_stem.samples[base] += src_l * send_gain;
        bus_stem.samples[base + 1U] += src_r * send_gain;
      }
    }
  }

  struct BusFuture {
    std::string bus_name;
    std::future<void> future;
  };
  std::vector<BusFuture> bus_futures;
  for (const auto& bus : file.buses) {
    const std::string bus_name = bus.name;
    auto buffer_it = bus_buffers.find(bus_name);
    auto program_it = bus_programs.find(bus_name);
    if (buffer_it == bus_buffers.end() || program_it == bus_programs.end()) {
      continue;
    }
    AudioStem* buffer_ptr = &buffer_it->second;
    const BusProgram* program_ptr = &program_it->second;
    bus_futures.push_back(BusFuture{
        bus_name, std::async(std::launch::async, [buffer_ptr, program_ptr, sample_rate = result.metadata.sample_rate]() {
          ProcessBusStem(buffer_ptr, *program_ptr, sample_rate);
        })});
  }
  for (auto& task : bus_futures) {
    task.future.get();
    ++progress_done_units;
    report_progress(false);
  }

  result.patch_stems.reserve(file.patches.size());
  for (const auto& patch : file.patches) {
    AudioStem stem;
    stem.channels = patch_buffers[patch.name].channels;
    stem.name = patch.out_stem.empty() ? patch.name : patch.out_stem;
    stem.samples = patch_buffers[patch.name].samples;
    result.patch_stems.push_back(std::move(stem));
  }

  result.bus_stems.reserve(file.buses.size());
  for (const auto& bus : file.buses) {
    AudioStem stem;
    stem.channels = bus_buffers[bus.name].channels;
    stem.name = bus.out_stem.empty() ? bus.name : bus.out_stem;
    stem.samples = bus_buffers[bus.name].samples;
    result.bus_stems.push_back(std::move(stem));
  }

  result.master.name = "master";
  bool any_stereo = false;
  for (const auto& stem : result.patch_stems) {
    if (stem.channels == 2) {
      any_stereo = true;
      break;
    }
  }
  if (!any_stereo) {
    for (const auto& stem : result.bus_stems) {
      if (stem.channels == 2) {
        any_stereo = true;
        break;
      }
    }
  }
  result.master.channels = any_stereo ? 2 : 1;
  result.master.samples.assign(static_cast<size_t>(total_samples) * static_cast<size_t>(result.master.channels), 0.0f);

  const auto mix_stem_into_master = [&](const AudioStem& stem) {
    if (stem.channels == 1 && result.master.channels == 1) {
      for (size_t i = 0; i < static_cast<size_t>(total_samples); ++i) {
        result.master.samples[i] += stem.samples[i];
      }
      return;
    }
    if (stem.channels == 2 && result.master.channels == 2) {
      for (size_t i = 0; i < static_cast<size_t>(total_samples) * 2U; ++i) {
        result.master.samples[i] += stem.samples[i];
      }
      return;
    }
    if (stem.channels == 1 && result.master.channels == 2) {
      for (size_t frame = 0; frame < static_cast<size_t>(total_samples); ++frame) {
        const float s = stem.samples[frame];
        const size_t base = frame * 2U;
        result.master.samples[base] += s;
        result.master.samples[base + 1U] += s;
      }
      return;
    }
    if (stem.channels == 2 && result.master.channels == 1) {
      for (size_t frame = 0; frame < static_cast<size_t>(total_samples); ++frame) {
        const size_t base = frame * 2U;
        result.master.samples[frame] += 0.5f * (stem.samples[base] + stem.samples[base + 1U]);
      }
    }
  };

  for (const auto& stem : result.patch_stems) {
    mix_stem_into_master(stem);
  }
  for (const auto& stem : result.bus_stems) {
    mix_stem_into_master(stem);
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

  progress_done_units = progress_total_units;
  report_progress(true);

  return result;
}

}  // namespace aurora::core
