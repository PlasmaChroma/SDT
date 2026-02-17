#include "aurora/core/analyzer.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <map>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

namespace aurora::core {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kEpsilon = 1e-12;

struct BasicStats {
  double peak = 0.0;
  double rms = 0.0;
};

struct FftFrameSummary {
  SpectralRatios ratios;
  double centroid_hz = 0.0;
  double rolloff_85_hz = 0.0;
  double flatness = 0.0;
  double total_energy = 0.0;
  double high_side_energy = 0.0;
  double high_total_energy = 0.0;
};

double Clamp(double value, double lo, double hi) { return std::max(lo, std::min(value, hi)); }

double ToDb(double linear) {
  const double safe = std::max(linear, kEpsilon);
  return 20.0 * std::log10(safe);
}

std::string NowIso8601Utc() {
  const std::time_t now = std::time(nullptr);
  std::tm tm_utc{};
#if defined(_WIN32)
  gmtime_s(&tm_utc, &now);
#else
  gmtime_r(&now, &tm_utc);
#endif
  std::ostringstream out;
  out << std::put_time(&tm_utc, "%Y-%m-%dT%H:%M:%SZ");
  return out.str();
}

std::vector<float> MixToMono(const AudioStem& stem) {
  if (stem.channels <= 1) {
    return stem.samples;
  }
  std::vector<float> mono;
  mono.resize(stem.samples.size() / 2U);
  for (size_t i = 0, j = 0; i + 1 < stem.samples.size(); i += 2, ++j) {
    mono[j] = 0.5F * (stem.samples[i] + stem.samples[i + 1]);
  }
  return mono;
}

BasicStats ComputeBasicStats(const std::vector<float>& samples) {
  BasicStats stats;
  if (samples.empty()) {
    return stats;
  }
  double sum_sq = 0.0;
  for (float s : samples) {
    const double v = static_cast<double>(s);
    const double av = std::fabs(v);
    stats.peak = std::max(stats.peak, av);
    sum_sq += v * v;
  }
  stats.rms = std::sqrt(sum_sq / static_cast<double>(samples.size()));
  return stats;
}

void FftInPlace(std::vector<std::complex<double>>* values) {
  auto& a = *values;
  const size_t n = a.size();
  size_t j = 0;
  for (size_t i = 1; i < n; ++i) {
    size_t bit = n >> 1;
    while (j & bit) {
      j ^= bit;
      bit >>= 1;
    }
    j ^= bit;
    if (i < j) {
      std::swap(a[i], a[j]);
    }
  }

  for (size_t len = 2; len <= n; len <<= 1) {
    const double angle = -2.0 * kPi / static_cast<double>(len);
    const std::complex<double> w_len(std::cos(angle), std::sin(angle));
    for (size_t i = 0; i < n; i += len) {
      std::complex<double> w(1.0, 0.0);
      for (size_t j2 = 0; j2 < len / 2; ++j2) {
        const std::complex<double> u = a[i + j2];
        const std::complex<double> v = a[i + j2 + len / 2] * w;
        a[i + j2] = u + v;
        a[i + j2 + len / 2] = u - v;
        w *= w_len;
      }
    }
  }
}

std::vector<double> BuildHann(int size) {
  std::vector<double> w(static_cast<size_t>(size), 0.0);
  if (size <= 1) {
    return w;
  }
  for (int i = 0; i < size; ++i) {
    w[static_cast<size_t>(i)] = 0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) / static_cast<double>(size - 1));
  }
  return w;
}

size_t BandIndex(double hz) {
  if (hz < 60.0) {
    return 0;
  }
  if (hz < 200.0) {
    return 1;
  }
  if (hz < 500.0) {
    return 2;
  }
  if (hz < 2000.0) {
    return 3;
  }
  if (hz < 5000.0) {
    return 4;
  }
  if (hz < 10000.0) {
    return 5;
  }
  if (hz < 16000.0) {
    return 6;
  }
  return 7;
}

void AccumulateBand(SpectralRatios* ratios, size_t band, double value) {
  switch (band) {
    case 0:
      ratios->sub += value;
      break;
    case 1:
      ratios->low += value;
      break;
    case 2:
      ratios->low_mid += value;
      break;
    case 3:
      ratios->mid += value;
      break;
    case 4:
      ratios->presence += value;
      break;
    case 5:
      ratios->high += value;
      break;
    case 6:
      ratios->air += value;
      break;
    case 7:
      ratios->ultra += value;
      break;
    default:
      break;
  }
}

FftFrameSummary AnalyzeFftFrame(const std::vector<float>& mono, const std::vector<float>& side, size_t start, int fft_size,
                                int sample_rate, const std::vector<double>& window, bool have_side) {
  FftFrameSummary out;
  std::vector<std::complex<double>> bins(static_cast<size_t>(fft_size));
  std::vector<std::complex<double>> side_bins(static_cast<size_t>(fft_size));
  for (int i = 0; i < fft_size; ++i) {
    const size_t idx = start + static_cast<size_t>(i);
    const double sample = static_cast<double>(mono[idx]);
    bins[static_cast<size_t>(i)] = std::complex<double>(sample * window[static_cast<size_t>(i)], 0.0);
    if (have_side) {
      side_bins[static_cast<size_t>(i)] = std::complex<double>(static_cast<double>(side[idx]) * window[static_cast<size_t>(i)],
                                                               0.0);
    }
  }
  FftInPlace(&bins);
  if (have_side) {
    FftInPlace(&side_bins);
  }

  const size_t half = static_cast<size_t>(fft_size / 2);
  std::vector<double> cumulative(half, 0.0);
  double total_mag = 0.0;
  double weighted_sum = 0.0;
  double geometric_sum = 0.0;
  for (size_t k = 1; k < half; ++k) {
    const double hz = static_cast<double>(sample_rate) * static_cast<double>(k) / static_cast<double>(fft_size);
    const double mag = std::abs(bins[k]);
    const double energy = mag * mag;
    out.total_energy += energy;
    total_mag += mag;
    weighted_sum += hz * mag;
    geometric_sum += std::log(std::max(mag, kEpsilon));
    cumulative[k] = cumulative[k - 1] + energy;
    const size_t band = BandIndex(hz);
    AccumulateBand(&out.ratios, band, energy);
    if (hz >= 2000.0) {
      out.high_total_energy += energy;
      if (have_side) {
        const double s_mag = std::abs(side_bins[k]);
        out.high_side_energy += s_mag * s_mag;
      }
    }
  }

  if (total_mag > 0.0) {
    out.centroid_hz = weighted_sum / total_mag;
    const double arithmetic_mean = total_mag / static_cast<double>(half - 1);
    const double geometric_mean = std::exp(geometric_sum / static_cast<double>(half - 1));
    out.flatness = geometric_mean / std::max(arithmetic_mean, kEpsilon);
  }

  const double target = out.total_energy * 0.85;
  for (size_t k = 1; k < half; ++k) {
    if (cumulative[k] >= target) {
      out.rolloff_85_hz = static_cast<double>(sample_rate) * static_cast<double>(k) / static_cast<double>(fft_size);
      break;
    }
  }

  return out;
}

std::vector<double> ComputeShortTermLoudness(const std::vector<float>& mono, int sample_rate) {
  std::vector<double> values;
  const size_t win = static_cast<size_t>(std::max(1, sample_rate * 3));
  const size_t hop = static_cast<size_t>(std::max(1, sample_rate));
  if (mono.size() < win) {
    const BasicStats stats = ComputeBasicStats(mono);
    values.push_back(ToDb(stats.rms) - 0.691);
    return values;
  }
  for (size_t start = 0; start + win <= mono.size(); start += hop) {
    double sum_sq = 0.0;
    for (size_t i = 0; i < win; ++i) {
      const double v = static_cast<double>(mono[start + i]);
      sum_sq += v * v;
    }
    const double rms = std::sqrt(sum_sq / static_cast<double>(win));
    values.push_back(ToDb(rms) - 0.691);
  }
  if (values.empty()) {
    values.push_back(-120.0);
  }
  return values;
}

TransientMetrics ComputeTransientMetrics(const std::vector<float>& mono, int sample_rate, double silence_threshold_db) {
  TransientMetrics out;
  if (mono.empty() || sample_rate <= 0) {
    return out;
  }
  const size_t frame = 1024;
  const size_t hop = 512;
  std::vector<double> energies;
  energies.reserve(mono.size() / hop + 1U);
  for (size_t start = 0; start + frame <= mono.size(); start += hop) {
    double e = 0.0;
    for (size_t i = 0; i < frame; ++i) {
      const double v = static_cast<double>(mono[start + i]);
      e += v * v;
    }
    energies.push_back(e / static_cast<double>(frame));
  }
  if (energies.empty()) {
    return out;
  }

  std::vector<double> onset_strength;
  onset_strength.reserve(energies.size());
  onset_strength.push_back(0.0);
  for (size_t i = 1; i < energies.size(); ++i) {
    onset_strength.push_back(std::max(0.0, energies[i] - energies[i - 1]));
  }
  const double mean = std::accumulate(onset_strength.begin(), onset_strength.end(), 0.0) /
                      static_cast<double>(onset_strength.size());
  double variance = 0.0;
  for (double v : onset_strength) {
    const double d = v - mean;
    variance += d * d;
  }
  variance /= static_cast<double>(onset_strength.size());
  const double threshold = mean + std::sqrt(variance);

  std::vector<double> hits;
  hits.reserve(onset_strength.size());
  for (double v : onset_strength) {
    if (v > threshold) {
      hits.push_back(v);
    }
  }
  if (!hits.empty()) {
    out.average_strength = std::accumulate(hits.begin(), hits.end(), 0.0) / static_cast<double>(hits.size());
    double var = 0.0;
    for (double v : hits) {
      const double d = v - out.average_strength;
      var += d * d;
    }
    out.variance = var / static_cast<double>(hits.size());
  }

  const double duration_minutes = static_cast<double>(mono.size()) / static_cast<double>(sample_rate) / 60.0;
  if (duration_minutes > 0.0) {
    out.transients_per_minute = static_cast<double>(hits.size()) / duration_minutes;
  }

  const double silence_threshold = std::pow(10.0, silence_threshold_db / 20.0);
  size_t silent = 0;
  for (float s : mono) {
    if (std::fabs(static_cast<double>(s)) < silence_threshold) {
      ++silent;
    }
  }
  out.silence_percentage = 100.0 * static_cast<double>(silent) / static_cast<double>(mono.size());

  return out;
}

std::vector<float> LowPass(const std::vector<float>& in, int sample_rate, double cutoff_hz) {
  std::vector<float> out(in.size(), 0.0F);
  if (in.empty() || sample_rate <= 0 || cutoff_hz <= 0.0) {
    return out;
  }
  const double alpha = std::exp(-2.0 * kPi * cutoff_hz / static_cast<double>(sample_rate));
  double y = 0.0;
  for (size_t i = 0; i < in.size(); ++i) {
    y = (1.0 - alpha) * static_cast<double>(in[i]) + alpha * y;
    out[i] = static_cast<float>(y);
  }
  return out;
}

double Correlation(const std::vector<float>& a, const std::vector<float>& b) {
  if (a.size() != b.size() || a.empty()) {
    return 0.0;
  }
  double sum_a = 0.0;
  double sum_b = 0.0;
  for (size_t i = 0; i < a.size(); ++i) {
    sum_a += static_cast<double>(a[i]);
    sum_b += static_cast<double>(b[i]);
  }
  const double mean_a = sum_a / static_cast<double>(a.size());
  const double mean_b = sum_b / static_cast<double>(b.size());
  double num = 0.0;
  double da = 0.0;
  double db = 0.0;
  for (size_t i = 0; i < a.size(); ++i) {
    const double va = static_cast<double>(a[i]) - mean_a;
    const double vb = static_cast<double>(b[i]) - mean_b;
    num += va * vb;
    da += va * va;
    db += vb * vb;
  }
  const double den = std::sqrt(std::max(da * db, kEpsilon));
  return Clamp(num / den, -1.0, 1.0);
}

std::string DominanceProfile(const SpectralRatios& ratios) {
  std::vector<std::pair<std::string, double>> bands = {
      {"sub", ratios.sub},       {"low", ratios.low},       {"low_mid", ratios.low_mid}, {"mid", ratios.mid},
      {"presence", ratios.presence}, {"high", ratios.high}, {"air", ratios.air},         {"ultra", ratios.ultra}};
  auto it = std::max_element(bands.begin(), bands.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
  if (it == bands.end()) {
    return "balanced";
  }
  return it->first + "_dominant";
}

IntentEvaluation EvaluateIntent(const FileAnalysis& mix, const std::string& intent) {
  IntentEvaluation out;
  if (intent.empty()) {
    return out;
  }
  out.status = "in_range";

  if (intent == "sleep") {
    if (mix.transient.transients_per_minute > 30.0) {
      out.notes.push_back("Transient density high for sleep");
    }
    if (mix.spectral.ratios.presence > 0.18) {
      out.notes.push_back("Presence band elevated");
    }
    if (mix.loudness.integrated_lufs > -14.0) {
      out.notes.push_back("Overall loudness high for sleep");
    }
  } else if (intent == "ritual") {
    if (mix.spectral.ratios.sub < 0.08) {
      out.notes.push_back("Sub band lower than ritual target");
    }
    if (mix.loudness.lra < 3.0) {
      out.notes.push_back("Dynamic range narrower than ritual target");
    }
  } else if (intent == "dub") {
    if (mix.spectral.ratios.sub < 0.12) {
      out.notes.push_back("Sub band low for dub");
    }
    if (mix.stereo.available && mix.stereo.side_energy < mix.stereo.mid_energy * 0.2) {
      out.notes.push_back("Stereo side energy low for dub");
    }
  } else {
    out.status = "unsupported_intent";
    out.notes.push_back("Unsupported intent preset: " + intent);
    return out;
  }

  if (!out.notes.empty()) {
    out.status = "out_of_range";
  }
  return out;
}

}  // namespace

FileAnalysis AnalyzeStem(const AudioStem& stem, int sample_rate, const AnalysisOptions& options) {
  FileAnalysis out;
  out.name = stem.name;

  if (sample_rate <= 0 || stem.channels <= 0 || stem.samples.empty()) {
    return out;
  }

  const size_t frame_count = stem.samples.size() / static_cast<size_t>(stem.channels);
  out.duration_seconds = static_cast<double>(frame_count) / static_cast<double>(sample_rate);

  std::vector<float> mono = MixToMono(stem);
  std::vector<float> left;
  std::vector<float> right;
  std::vector<float> side;
  if (stem.channels == 2) {
    left.resize(frame_count);
    right.resize(frame_count);
    side.resize(frame_count);
    for (size_t i = 0; i < frame_count; ++i) {
      const float l = stem.samples[i * 2U];
      const float r = stem.samples[i * 2U + 1U];
      left[i] = l;
      right[i] = r;
      side[i] = 0.5F * (l - r);
    }
  }

  const BasicStats mono_stats = ComputeBasicStats(mono);
  out.peak_db = ToDb(mono_stats.peak);
  out.rms_db = ToDb(mono_stats.rms);

  out.loudness.rms_db = out.rms_db;
  out.loudness.true_peak_dbtp = out.peak_db;
  out.loudness.integrated_lufs = out.rms_db - 0.691;
  const std::vector<double> short_term = ComputeShortTermLoudness(mono, sample_rate);
  out.loudness.short_term_lufs =
      std::accumulate(short_term.begin(), short_term.end(), 0.0) / static_cast<double>(short_term.size());
  std::vector<double> sorted_st = short_term;
  std::sort(sorted_st.begin(), sorted_st.end());
  const size_t p10 = static_cast<size_t>(0.1 * static_cast<double>(sorted_st.size() - 1));
  const size_t p95 = static_cast<size_t>(0.95 * static_cast<double>(sorted_st.size() - 1));
  out.loudness.lra = sorted_st[p95] - sorted_st[p10];
  out.loudness.crest_factor_db = out.peak_db - out.rms_db;

  out.transient = ComputeTransientMetrics(mono, sample_rate, options.silence_threshold_db);

  const int fft_size = std::max(256, options.fft_size);
  const int hop = std::max(64, options.fft_hop);
  const std::vector<double> window = BuildHann(fft_size);
  std::vector<double> centroids;
  double rolloff_sum = 0.0;
  double flatness_sum = 0.0;
  size_t frames = 0;
  SpectralRatios energy_sum;
  double total_spectral_energy = 0.0;
  double high_side_energy = 0.0;
  double high_total_energy = 0.0;

  if (mono.size() >= static_cast<size_t>(fft_size)) {
    for (size_t start = 0; start + static_cast<size_t>(fft_size) <= mono.size(); start += static_cast<size_t>(hop)) {
      const FftFrameSummary frame = AnalyzeFftFrame(mono, side, start, fft_size, sample_rate, window, stem.channels == 2);
      AccumulateBand(&energy_sum, 0, frame.ratios.sub);
      AccumulateBand(&energy_sum, 1, frame.ratios.low);
      AccumulateBand(&energy_sum, 2, frame.ratios.low_mid);
      AccumulateBand(&energy_sum, 3, frame.ratios.mid);
      AccumulateBand(&energy_sum, 4, frame.ratios.presence);
      AccumulateBand(&energy_sum, 5, frame.ratios.high);
      AccumulateBand(&energy_sum, 6, frame.ratios.air);
      AccumulateBand(&energy_sum, 7, frame.ratios.ultra);
      total_spectral_energy += frame.total_energy;
      centroids.push_back(frame.centroid_hz);
      rolloff_sum += frame.rolloff_85_hz;
      flatness_sum += frame.flatness;
      high_side_energy += frame.high_side_energy;
      high_total_energy += frame.high_total_energy;
      ++frames;
    }
  }

  if (total_spectral_energy > 0.0) {
    out.spectral.ratios.sub = energy_sum.sub / total_spectral_energy;
    out.spectral.ratios.low = energy_sum.low / total_spectral_energy;
    out.spectral.ratios.low_mid = energy_sum.low_mid / total_spectral_energy;
    out.spectral.ratios.mid = energy_sum.mid / total_spectral_energy;
    out.spectral.ratios.presence = energy_sum.presence / total_spectral_energy;
    out.spectral.ratios.high = energy_sum.high / total_spectral_energy;
    out.spectral.ratios.air = energy_sum.air / total_spectral_energy;
    out.spectral.ratios.ultra = energy_sum.ultra / total_spectral_energy;
  }

  if (!centroids.empty()) {
    const double centroid_mean = std::accumulate(centroids.begin(), centroids.end(), 0.0) / static_cast<double>(centroids.size());
    double centroid_var = 0.0;
    for (double c : centroids) {
      const double d = c - centroid_mean;
      centroid_var += d * d;
    }
    centroid_var /= static_cast<double>(centroids.size());
    out.spectral.centroid_mean_hz = centroid_mean;
    out.spectral.centroid_variance = centroid_var;
  }
  if (frames > 0) {
    out.spectral.rolloff_85_hz = rolloff_sum / static_cast<double>(frames);
    out.spectral.flatness = flatness_sum / static_cast<double>(frames);
  }

  const std::vector<float> lp60 = LowPass(mono, sample_rate, 60.0);
  const std::vector<float> lp200 = LowPass(mono, sample_rate, 200.0);
  std::vector<float> band60_200(mono.size(), 0.0F);
  for (size_t i = 0; i < mono.size(); ++i) {
    band60_200[i] = lp200[i] - lp60[i];
  }
  const BasicStats sub_stats = ComputeBasicStats(lp60);
  const BasicStats low_stats = ComputeBasicStats(band60_200);
  out.sub.sub_rms_db = ToDb(sub_stats.rms);
  out.sub.sub_crest_factor_db = ToDb(sub_stats.peak) - ToDb(sub_stats.rms);
  out.sub.sub_to_total_ratio = Clamp(out.spectral.ratios.sub, 0.0, 1.0);
  out.sub.low_to_sub_ratio = low_stats.rms / std::max(sub_stats.rms, kEpsilon);

  if (stem.channels == 2) {
    out.stereo.available = true;
    std::vector<float> mid(frame_count, 0.0F);
    for (size_t i = 0; i < frame_count; ++i) {
      mid[i] = 0.5F * (left[i] + right[i]);
    }
    const BasicStats mid_stats = ComputeBasicStats(mid);
    const BasicStats side_stats = ComputeBasicStats(side);
    out.stereo.mid_energy = mid_stats.rms * mid_stats.rms;
    out.stereo.side_energy = side_stats.rms * side_stats.rms;
    out.stereo.mid_side_ratio = out.stereo.mid_energy / std::max(out.stereo.side_energy, kEpsilon);
    out.stereo.correlation = Correlation(left, right);
    const std::vector<float> l_low = LowPass(left, sample_rate, 200.0);
    const std::vector<float> r_low = LowPass(right, sample_rate, 200.0);
    out.stereo.low_frequency_correlation = Correlation(l_low, r_low);
    out.stereo.high_band_side_ratio = high_side_energy / std::max(high_total_energy, kEpsilon);
    out.sub.low_frequency_phase_coherence = std::fabs(out.stereo.low_frequency_correlation);
  }

  out.frequency_dominance_profile = DominanceProfile(out.spectral.ratios);
  return out;
}

AnalysisReport AnalyzeFiles(const std::vector<AudioStem>& stems, const AudioStem& mix, int sample_rate,
                            const std::string& mode, const AnalysisOptions& options) {
  AnalysisReport report;
  report.timestamp = NowIso8601Utc();
  report.sample_rate = sample_rate;
  report.mode = mode;
  report.mix = AnalyzeStem(mix, sample_rate, options);

  double mix_rms_linear = std::pow(10.0, report.mix.rms_db / 20.0);
  const double mix_sub_ratio = report.mix.sub.sub_to_total_ratio;
  report.stems.reserve(stems.size());
  for (const auto& stem : stems) {
    FileAnalysis analyzed = AnalyzeStem(stem, sample_rate, options);
    const double stem_rms_linear = std::pow(10.0, analyzed.rms_db / 20.0);
    analyzed.relative_loudness_lufs = analyzed.loudness.integrated_lufs - report.mix.loudness.integrated_lufs;
    analyzed.energy_contribution_ratio = stem_rms_linear / std::max(mix_rms_linear, kEpsilon);
    analyzed.sub_contribution_ratio = analyzed.sub.sub_to_total_ratio / std::max(mix_sub_ratio, kEpsilon);
    report.stems.push_back(std::move(analyzed));
  }

  report.intent_evaluation = EvaluateIntent(report.mix, options.intent);
  return report;
}

AnalysisReport AnalyzeRender(const RenderResult& render, const AnalysisOptions& options) {
  std::vector<AudioStem> stems;
  stems.reserve(render.patch_stems.size() + render.bus_stems.size());
  stems.insert(stems.end(), render.patch_stems.begin(), render.patch_stems.end());
  stems.insert(stems.end(), render.bus_stems.begin(), render.bus_stems.end());
  return AnalyzeFiles(stems, render.master, render.metadata.sample_rate, "render_analysis", options);
}

}  // namespace aurora::core
