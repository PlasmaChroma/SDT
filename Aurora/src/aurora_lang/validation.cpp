#include "aurora/lang/validation.hpp"

#include <set>
#include <map>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace aurora::lang {
namespace {

bool HasMajorVersionOne(const std::string& version) {
  if (version.empty()) {
    return false;
  }
  const size_t dot = version.find('.');
  const std::string major = dot == std::string::npos ? version : version.substr(0, dot);
  return major == "1";
}

bool IsCvNodeType(const std::string& node_type) {
  return node_type == "cv_scale" || node_type == "cv_offset" || node_type == "cv_mix" || node_type == "cv_slew" ||
         node_type == "cv_clip" || node_type == "cv_invert" || node_type == "cv_sample_hold" || node_type == "cv_cmp" ||
         node_type == "cv_logic";
}

bool IsControlSourceType(const std::string& node_type) {
  return node_type == "env_adsr" || node_type == "env_ad" || node_type == "env_ar" || node_type == "lfo" ||
         IsCvNodeType(node_type);
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
    return std::make_pair(single, patch_index + 1);
  }
  if (patch_index + 1 < parts.size()) {
    const std::string dotted = single + "." + parts[patch_index + 1];
    if (patch_names.contains(dotted)) {
      return std::make_pair(dotted, patch_index + 2);
    }
  }
  return std::nullopt;
}

enum class PortKind { kAudioIn, kControlIn, kAudioOut, kControlOut };

PortKind ClassifyPort(const std::string& node_type, const std::string& port_name, bool is_source) {
  if (is_source) {
    if (IsControlSourceType(node_type)) {
      return PortKind::kControlOut;
    }
    return PortKind::kAudioOut;
  }
  if (port_name.rfind("in", 0) == 0) {
    return IsCvNodeType(node_type) ? PortKind::kControlIn : PortKind::kAudioIn;
  }
  return PortKind::kControlIn;
}

void ValidateGraphConnections(const std::string& owner_label, const GraphDefinition& graph, ValidationResult* out) {
  std::map<std::string, std::string> node_type_by_id;
  for (const auto& node : graph.nodes) {
    node_type_by_id[node.id] = node.type;
  }
  for (const auto& conn : graph.connections) {
    const auto [src_node, src_port] = SplitNodePort(conn.from);
    const auto [dst_node, dst_port] = SplitNodePort(conn.to);
    const auto src_it = node_type_by_id.find(src_node);
    const auto dst_it = node_type_by_id.find(dst_node);
    if (src_it == node_type_by_id.end()) {
      out->errors.push_back(owner_label + " connection references unknown source node '" + src_node + "'.");
      continue;
    }
    if (dst_it == node_type_by_id.end()) {
      out->errors.push_back(owner_label + " connection references unknown destination node '" + dst_node + "'.");
      continue;
    }
    const PortKind src_kind = ClassifyPort(src_it->second, src_port, true);
    const PortKind dst_kind = ClassifyPort(dst_it->second, dst_port, false);
    if (src_kind == PortKind::kAudioOut && dst_kind == PortKind::kControlIn) {
      out->errors.push_back(owner_label + " connection '" + conn.from + "' -> '" + conn.to +
                            "' is invalid: audio source cannot drive control input.");
    }
    if (src_kind == PortKind::kControlOut && dst_kind == PortKind::kAudioIn) {
      out->errors.push_back(owner_label + " connection '" + conn.from + "' -> '" + conn.to +
                            "' is invalid: control source cannot drive audio input.");
    }
  }
}

void ValidateControlFeedbackCycles(const std::string& owner_label, const GraphDefinition& graph, ValidationResult* out) {
  std::map<std::string, std::string> node_type_by_id;
  for (const auto& node : graph.nodes) {
    node_type_by_id[node.id] = node.type;
  }
  std::map<std::string, std::set<std::string>> adjacency;
  for (const auto& conn : graph.connections) {
    const auto [src_node, src_port] = SplitNodePort(conn.from);
    const auto [dst_node, dst_port] = SplitNodePort(conn.to);
    const auto src_it = node_type_by_id.find(src_node);
    const auto dst_it = node_type_by_id.find(dst_node);
    if (src_it == node_type_by_id.end() || dst_it == node_type_by_id.end()) {
      continue;
    }
    const PortKind src_kind = ClassifyPort(src_it->second, src_port, true);
    const PortKind dst_kind = ClassifyPort(dst_it->second, dst_port, false);
    if (src_kind == PortKind::kControlOut && dst_kind == PortKind::kControlIn) {
      adjacency[src_node].insert(dst_node);
    }
  }

  std::set<std::string> visiting;
  std::set<std::string> visited;
  bool cycle_found = false;
  std::function<void(const std::string&)> dfs = [&](const std::string& node) {
    if (cycle_found) {
      return;
    }
    visiting.insert(node);
    for (const auto& next : adjacency[node]) {
      if (visiting.contains(next)) {
        cycle_found = true;
        return;
      }
      if (!visited.contains(next)) {
        dfs(next);
      }
    }
    visiting.erase(node);
    visited.insert(node);
  };
  for (const auto& [node, _] : adjacency) {
    if (!visited.contains(node)) {
      dfs(node);
    }
    if (cycle_found) {
      break;
    }
  }
  if (cycle_found) {
    out->warnings.push_back(owner_label +
                            " contains a control feedback cycle; renderer applies deterministic one-sample delay fallback.");
  }
}

}  // namespace

ValidationResult Validate(const AuroraFile& file) {
  ValidationResult out;

  if (!HasMajorVersionOne(file.version)) {
    out.errors.push_back("Unsupported language major version: " + file.version);
  }

  if (file.patches.empty()) {
    out.errors.push_back("At least one patch is required.");
  }

  if (file.sections.empty()) {
    out.errors.push_back("score must contain at least one section.");
  }

  if (file.globals.block != 256) {
    out.errors.push_back("globals.block must be 256 in v1.0.");
  }
  if (file.globals.sr <= 0) {
    out.errors.push_back("globals.sr must be > 0.");
  }

  std::set<std::string> patch_names;
  std::set<std::string> import_aliases;
  for (const auto& import : file.imports) {
    if (import.source.empty()) {
      out.errors.push_back("Import source path cannot be empty.");
    }
    if (import.alias.empty()) {
      out.errors.push_back("Import alias cannot be empty.");
      continue;
    }
    if (!import_aliases.insert(import.alias).second) {
      out.errors.push_back("Duplicate import alias: " + import.alias);
    }
  }
  std::set<std::string> stem_names;
  for (const auto& patch : file.patches) {
    if (!patch_names.insert(patch.name).second) {
      out.errors.push_back("Duplicate patch name: " + patch.name);
    }
    if (patch.out_stem.empty()) {
      out.errors.push_back("Patch '" + patch.name + "' must define out: stem(\"...\").");
    } else if (!stem_names.insert(patch.out_stem).second) {
      out.warnings.push_back("Stem name reused by multiple outputs: " + patch.out_stem);
    }
    if (patch.graph.nodes.empty()) {
      out.errors.push_back("Patch '" + patch.name + "' graph must contain nodes.");
    }
    if (patch.graph.out.empty()) {
      out.errors.push_back("Patch '" + patch.name + "' graph io.out is required.");
    }
    if (!patch.retrig.empty() && patch.retrig != "always" && patch.retrig != "legato" && patch.retrig != "never") {
      out.warnings.push_back("Patch '" + patch.name + "' retrig should be 'always', 'legato', or 'never'.");
    }
    ValidateGraphConnections("Patch '" + patch.name + "' graph", patch.graph, &out);
    ValidateControlFeedbackCycles("Patch '" + patch.name + "' graph", patch.graph, &out);
    if (patch.binaural.enabled) {
      if (patch.binaural.mix < 0.0 || patch.binaural.mix > 1.0) {
        out.warnings.push_back("Patch '" + patch.name + "' binaural.mix is outside [0,1]; renderer will clamp.");
      }
      bool has_oscillator = false;
      for (const auto& node : patch.graph.nodes) {
        if (node.type.rfind("osc_", 0) == 0) {
          has_oscillator = true;
          break;
        }
      }
      if (!has_oscillator) {
        out.warnings.push_back("Patch '" + patch.name + "' has binaural enabled but no oscillator nodes.");
      }
    }
  }

  std::set<std::string> bus_names;
  for (const auto& bus : file.buses) {
    if (!bus_names.insert(bus.name).second) {
      out.errors.push_back("Duplicate bus name: " + bus.name);
    }
    if (bus.out_stem.empty()) {
      out.errors.push_back("Bus '" + bus.name + "' must define out: stem(\"...\").");
    } else if (!stem_names.insert(bus.out_stem).second) {
      out.warnings.push_back("Stem name reused by multiple outputs: " + bus.out_stem);
    }
    if (bus.graph.nodes.empty()) {
      out.errors.push_back("Bus '" + bus.name + "' graph must contain nodes.");
    }
    if (bus.graph.out.empty()) {
      out.errors.push_back("Bus '" + bus.name + "' graph io.out is required.");
    }
    if (bus.channels < 1 || bus.channels > 2) {
      out.errors.push_back("Bus '" + bus.name + "' channels must be 1 or 2.");
    }
    ValidateGraphConnections("Bus '" + bus.name + "' graph", bus.graph, &out);
    ValidateControlFeedbackCycles("Bus '" + bus.name + "' graph", bus.graph, &out);
  }

  for (const auto& patch : file.patches) {
    if (patch.send.has_value() && !patch.send->bus.empty() && !bus_names.contains(patch.send->bus)) {
      out.errors.push_back("Patch '" + patch.name + "' references unknown send bus '" + patch.send->bus + "'.");
    }
  }

  if (!file.globals.tempo.has_value() && file.globals.tempo_map.empty()) {
    out.warnings.push_back("No tempo specified; defaulting to 60 BPM.");
  }

  for (const auto& section : file.sections) {
    for (const auto& event : section.events) {
      if (std::holds_alternative<PlayEvent>(event)) {
        const auto& play = std::get<PlayEvent>(event);
        if (!patch_names.contains(play.patch)) {
          out.errors.push_back("Section '" + section.name + "' references unknown patch '" + play.patch + "'.");
        }
        continue;
      }
      if (std::holds_alternative<SeqEvent>(event)) {
        const auto& seq = std::get<SeqEvent>(event);
        if (!patch_names.contains(seq.patch)) {
          out.errors.push_back("Section '" + section.name + "' seq references unknown patch '" + seq.patch + "'.");
        }
        continue;
      }
      if (std::holds_alternative<SetEvent>(event)) {
        const auto& set = std::get<SetEvent>(event);
        const auto parts = SplitByDot(set.target);
        if (parts.empty() || parts.front() != "patch") {
          continue;
        }
        const auto patch_ref = ResolvePatchRefFromTarget(parts, 1U, patch_names);
        if (!patch_ref.has_value()) {
          out.errors.push_back("Section '" + section.name + "' set target references unknown patch in '" + set.target + "'.");
          continue;
        }
        if (parts.size() < patch_ref->second + 2U) {
          out.errors.push_back("Section '" + section.name + "' set target is incomplete: '" + set.target + "'.");
        }
        continue;
      }
      if (std::holds_alternative<AutomateEvent>(event)) {
        const auto& automate = std::get<AutomateEvent>(event);
        const auto parts = SplitByDot(automate.target);
        if (parts.empty() || parts.front() != "patch") {
          continue;
        }
        const auto patch_ref = ResolvePatchRefFromTarget(parts, 1U, patch_names);
        if (!patch_ref.has_value()) {
          out.errors.push_back("Section '" + section.name +
                               "' automate target references unknown patch in '" + automate.target + "'.");
          continue;
        }
        if (parts.size() < patch_ref->second + 2U) {
          out.errors.push_back("Section '" + section.name + "' automate target is incomplete: '" + automate.target + "'.");
        }
      }
    }
  }

  out.ok = out.errors.empty();
  return out;
}

}  // namespace aurora::lang
