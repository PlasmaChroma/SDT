#include "aurora/io/analysis_writer.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

namespace aurora::io {
namespace {

std::string EscapeJson(const std::string& in) {
  std::ostringstream out;
  for (const char ch : in) {
    switch (ch) {
      case '"':
        out << "\\\"";
        break;
      case '\\':
        out << "\\\\";
        break;
      case '\n':
        out << "\\n";
        break;
      case '\r':
        out << "\\r";
        break;
      case '\t':
        out << "\\t";
        break;
      default:
        out << ch;
        break;
    }
  }
  return out.str();
}

void WriteSpectralRatios(std::ofstream& out, const aurora::core::SpectralRatios& r, int indent) {
  const std::string pad(static_cast<size_t>(indent), ' ');
  out << pad << "\"sub\": " << r.sub << ",\n";
  out << pad << "\"low\": " << r.low << ",\n";
  out << pad << "\"low_mid\": " << r.low_mid << ",\n";
  out << pad << "\"mid\": " << r.mid << ",\n";
  out << pad << "\"presence\": " << r.presence << ",\n";
  out << pad << "\"high\": " << r.high << ",\n";
  out << pad << "\"air\": " << r.air << ",\n";
  out << pad << "\"ultra\": " << r.ultra << "\n";
}

void WriteSpectrogram(std::ofstream& out, const aurora::core::SpectrogramArtifact& spec, int indent) {
  const std::string pad(static_cast<size_t>(indent), ' ');
  out << pad << "\"enabled\": " << (spec.enabled ? "true" : "false");
  if (!spec.path.empty()) {
    out << ",\n" << pad << "\"path\": \"" << EscapeJson(spec.path) << "\"";
  }
  if (!spec.paths.empty()) {
    out << ",\n" << pad << "\"paths\": [\n";
    for (size_t i = 0; i < spec.paths.size(); ++i) {
      out << pad << "  \"" << EscapeJson(spec.paths[i]) << "\"";
      if (i + 1 < spec.paths.size()) {
        out << ",";
      }
      out << "\n";
    }
    out << pad << "]";
  }
  if (!spec.error.empty()) {
    out << ",\n" << pad << "\"error\": \"" << EscapeJson(spec.error) << "\"";
  }
  if (spec.enabled) {
    out << ",\n" << pad << "\"mode\": \"" << EscapeJson(spec.mode) << "\"";
    out << ",\n" << pad << "\"sr\": " << spec.sr;
    out << ",\n" << pad << "\"window\": " << spec.window;
    out << ",\n" << pad << "\"hop\": " << spec.hop;
    out << ",\n" << pad << "\"nfft\": " << spec.nfft;
    out << ",\n" << pad << "\"freq_scale\": \"" << EscapeJson(spec.freq_scale) << "\"";
    out << ",\n" << pad << "\"min_hz\": " << spec.min_hz;
    out << ",\n" << pad << "\"max_hz\": " << spec.max_hz;
    out << ",\n" << pad << "\"db_min\": " << spec.db_min;
    out << ",\n" << pad << "\"db_max\": " << spec.db_max;
    out << ",\n" << pad << "\"colormap\": \"" << EscapeJson(spec.colormap) << "\"";
    out << ",\n" << pad << "\"width_px\": " << spec.width_px;
    out << ",\n" << pad << "\"height_px\": " << spec.height_px;
    out << ",\n" << pad << "\"gamma\": " << spec.gamma;
    out << ",\n" << pad << "\"smoothing_bins\": " << spec.smoothing_bins;
  }
  out << "\n";
}

void WriteFileAnalysis(std::ofstream& out, const aurora::core::FileAnalysis& item, int indent) {
  const std::string pad(static_cast<size_t>(indent), ' ');
  out << pad << "\"name\": \"" << EscapeJson(item.name) << "\",\n";
  out << pad << "\"duration_seconds\": " << item.duration_seconds << ",\n";
  out << pad << "\"rms\": " << item.rms_db << ",\n";
  out << pad << "\"peak_db\": " << item.peak_db << ",\n";
  out << pad << "\"loudness\": {\n";
  out << pad << "  \"integrated_lufs\": " << item.loudness.integrated_lufs << ",\n";
  out << pad << "  \"short_term_lufs\": " << item.loudness.short_term_lufs << ",\n";
  out << pad << "  \"true_peak_db\": " << item.loudness.true_peak_dbtp << ",\n";
  out << pad << "  \"rms_db\": " << item.loudness.rms_db << ",\n";
  out << pad << "  \"crest_factor\": " << item.loudness.crest_factor_db << ",\n";
  out << pad << "  \"lra\": " << item.loudness.lra << "\n";
  out << pad << "},\n";
  out << pad << "\"spectral_ratios\": {\n";
  WriteSpectralRatios(out, item.spectral.ratios, indent + 2);
  out << pad << "},\n";
  out << pad << "\"spectral\": {\n";
  out << pad << "  \"centroid_mean_hz\": " << item.spectral.centroid_mean_hz << ",\n";
  out << pad << "  \"centroid_variance\": " << item.spectral.centroid_variance << ",\n";
  out << pad << "  \"rolloff_85_hz\": " << item.spectral.rolloff_85_hz << ",\n";
  out << pad << "  \"flatness\": " << item.spectral.flatness << "\n";
  out << pad << "},\n";
  out << pad << "\"transient\": {\n";
  out << pad << "  \"transients_per_minute\": " << item.transient.transients_per_minute << ",\n";
  out << pad << "  \"average_strength\": " << item.transient.average_strength << ",\n";
  out << pad << "  \"variance\": " << item.transient.variance << ",\n";
  out << pad << "  \"silence_percentage\": " << item.transient.silence_percentage << "\n";
  out << pad << "},\n";
  out << pad << "\"stereo\": {\n";
  out << pad << "  \"available\": " << (item.stereo.available ? "true" : "false") << ",\n";
  out << pad << "  \"mid_energy\": " << item.stereo.mid_energy << ",\n";
  out << pad << "  \"side_energy\": " << item.stereo.side_energy << ",\n";
  out << pad << "  \"mid_side_ratio\": " << item.stereo.mid_side_ratio << ",\n";
  out << pad << "  \"correlation\": " << item.stereo.correlation << ",\n";
  out << pad << "  \"low_frequency_correlation\": " << item.stereo.low_frequency_correlation << ",\n";
  out << pad << "  \"high_band_side_ratio\": " << item.stereo.high_band_side_ratio << "\n";
  out << pad << "},\n";
  out << pad << "\"sub\": {\n";
  out << pad << "  \"sub_rms_db\": " << item.sub.sub_rms_db << ",\n";
  out << pad << "  \"sub_crest_factor\": " << item.sub.sub_crest_factor_db << ",\n";
  out << pad << "  \"sub_to_total_ratio\": " << item.sub.sub_to_total_ratio << ",\n";
  out << pad << "  \"low_to_sub_ratio\": " << item.sub.low_to_sub_ratio << ",\n";
  out << pad << "  \"low_frequency_phase_coherence\": " << item.sub.low_frequency_phase_coherence << "\n";
  out << pad << "},\n";
  out << pad << "\"relative_loudness_lufs\": " << item.relative_loudness_lufs << ",\n";
  out << pad << "\"energy_contribution_ratio\": " << item.energy_contribution_ratio << ",\n";
  out << pad << "\"sub_contribution_ratio\": " << item.sub_contribution_ratio << ",\n";
  out << pad << "\"frequency_dominance_profile\": \"" << EscapeJson(item.frequency_dominance_profile) << "\"";
  if (item.spectrogram.present) {
    out << ",\n";
    out << pad << "\"spectrogram\": {\n";
    WriteSpectrogram(out, item.spectrogram, indent + 2);
    out << pad << "}\n";
  } else {
    out << "\n";
  }
}

void WriteCompositeSpectrogram(std::ofstream& out, const aurora::core::CompositeSpectrogramReport& composite, int indent) {
  const std::string pad(static_cast<size_t>(indent), ' ');
  out << pad << "\"enabled\": " << (composite.enabled ? "true" : "false") << ",\n";
  out << pad << "\"mode\": \"" << EscapeJson(composite.mode) << "\",\n";
  out << pad << "\"profile\": \"" << EscapeJson(composite.profile) << "\",\n";
  if (!composite.path.empty()) {
    out << pad << "\"path\": \"" << EscapeJson(composite.path) << "\",\n";
  }
  out << pad << "\"targets\": [\n";
  for (size_t i = 0; i < composite.targets.size(); ++i) {
    out << pad << "  {\"kind\":\"" << EscapeJson(composite.targets[i].kind) << "\",\"name\":\""
        << EscapeJson(composite.targets[i].name) << "\"}";
    if (i + 1 < composite.targets.size()) {
      out << ",";
    }
    out << "\n";
  }
  out << pad << "],\n";
  out << pad << "\"row_height_px\": " << composite.row_height_px << ",\n";
  out << pad << "\"header_height_px\": " << composite.header_height_px << ",\n";
  out << pad << "\"width_px\": " << composite.width_px << ",\n";
  out << pad << "\"format\": \"" << EscapeJson(composite.format) << "\",\n";
  out << pad << "\"indexed_palette\": " << (composite.indexed_palette ? "true" : "false") << ",\n";
  out << pad << "\"freq_scale\": \"" << EscapeJson(composite.freq_scale) << "\",\n";
  out << pad << "\"colormap\": \"" << EscapeJson(composite.colormap) << "\",\n";
  if (composite.error.empty()) {
    out << pad << "\"error\": null\n";
  } else {
    out << pad << "\"error\": \"" << EscapeJson(composite.error) << "\"\n";
  }
}

}  // namespace

bool WriteAnalysisJson(const std::filesystem::path& path, const aurora::core::AnalysisReport& report, std::string* error) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path);
  if (!out.is_open()) {
    if (error != nullptr) {
      *error = "Failed to open analysis JSON for writing: " + path.string();
    }
    return false;
  }

  out << std::setprecision(9);
  out << "{\n";
  out << "  \"aurora_version\": \"" << EscapeJson(report.aurora_version) << "\",\n";
  out << "  \"analysis_version\": \"" << EscapeJson(report.analysis_version) << "\",\n";
  out << "  \"timestamp\": \"" << EscapeJson(report.timestamp) << "\",\n";
  out << "  \"sample_rate\": " << report.sample_rate << ",\n";
  out << "  \"mode\": \"" << EscapeJson(report.mode) << "\",\n";
  out << "  \"mix\": {\n";
  WriteFileAnalysis(out, report.mix, 4);
  out << "  },\n";
  out << "  \"stems\": [\n";
  for (size_t i = 0; i < report.stems.size(); ++i) {
    out << "    {\n";
    WriteFileAnalysis(out, report.stems[i], 6);
    out << "    }";
    if (i + 1 < report.stems.size()) {
      out << ",";
    }
    out << "\n";
  }
  out << "  ],\n";
  if (report.composite_spectrogram.present) {
    out << "  \"composite_spectrogram\": {\n";
    WriteCompositeSpectrogram(out, report.composite_spectrogram, 4);
    out << "  },\n";
  }
  out << "  \"intent_evaluation\": {\n";
  out << "    \"status\": \"" << EscapeJson(report.intent_evaluation.status) << "\",\n";
  out << "    \"notes\": [\n";
  for (size_t i = 0; i < report.intent_evaluation.notes.size(); ++i) {
    out << "      \"" << EscapeJson(report.intent_evaluation.notes[i]) << "\"";
    if (i + 1 < report.intent_evaluation.notes.size()) {
      out << ",";
    }
    out << "\n";
  }
  out << "    ]\n";
  out << "  }\n";
  out << "}\n";

  if (!out.good()) {
    if (error != nullptr) {
      *error = "Failed while writing analysis JSON: " + path.string();
    }
    return false;
  }
  return true;
}

}  // namespace aurora::io
