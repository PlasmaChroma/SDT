#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace aurora::core {

struct SpectrogramConfig {
  int window = 2048;
  int hop = 512;
  int nfft = 2048;
  std::string mode = "mixdown";
  std::string freq_scale = "log";
  double min_hz = 20.0;
  double max_hz = 20000.0;
  double db_min = -90.0;
  double db_max = 0.0;
  std::string colormap = "magma";
  int width_px = 1600;
  int height_px = 512;
  double gamma = 1.0;
  int smoothing_bins = 0;
};

bool RenderSpectrogramRgb(const std::vector<float>& mono, int sample_rate, const SpectrogramConfig& config,
                          std::vector<uint8_t>* rgb, std::string* error);
bool BuildColormapLutRgb(const std::string& name, std::vector<uint8_t>* palette_rgb, std::string* error);

}  // namespace aurora::core
