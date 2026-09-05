#pragma once

#include <algorithm>
#include <cmath>

#include "aca/audio_buffer.h"

namespace aca {

// speexdsp's AGC target (SPEEX_PREPROCESS_SET_AGC_LEVEL) is a LINEAR amplitude
// relative to full scale, not a dB value. Passing dB directly is a silent
// misconfiguration, so all AGC setup goes through this conversion.
inline float dbfs_to_agc_level(float dbfs) {
  const float linear = 32768.0f * std::pow(10.0f, dbfs / 20.0f);
  return std::clamp(linear, 1000.0f, 32000.0f);
}

// speexdsp processes whole frames only, and AEC/ANS/AGC here are mono.
// Downmix and zero-pad up to a frame boundary.
inline AudioBuffer to_padded_mono(const AudioBuffer& in, size_t frame_size) {
  AudioBuffer b = in;
  b.to_mono();
  if (frame_size > 0) {
    const size_t padded =
        ((b.samples.size() + frame_size - 1) / frame_size) * frame_size;
    b.samples.resize(padded, 0);
  }
  return b;
}

}  // namespace aca
