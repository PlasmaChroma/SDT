#pragma once

#include <cstdint>
#include <string_view>

namespace aurora::core {

inline uint64_t Hash64(std::string_view text, uint64_t seed = 1469598103934665603ULL) {
  uint64_t hash = seed;
  constexpr uint64_t kPrime = 1099511628211ULL;
  for (const char c : text) {
    hash ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
    hash *= kPrime;
  }
  return hash;
}

inline uint64_t Hash64Combine(uint64_t a, uint64_t b) {
  uint64_t z = a + 0x9e3779b97f4a7c15ULL + (b << 6U) + (b >> 2U);
  z ^= z >> 30U;
  z *= 0xbf58476d1ce4e5b9ULL;
  z ^= z >> 27U;
  z *= 0x94d049bb133111ebULL;
  z ^= z >> 31U;
  return z;
}

inline uint64_t Hash64FromParts(uint64_t seed, std::string_view a, std::string_view b = {},
                                std::string_view c = {}, std::string_view d = {}) {
  uint64_t h = Hash64Combine(seed, Hash64(a));
  if (!b.empty()) {
    h = Hash64Combine(h, Hash64(b));
  }
  if (!c.empty()) {
    h = Hash64Combine(h, Hash64(c));
  }
  if (!d.empty()) {
    h = Hash64Combine(h, Hash64(d));
  }
  return h;
}

class PCG32 {
 public:
  explicit PCG32(uint64_t seed = 0u, uint64_t sequence = 0x853c49e6748fea9bULL) { Seed(seed, sequence); }

  void Seed(uint64_t seed, uint64_t sequence = 0x853c49e6748fea9bULL) {
    state_ = 0U;
    increment_ = (sequence << 1U) | 1U;
    NextUInt();
    state_ += seed;
    NextUInt();
  }

  uint32_t NextUInt() {
    const uint64_t old_state = state_;
    state_ = old_state * 6364136223846793005ULL + increment_;
    const uint32_t xorshifted = static_cast<uint32_t>(((old_state >> 18U) ^ old_state) >> 27U);
    const uint32_t rot = static_cast<uint32_t>(old_state >> 59U);
    return (xorshifted >> rot) | (xorshifted << ((-rot) & 31U));
  }

  double NextUnit() { return static_cast<double>(NextUInt()) / static_cast<double>(UINT32_MAX); }

  double Uniform(double min_value, double max_value) {
    return min_value + (max_value - min_value) * NextUnit();
  }

 private:
  uint64_t state_ = 0U;
  uint64_t increment_ = 0U;
};

}  // namespace aurora::core

