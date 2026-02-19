#include "aurora/core/spectrogram.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace aurora::core {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kEps = 1e-12;

struct Rgb {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
};

double Clamp(double value, double lo, double hi) { return std::max(lo, std::min(value, hi)); }

bool IsPowerOfTwo(int value) { return value > 0 && (value & (value - 1)) == 0; }

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

Rgb LerpRgb(const Rgb& a, const Rgb& b, double t) {
  const double tt = Clamp(t, 0.0, 1.0);
  const int r = static_cast<int>(std::lround((1.0 - tt) * static_cast<double>(a.r) + tt * static_cast<double>(b.r)));
  const int g = static_cast<int>(std::lround((1.0 - tt) * static_cast<double>(a.g) + tt * static_cast<double>(b.g)));
  const int b2 = static_cast<int>(std::lround((1.0 - tt) * static_cast<double>(a.b) + tt * static_cast<double>(b.b)));
  Rgb out;
  out.r = static_cast<uint8_t>(std::clamp(r, 0, 255));
  out.g = static_cast<uint8_t>(std::clamp(g, 0, 255));
  out.b = static_cast<uint8_t>(std::clamp(b2, 0, 255));
  return out;
}

struct ColorStop {
  double t;
  Rgb c;
};

std::vector<Rgb> BuildLutFromStops(const std::vector<ColorStop>& stops) {
  std::vector<Rgb> lut(256);
  for (int i = 0; i < 256; ++i) {
    const double t = static_cast<double>(i) / 255.0;
    size_t hi = 1;
    while (hi < stops.size() && t > stops[hi].t) {
      ++hi;
    }
    if (hi >= stops.size()) {
      lut[static_cast<size_t>(i)] = stops.back().c;
      continue;
    }
    if (hi == 0) {
      lut[static_cast<size_t>(i)] = stops.front().c;
      continue;
    }
    const size_t lo = hi - 1;
    const double denom = std::max(stops[hi].t - stops[lo].t, 1e-9);
    const double local_t = (t - stops[lo].t) / denom;
    lut[static_cast<size_t>(i)] = LerpRgb(stops[lo].c, stops[hi].c, local_t);
  }
  return lut;
}

const std::vector<Rgb>& GetColorLut(const std::string& name) {
  static const std::vector<Rgb> magma = BuildLutFromStops({
      {0.0, {0, 0, 4}},   {0.13, {28, 16, 68}}, {0.25, {79, 18, 123}}, {0.38, {129, 37, 129}},
      {0.5, {181, 54, 122}}, {0.63, {229, 80, 100}}, {0.78, {251, 135, 97}}, {0.9, {254, 194, 135}}, {1.0, {252, 253, 191}},
  });
  static const std::vector<Rgb> inferno = BuildLutFromStops({
      {0.0, {0, 0, 4}},   {0.13, {31, 12, 72}}, {0.25, {85, 15, 109}}, {0.38, {136, 34, 106}},
      {0.5, {186, 54, 85}}, {0.63, {227, 89, 51}}, {0.78, {249, 140, 10}}, {0.9, {249, 201, 50}}, {1.0, {252, 255, 164}},
  });
  static const std::vector<Rgb> viridis = BuildLutFromStops({
      {0.0, {68, 1, 84}},   {0.13, {72, 35, 116}}, {0.25, {64, 67, 135}}, {0.38, {52, 94, 141}},
      {0.5, {41, 120, 142}}, {0.63, {32, 146, 140}}, {0.78, {53, 183, 121}}, {0.9, {144, 214, 67}}, {1.0, {253, 231, 37}},
  });
  static const std::vector<Rgb> plasma = BuildLutFromStops({
      {0.0, {13, 8, 135}},   {0.13, {75, 3, 161}}, {0.25, {125, 3, 168}}, {0.38, {168, 34, 150}},
      {0.5, {203, 70, 121}}, {0.63, {229, 108, 93}}, {0.78, {248, 148, 65}}, {0.9, {253, 195, 40}}, {1.0, {240, 249, 33}},
  });
  if (name == "inferno") {
    return inferno;
  }
  if (name == "viridis") {
    return viridis;
  }
  if (name == "plasma") {
    return plasma;
  }
  return magma;
}

float SampleFrameMag(const std::vector<float>& mags, int bins, int frame, double kf) {
  const double bounded = Clamp(kf, 0.0, static_cast<double>(bins - 1));
  const int k0 = static_cast<int>(std::floor(bounded));
  const int k1 = std::min(k0 + 1, bins - 1);
  const double frac = bounded - static_cast<double>(k0);
  const float a = mags[static_cast<size_t>(frame * bins + k0)];
  const float b = mags[static_cast<size_t>(frame * bins + k1)];
  return static_cast<float>((1.0 - frac) * static_cast<double>(a) + frac * static_cast<double>(b));
}

}  // namespace

bool RenderSpectrogramRgb(const std::vector<float>& mono, int sample_rate, const SpectrogramConfig& config,
                          std::vector<uint8_t>* rgb, std::string* error) {
  if (rgb == nullptr) {
    if (error != nullptr) {
      *error = "Internal error: null spectrogram output buffer.";
    }
    return false;
  }
  if (sample_rate <= 0) {
    if (error != nullptr) {
      *error = "Invalid sample rate for spectrogram.";
    }
    return false;
  }
  if (config.window < 2 || config.hop < 1 || config.nfft < config.window || !IsPowerOfTwo(config.nfft) ||
      config.width_px < 2 || config.height_px < 2 || config.gamma <= 0.0 || config.max_hz <= config.min_hz) {
    if (error != nullptr) {
      *error = "Invalid spectrogram configuration.";
    }
    return false;
  }

  const int width = config.width_px;
  const int height = config.height_px;
  const int fft_size = config.nfft;
  const int bins = fft_size / 2 + 1;
  const size_t sample_count = mono.size();
  const size_t window = static_cast<size_t>(config.window);
  const size_t hop = static_cast<size_t>(config.hop);
  const size_t num_frames = sample_count >= window ? (1U + (sample_count - window) / hop) : 1U;

  std::vector<float> mags(num_frames * static_cast<size_t>(bins), 0.0F);
  const std::vector<double> hann = BuildHann(config.window);
  std::vector<std::complex<double>> frame(static_cast<size_t>(fft_size), std::complex<double>(0.0, 0.0));

  for (size_t t = 0; t < num_frames; ++t) {
    std::fill(frame.begin(), frame.end(), std::complex<double>(0.0, 0.0));
    const size_t start = t * hop;
    for (int i = 0; i < config.window; ++i) {
      const size_t idx = start + static_cast<size_t>(i);
      double sample = 0.0;
      if (idx < sample_count) {
        sample = static_cast<double>(mono[idx]);
        if (!std::isfinite(sample)) {
          sample = 0.0;
        }
      }
      frame[static_cast<size_t>(i)] = std::complex<double>(sample * hann[static_cast<size_t>(i)], 0.0);
    }
    FftInPlace(&frame);
    for (int k = 0; k < bins; ++k) {
      mags[t * static_cast<size_t>(bins) + static_cast<size_t>(k)] = static_cast<float>(std::abs(frame[static_cast<size_t>(k)]));
    }
  }

  std::vector<double> freq_bins(static_cast<size_t>(height), 0.0);
  for (int y = 0; y < height; ++y) {
    const double alpha = static_cast<double>(y) / static_cast<double>(height - 1);
    if (config.freq_scale == "linear") {
      freq_bins[static_cast<size_t>(y)] = alpha * static_cast<double>(bins - 1);
    } else {
      const double ratio = config.max_hz / config.min_hz;
      const double fy = config.min_hz * std::pow(ratio, alpha);
      freq_bins[static_cast<size_t>(y)] = fy * static_cast<double>(fft_size) / static_cast<double>(sample_rate);
    }
  }

  std::vector<double> u(static_cast<size_t>(width * height), 0.0);
  for (int x = 0; x < width; ++x) {
    const double tf = static_cast<double>(x) * static_cast<double>(num_frames - 1) / static_cast<double>(width - 1);
    const int t0 = static_cast<int>(std::floor(tf));
    const int t1 = std::min(t0 + 1, static_cast<int>(num_frames - 1));
    const double time_frac = tf - static_cast<double>(t0);
    for (int y = 0; y < height; ++y) {
      const float m0 = SampleFrameMag(mags, bins, t0, freq_bins[static_cast<size_t>(y)]);
      const float m1 = SampleFrameMag(mags, bins, t1, freq_bins[static_cast<size_t>(y)]);
      const double mag = (1.0 - time_frac) * static_cast<double>(m0) + time_frac * static_cast<double>(m1);
      double db = 20.0 * std::log10(mag + kEps);
      db = Clamp(db, config.db_min, config.db_max);
      double norm = (db - config.db_min) / std::max(config.db_max - config.db_min, 1e-9);
      norm = Clamp(norm, 0.0, 1.0);
      if (config.gamma != 1.0) {
        norm = std::pow(norm, 1.0 / config.gamma);
      }
      const int yy = (height - 1) - y;
      u[static_cast<size_t>(yy * width + x)] = norm;
    }
  }

  if (config.smoothing_bins > 0) {
    const int radius = config.smoothing_bins;
    std::vector<double> smoothed = u;
    for (int y = 0; y < height; ++y) {
      const int y0 = std::max(0, y - radius);
      const int y1 = std::min(height - 1, y + radius);
      for (int x = 0; x < width; ++x) {
        double sum = 0.0;
        int count = 0;
        for (int yy = y0; yy <= y1; ++yy) {
          sum += u[static_cast<size_t>(yy * width + x)];
          ++count;
        }
        smoothed[static_cast<size_t>(y * width + x)] = sum / static_cast<double>(std::max(count, 1));
      }
    }
    u = std::move(smoothed);
  }

  const std::vector<Rgb>& lut = GetColorLut(config.colormap);
  rgb->assign(static_cast<size_t>(width * height * 3), 0U);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const double norm = Clamp(u[static_cast<size_t>(y * width + x)], 0.0, 1.0);
      const int lut_idx = std::clamp(static_cast<int>(std::lround(norm * 255.0)), 0, 255);
      const Rgb c = lut[static_cast<size_t>(lut_idx)];
      const size_t out = static_cast<size_t>((y * width + x) * 3);
      (*rgb)[out] = c.r;
      (*rgb)[out + 1] = c.g;
      (*rgb)[out + 2] = c.b;
    }
  }

  return true;
}

}  // namespace aurora::core
