#include "aurora/io/midi_writer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace aurora::io {
namespace {

constexpr uint16_t kPpq = 480;

void WriteU16BE(std::ofstream& out, uint16_t v) {
  out.put(static_cast<char>((v >> 8) & 0xFF));
  out.put(static_cast<char>(v & 0xFF));
}

void WriteU32BE(std::ofstream& out, uint32_t v) {
  out.put(static_cast<char>((v >> 24) & 0xFF));
  out.put(static_cast<char>((v >> 16) & 0xFF));
  out.put(static_cast<char>((v >> 8) & 0xFF));
  out.put(static_cast<char>(v & 0xFF));
}

void WriteVarLen(std::vector<uint8_t>* data, uint32_t value) {
  uint8_t bytes[5];
  int count = 0;
  bytes[count++] = static_cast<uint8_t>(value & 0x7F);
  while ((value >>= 7U) != 0U) {
    bytes[count++] = static_cast<uint8_t>((value & 0x7F) | 0x80U);
  }
  for (int i = count - 1; i >= 0; --i) {
    data->push_back(bytes[i]);
  }
}

double SecondsToTicks(double seconds, const aurora::core::TempoMap& tempo_map) {
  double ticks = 0.0;
  for (size_t i = 0; i < tempo_map.points.size(); ++i) {
    const double start = tempo_map.points[i].at_seconds;
    const double bpm = tempo_map.points[i].bpm;
    const double end =
        (i + 1 < tempo_map.points.size()) ? tempo_map.points[i + 1].at_seconds : std::numeric_limits<double>::infinity();
    if (seconds <= start) {
      break;
    }
    const double segment_end = std::min(seconds, end);
    const double seg_seconds = std::max(0.0, segment_end - start);
    ticks += seg_seconds * (bpm / 60.0) * static_cast<double>(kPpq);
    if (seconds <= end) {
      break;
    }
  }
  return ticks;
}

bool TickDoubleToU32(double ticks, const char* context, uint32_t* out, std::string* error) {
  if (!std::isfinite(ticks) || ticks < 0.0 || ticks > static_cast<double>(std::numeric_limits<uint32_t>::max())) {
    if (error != nullptr) {
      *error = std::string("MIDI tick overflow while encoding ") + context + ".";
    }
    return false;
  }
  *out = static_cast<uint32_t>(std::llround(ticks));
  return true;
}

bool SampleToTick(uint64_t sample, int sample_rate, const aurora::core::TempoMap& tempo_map, uint32_t* out, std::string* error) {
  const double seconds = static_cast<double>(sample) / static_cast<double>(sample_rate);
  return TickDoubleToU32(SecondsToTicks(seconds, tempo_map), "note/cc event", out, error);
}

struct MidiEvent {
  uint32_t tick = 0;
  std::vector<uint8_t> bytes;
  int order = 0;
};

std::vector<uint8_t> EncodeTrack(std::vector<MidiEvent> events, uint32_t end_tick) {
  events.push_back(MidiEvent{end_tick, {0xFF, 0x2F, 0x00}, 9999});
  std::stable_sort(events.begin(), events.end(), [](const MidiEvent& a, const MidiEvent& b) {
    if (a.tick == b.tick) {
      return a.order < b.order;
    }
    return a.tick < b.tick;
  });

  std::vector<uint8_t> data;
  uint32_t prev_tick = 0;
  for (const auto& ev : events) {
    const uint32_t delta = ev.tick - prev_tick;
    WriteVarLen(&data, delta);
    data.insert(data.end(), ev.bytes.begin(), ev.bytes.end());
    prev_tick = ev.tick;
  }
  return data;
}

MidiEvent MakeTempoEvent(uint32_t tick, double bpm) {
  const double safe_bpm = std::max(1.0, bpm);
  const uint32_t us_per_quarter = static_cast<uint32_t>(std::llround(60000000.0 / safe_bpm));
  MidiEvent event;
  event.tick = tick;
  event.order = 0;
  event.bytes = {0xFF, 0x51, 0x03, static_cast<uint8_t>((us_per_quarter >> 16) & 0xFF),
                 static_cast<uint8_t>((us_per_quarter >> 8) & 0xFF), static_cast<uint8_t>(us_per_quarter & 0xFF)};
  return event;
}

}  // namespace

bool WriteMidiFormat1(const std::filesystem::path& path, const std::vector<aurora::core::MidiTrackData>& tracks,
                      const aurora::core::TempoMap& tempo_map, uint64_t total_samples, int sample_rate,
                      std::string* error) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary);
  if (!out.is_open()) {
    if (error != nullptr) {
      *error = "Failed to open MIDI file for writing: " + path.string();
    }
    return false;
  }

  uint32_t end_tick = 0;
  if (!TickDoubleToU32(
          std::ceil(SecondsToTicks(static_cast<double>(total_samples) / static_cast<double>(sample_rate), tempo_map)),
          "track length", &end_tick, error)) {
    return false;
  }

  std::vector<std::vector<uint8_t>> encoded_tracks;

  std::vector<MidiEvent> tempo_events;
  for (const auto& point : tempo_map.points) {
    uint32_t tick = 0;
    if (!TickDoubleToU32(SecondsToTicks(point.at_seconds, tempo_map), "tempo event", &tick, error)) {
      return false;
    }
    tempo_events.push_back(MakeTempoEvent(tick, point.bpm));
  }
  if (tempo_events.empty()) {
    tempo_events.push_back(MakeTempoEvent(0, 60.0));
  }
  encoded_tracks.push_back(EncodeTrack(std::move(tempo_events), end_tick));

  for (const auto& track : tracks) {
    std::vector<MidiEvent> events;

    if (!track.name.empty()) {
      MidiEvent name;
      name.tick = 0;
      name.order = 0;
      name.bytes = {0xFF, 0x03, static_cast<uint8_t>(std::min<size_t>(255, track.name.size()))};
      for (size_t i = 0; i < std::min<size_t>(255, track.name.size()); ++i) {
        name.bytes.push_back(static_cast<uint8_t>(track.name[i]));
      }
      events.push_back(std::move(name));
    }

    for (const auto& note : track.notes) {
      uint32_t on_tick = 0;
      uint32_t off_tick = 0;
      if (!SampleToTick(note.start_sample, sample_rate, tempo_map, &on_tick, error)) {
        return false;
      }
      if (!SampleToTick(note.end_sample, sample_rate, tempo_map, &off_tick, error)) {
        return false;
      }
      MidiEvent on;
      on.tick = on_tick;
      on.order = 2;
      on.bytes = {static_cast<uint8_t>(0x90 | (note.channel & 0x0F)), static_cast<uint8_t>(note.note & 0x7F),
                  static_cast<uint8_t>(note.velocity & 0x7F)};
      events.push_back(on);

      MidiEvent off;
      off.tick = std::max(off_tick, on_tick + 1);
      off.order = 1;
      off.bytes = {static_cast<uint8_t>(0x80 | (note.channel & 0x0F)), static_cast<uint8_t>(note.note & 0x7F), 0x00};
      events.push_back(off);
    }

    for (const auto& cc : track.ccs) {
      uint32_t cc_tick = 0;
      if (!SampleToTick(cc.sample, sample_rate, tempo_map, &cc_tick, error)) {
        return false;
      }
      MidiEvent cce;
      cce.tick = cc_tick;
      cce.order = 3;
      cce.bytes = {static_cast<uint8_t>(0xB0 | (cc.channel & 0x0F)), static_cast<uint8_t>(cc.cc & 0x7F),
                   static_cast<uint8_t>(cc.value & 0x7F)};
      events.push_back(cce);
    }

    encoded_tracks.push_back(EncodeTrack(std::move(events), end_tick));
  }

  out.write("MThd", 4);
  WriteU32BE(out, 6);
  WriteU16BE(out, 1);
  WriteU16BE(out, static_cast<uint16_t>(encoded_tracks.size()));
  WriteU16BE(out, kPpq);

  for (const auto& track_data : encoded_tracks) {
    out.write("MTrk", 4);
    WriteU32BE(out, static_cast<uint32_t>(track_data.size()));
    out.write(reinterpret_cast<const char*>(track_data.data()), static_cast<std::streamsize>(track_data.size()));
  }

  if (!out.good()) {
    if (error != nullptr) {
      *error = "Failed while writing MIDI data: " + path.string();
    }
    return false;
  }
  return true;
}

}  // namespace aurora::io
