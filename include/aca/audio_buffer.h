#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace aca {

// All DSP in this project runs on interleaved 16-bit PCM, because both the
// speexdsp AEC/preprocess API and the Opus fixed-point path are int16-native.
// Converting to float and back would only add rounding noise here.
using Pcm16 = std::vector<int16_t>;

struct AudioBuffer {
  Pcm16 samples;          // interleaved
  int sample_rate = 0;    // Hz
  int channels = 0;

  size_t frames() const {
    return channels > 0 ? samples.size() / static_cast<size_t>(channels) : 0;
  }
  bool empty() const { return samples.empty(); }
  double duration_seconds() const {
    return sample_rate > 0 ? static_cast<double>(frames()) / sample_rate : 0.0;
  }

  // Downmix to mono in place. AEC/ANS/AGC are mono-only in this project.
  void to_mono();
};

// Number of samples per channel in a `ms` slice at `sample_rate`.
inline size_t frames_for_ms(int sample_rate, double ms) {
  return static_cast<size_t>(sample_rate * ms / 1000.0);
}

}  // namespace aca
