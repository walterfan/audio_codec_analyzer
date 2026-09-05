#pragma once

#include <cstddef>
#include <vector>

#include "aca/audio_buffer.h"

namespace aca {

// dBFS of the RMS level. Returns -inf-ish (-120.0) for pure silence.
double rms_dbfs(const int16_t* data, size_t count);
double rms_dbfs(const Pcm16& v);

double peak_dbfs(const int16_t* data, size_t count);
double peak_dbfs(const Pcm16& v);

// Signal-to-noise style comparison of a processed signal against a reference.
// Both must be the same length; the shorter length is used if they differ.
double snr_db(const Pcm16& reference, const Pcm16& processed);

// Echo Return Loss Enhancement: how much the echo path was attenuated.
// Positive = the canceller removed energy relative to `before`.
double erle_db(const Pcm16& before, const Pcm16& after);

struct LevelReport {
  double rms_dbfs = 0.0;
  double peak_dbfs = 0.0;
  double duration_seconds = 0.0;
  size_t clipped_samples = 0;
};

LevelReport analyze_levels(const AudioBuffer& buf);

}  // namespace aca
