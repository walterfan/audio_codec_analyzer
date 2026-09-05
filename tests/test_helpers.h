#pragma once

#include <cmath>
#include <random>
#include <vector>

#include "aca/audio_buffer.h"

namespace aca::test {

// A tone at `freq` Hz, amplitude in [0,1], no file I/O required.
inline AudioBuffer make_tone(int sample_rate, double seconds, double freq,
                             double amp = 0.5, int channels = 1) {
  AudioBuffer b;
  b.sample_rate = sample_rate;
  b.channels = channels;
  const size_t frames = static_cast<size_t>(sample_rate * seconds);
  b.samples.resize(frames * static_cast<size_t>(channels));
  for (size_t i = 0; i < frames; ++i) {
    const double t = static_cast<double>(i) / sample_rate;
    const double v = amp * std::sin(2.0 * M_PI * freq * t);
    for (int c = 0; c < channels; ++c) {
      b.samples[i * channels + c] = static_cast<int16_t>(v * 32767.0);
    }
  }
  return b;
}

// IMPORTANT: speexdsp's ANS is a stationary-noise estimator, so a constant
// sine wave gets classified as noise and suppressed by ~40 dB. Any test that
// exercises ANS or AGC must use a NON-STATIONARY, speech-like signal or the
// results are meaningless. This mirrors tools/gen_fixtures.cpp: a 140 Hz pitch
// with harmonics, shaped by a 4 Hz syllable envelope with short gaps.
inline AudioBuffer make_speech_like(int sample_rate, double seconds,
                                    double amp = 0.5) {
  AudioBuffer b;
  b.sample_rate = sample_rate;
  b.channels = 1;
  const size_t frames = static_cast<size_t>(sample_rate * seconds);
  b.samples.resize(frames);
  for (size_t i = 0; i < frames; ++i) {
    const double t = static_cast<double>(i) / sample_rate;
    double s = 0.60 * std::sin(2 * M_PI * 140.0 * t) +
               0.25 * std::sin(2 * M_PI * 280.0 * t) +
               0.15 * std::sin(2 * M_PI * 700.0 * t) +
               0.08 * std::sin(2 * M_PI * 1220.0 * t);
    const double syllable = 0.5 * (1.0 - std::cos(2 * M_PI * 4.0 * t));
    const double gate = (std::fmod(t, 1.0) < 0.75) ? 1.0 : 0.05;
    double v = s * syllable * gate * amp;
    v = std::max(-1.0, std::min(1.0, v));
    b.samples[i] = static_cast<int16_t>(v * 32767.0);
  }
  return b;
}

inline AudioBuffer make_noise(int sample_rate, double seconds, double amp,
                              unsigned seed = 42) {
  AudioBuffer b;
  b.sample_rate = sample_rate;
  b.channels = 1;
  const size_t frames = static_cast<size_t>(sample_rate * seconds);
  b.samples.resize(frames);
  std::mt19937 rng(seed);
  std::normal_distribution<double> g(0.0, amp);
  for (size_t i = 0; i < frames; ++i) {
    double v = g(rng);
    v = v > 1.0 ? 1.0 : (v < -1.0 ? -1.0 : v);
    b.samples[i] = static_cast<int16_t>(v * 32767.0);
  }
  return b;
}

// Adds `b` into `a` sample-wise, with saturation.
inline AudioBuffer mix(const AudioBuffer& a, const AudioBuffer& b,
                       double b_gain = 1.0) {
  AudioBuffer out = a;
  const size_t n = std::min(a.samples.size(), b.samples.size());
  for (size_t i = 0; i < n; ++i) {
    int32_t v = static_cast<int32_t>(a.samples[i]) +
                static_cast<int32_t>(b.samples[i] * b_gain);
    v = v > 32767 ? 32767 : (v < -32768 ? -32768 : v);
    out.samples[i] = static_cast<int16_t>(v);
  }
  return out;
}

}  // namespace aca::test
