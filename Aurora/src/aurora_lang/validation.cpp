#include "aurora/lang/validation.hpp"

#include <set>
#include <string>

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

  std::set<std::string> patch_names;
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
  }

  for (const auto& patch : file.patches) {
    if (patch.send.has_value() && !patch.send->bus.empty() && !bus_names.contains(patch.send->bus)) {
      out.errors.push_back("Patch '" + patch.name + "' references unknown send bus '" + patch.send->bus + "'.");
    }
  }

  if (!file.globals.tempo.has_value() && file.globals.tempo_map.empty()) {
    out.warnings.push_back("No tempo specified; defaulting to 60 BPM.");
  }

  out.ok = out.errors.empty();
  return out;
}

}  // namespace aurora::lang
