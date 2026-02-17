#pragma once

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "aurora/lang/value.hpp"

namespace aurora::lang {

struct TempoPoint {
  UnitNumber at;
  double bpm = 60.0;
};

struct AssetsDefinition {
  std::string samples_dir;
  std::map<std::string, std::string> samples;
};

struct ImportDefinition {
  std::string source;
  std::string alias;
};

struct OutputsDefinition {
  std::string stems_dir = "renders/stems";
  std::string midi_dir = "renders/midi";
  std::string mix_dir = "renders/mix";
  std::string meta_dir = "renders/meta";
  std::string master = "master.wav";
  std::string render_json = "render.json";
};

struct TailPolicy {
  enum class Kind { kFixed };
  Kind kind = Kind::kFixed;
  double fixed_seconds = 0.0;
};

struct GlobalsDefinition {
  int sr = 48000;
  int block = 256;
  std::optional<double> tempo;
  std::vector<TempoPoint> tempo_map;
  TailPolicy tail_policy;
};

struct GraphNode {
  std::string id;
  std::string type;
  std::map<std::string, ParamValue> params;
};

struct GraphConnection {
  std::string from;
  std::string to;
  std::string rate = "audio";
  std::map<std::string, ParamValue> map;
};

struct GraphDefinition {
  std::vector<GraphNode> nodes;
  std::vector<GraphConnection> connections;
  std::string out;
};

struct SendDefinition {
  std::string bus;
  double amount_db = 0.0;
};

struct PatchDefinition {
  struct BinauralDefinition {
    bool enabled = false;
    double shift_hz = 0.0;
    double mix = 1.0;
  };
  struct VoiceSpreadDefinition {
    bool enabled = false;
    double pan = 0.0;
    double detune_semitones = 0.0;
    double delay_seconds = 0.0;
  };
  struct StagePositionDefinition {
    bool enabled = false;
    double pan = 0.0;
    double depth = 0.0;
  };

  std::string name;
  int poly = 8;
  std::string voice_steal = "oldest";
  bool mono = false;
  bool legato = false;
  std::string retrig = "always";
  BinauralDefinition binaural;
  VoiceSpreadDefinition voice_spread;
  StagePositionDefinition stage_position;
  std::string out_stem;
  std::optional<SendDefinition> send;
  GraphDefinition graph;
};

struct BusDefinition {
  std::string name;
  int channels = 1;
  std::string out_stem;
  GraphDefinition graph;
};

struct PlayEvent {
  std::string patch;
  UnitNumber at;
  UnitNumber dur;
  double vel = 1.0;
  std::vector<ParamValue> pitch_values;
  std::map<std::string, ParamValue> params;
};

struct AutomateEvent {
  std::string target;
  std::string curve = "linear";
  std::vector<std::pair<UnitNumber, ParamValue>> points;
};

struct SeqEvent {
  std::string patch;
  std::map<std::string, ParamValue> fields;
};

struct SetEvent {
  std::string target;
  ParamValue value;
};

using SectionEvent = std::variant<PlayEvent, AutomateEvent, SeqEvent, SetEvent>;

struct SectionDefinition {
  std::string name;
  UnitNumber at;
  UnitNumber dur;
  std::map<std::string, ParamValue> directives;
  std::vector<SectionEvent> events;
};

struct AuroraFile {
  std::string version;
  std::vector<ImportDefinition> imports;
  AssetsDefinition assets;
  OutputsDefinition outputs;
  GlobalsDefinition globals;
  std::vector<BusDefinition> buses;
  std::vector<PatchDefinition> patches;
  std::vector<SectionDefinition> sections;
};

}  // namespace aurora::lang
