#pragma once

#include <string>
#include <vector>

#include "aurora/core/renderer.hpp"

namespace aurora::core {

struct SpectralRatios {
  double sub = 0.0;
  double low = 0.0;
  double low_mid = 0.0;
  double mid = 0.0;
  double presence = 0.0;
  double high = 0.0;
  double air = 0.0;
  double ultra = 0.0;
};

struct LoudnessMetrics {
  double integrated_lufs = 0.0;
  double short_term_lufs = 0.0;
  double true_peak_dbtp = 0.0;
  double rms_db = 0.0;
  double crest_factor_db = 0.0;
  double lra = 0.0;
};

struct SpectralMetrics {
  SpectralRatios ratios;
  double centroid_mean_hz = 0.0;
  double centroid_variance = 0.0;
  double rolloff_85_hz = 0.0;
  double flatness = 0.0;
};

struct TransientMetrics {
  double transients_per_minute = 0.0;
  double average_strength = 0.0;
  double variance = 0.0;
  double silence_percentage = 0.0;
};

struct StereoMetrics {
  bool available = false;
  double mid_energy = 0.0;
  double side_energy = 0.0;
  double mid_side_ratio = 0.0;
  double correlation = 0.0;
  double low_frequency_correlation = 0.0;
  double high_band_side_ratio = 0.0;
};

struct SubMetrics {
  double sub_rms_db = 0.0;
  double sub_crest_factor_db = 0.0;
  double sub_to_total_ratio = 0.0;
  double low_to_sub_ratio = 0.0;
  double low_frequency_phase_coherence = 0.0;
};

struct IntentEvaluation {
  std::string status = "not_evaluated";
  std::vector<std::string> notes;
};

struct FileAnalysis {
  std::string name;
  double duration_seconds = 0.0;
  double peak_db = 0.0;
  double rms_db = 0.0;
  LoudnessMetrics loudness;
  SpectralMetrics spectral;
  TransientMetrics transient;
  StereoMetrics stereo;
  SubMetrics sub;
  double relative_loudness_lufs = 0.0;
  double energy_contribution_ratio = 0.0;
  double sub_contribution_ratio = 0.0;
  std::string frequency_dominance_profile;
};

struct AnalysisReport {
  std::string aurora_version = "1.0.0";
  std::string analysis_version = "1.0";
  std::string timestamp;
  int sample_rate = 0;
  std::string mode;
  FileAnalysis mix;
  std::vector<FileAnalysis> stems;
  IntentEvaluation intent_evaluation;
};

struct AnalysisOptions {
  int fft_size = 2048;
  int fft_hop = 1024;
  double silence_threshold_db = -50.0;
  int max_parallel_jobs = 0;
  std::string intent;
};

FileAnalysis AnalyzeStem(const AudioStem& stem, int sample_rate, const AnalysisOptions& options);
AnalysisReport AnalyzeRender(const RenderResult& render, const AnalysisOptions& options);
AnalysisReport AnalyzeFiles(const std::vector<AudioStem>& stems, const AudioStem& mix, int sample_rate,
                            const std::string& mode, const AnalysisOptions& options);

}  // namespace aurora::core
