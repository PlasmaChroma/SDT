#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "aurora/lang/ast.hpp"

namespace aurora::core {

struct TempoMapPoint {
  double at_seconds = 0.0;
  double bpm = 60.0;
};

struct TempoMap {
  std::vector<TempoMapPoint> points;
};

inline double SecondsFromUnit(const aurora::lang::UnitNumber& value, double bpm) {
  if (value.unit.empty() || value.unit == "s") {
    return value.value;
  }
  if (value.unit == "ms") {
    return value.value / 1000.0;
  }
  if (value.unit == "min") {
    return value.value * 60.0;
  }
  if (value.unit == "h") {
    return value.value * 3600.0;
  }
  if (value.unit == "beats") {
    return value.value * 60.0 / bpm;
  }
  throw std::runtime_error("Unsupported time unit: " + value.unit);
}

inline TempoMap BuildTempoMap(const aurora::lang::GlobalsDefinition& globals) {
  TempoMap map;
  const double base_bpm = globals.tempo.value_or(60.0);
  map.points.push_back({0.0, base_bpm});

  for (const auto& p : globals.tempo_map) {
    double at_seconds = 0.0;
    if (p.at.unit == "beats") {
      double remaining_beats = p.at.value;
      for (size_t i = 0; i < map.points.size(); ++i) {
        const double bpm = map.points[i].bpm;
        const double seg_start = map.points[i].at_seconds;
        const double seg_end = (i + 1 < map.points.size()) ? map.points[i + 1].at_seconds : std::numeric_limits<double>::infinity();
        const double seg_len = seg_end - seg_start;
        const double seg_beats = std::isinf(seg_len) ? std::numeric_limits<double>::infinity() : seg_len * bpm / 60.0;
        if (remaining_beats <= seg_beats) {
          at_seconds = seg_start + (remaining_beats * 60.0 / bpm);
          break;
        }
        remaining_beats -= seg_beats;
      }
    } else {
      at_seconds = SecondsFromUnit(p.at, base_bpm);
    }
    map.points.push_back({at_seconds, p.bpm});
  }

  std::sort(map.points.begin(), map.points.end(), [](const TempoMapPoint& a, const TempoMapPoint& b) {
    if (a.at_seconds == b.at_seconds) {
      return a.bpm < b.bpm;
    }
    return a.at_seconds < b.at_seconds;
  });
  return map;
}

inline double BeatsToSeconds(double beats, const TempoMap& tempo_map) {
  if (beats <= 0.0) {
    return 0.0;
  }
  double remaining = beats;
  for (size_t i = 0; i < tempo_map.points.size(); ++i) {
    const double bpm = tempo_map.points[i].bpm;
    const double start = tempo_map.points[i].at_seconds;
    const double end = (i + 1 < tempo_map.points.size()) ? tempo_map.points[i + 1].at_seconds : std::numeric_limits<double>::infinity();
    const double seg_seconds = end - start;
    const double seg_beats = std::isinf(seg_seconds) ? std::numeric_limits<double>::infinity() : seg_seconds * bpm / 60.0;
    if (remaining <= seg_beats) {
      return start + remaining * 60.0 / bpm;
    }
    remaining -= seg_beats;
  }
  const TempoMapPoint& last = tempo_map.points.back();
  return last.at_seconds + remaining * 60.0 / last.bpm;
}

inline double SecondsToBeats(double seconds, const TempoMap& tempo_map) {
  if (seconds <= 0.0) {
    return 0.0;
  }
  double beats = 0.0;
  double remaining = seconds;
  for (size_t i = 0; i < tempo_map.points.size(); ++i) {
    const double bpm = tempo_map.points[i].bpm;
    const double start = tempo_map.points[i].at_seconds;
    const double end = (i + 1 < tempo_map.points.size()) ? tempo_map.points[i + 1].at_seconds
                                                          : std::numeric_limits<double>::infinity();
    if (remaining <= start) {
      break;
    }
    const double seg_end = std::min(remaining, end);
    const double seg_seconds = std::max(0.0, seg_end - start);
    beats += seg_seconds * bpm / 60.0;
    if (remaining <= end) {
      break;
    }
  }
  return beats;
}

inline double OffsetSecondsFrom(double anchor_seconds, const aurora::lang::UnitNumber& offset, const TempoMap& tempo_map) {
  if (offset.unit == "beats") {
    const double anchor_beats = SecondsToBeats(anchor_seconds, tempo_map);
    const double end_seconds = BeatsToSeconds(anchor_beats + offset.value, tempo_map);
    return end_seconds - anchor_seconds;
  }
  return SecondsFromUnit(offset, tempo_map.points.front().bpm);
}

inline double ToSeconds(const aurora::lang::UnitNumber& value, const TempoMap& tempo_map) {
  if (value.unit == "beats") {
    return BeatsToSeconds(value.value, tempo_map);
  }
  return SecondsFromUnit(value, tempo_map.points.front().bpm);
}

inline uint64_t ToSamples(const aurora::lang::UnitNumber& value, const TempoMap& tempo_map, int sample_rate) {
  const double seconds = ToSeconds(value, tempo_map);
  return static_cast<uint64_t>(std::llround(seconds * static_cast<double>(sample_rate)));
}

inline uint64_t RoundUpToBlock(uint64_t samples, int block_size) {
  const uint64_t block = static_cast<uint64_t>(block_size);
  if (block == 0U) {
    return samples;
  }
  const uint64_t rem = samples % block;
  if (rem == 0U) {
    return samples;
  }
  return samples + (block - rem);
}

}  // namespace aurora::core
