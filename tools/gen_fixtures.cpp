// Generates the synthetic WAV fixtures used by the tests and the demo.
// Everything is synthesised, so the repo carries no binary audio assets.
//
//   assets/speech.wav       clean-ish "voice" (sum of formant-like tones)
//   assets/noisy.wav        speech + white noise            -> for `aca ans`
//   assets/quiet.wav        speech at -30 dBFS              -> for `aca agc`
//   assets/far_end.wav      far-end signal played to speaker
//   assets/mic_echo.wav     speech + delayed/attenuated far-end -> for `aca aec`

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#include "aca/audio_buffer.h"
#include "aca/wav_io.h"

namespace {

constexpr int kRate = 48000;
constexpr double kSeconds = 3.0;

double clampf(double v, double lo, double hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

int16_t to_i16(double v) {
  return static_cast<int16_t>(clampf(v, -1.0, 1.0) * 32767.0);
}

// A crude voiced-speech stand-in: a 140 Hz "pitch" with formant-ish harmonics,
// amplitude-modulated into syllable-like bursts.
std::vector<double> make_speech(int rate, double seconds) {
  const size_t n = static_cast<size_t>(rate * seconds);
  std::vector<double> out(n);
  for (size_t i = 0; i < n; ++i) {
    const double t = static_cast<double>(i) / rate;
    const double f0 = 140.0;
    double s = 0.60 * std::sin(2 * M_PI * f0 * t);
    s += 0.25 * std::sin(2 * M_PI * 2 * f0 * t);
    s += 0.15 * std::sin(2 * M_PI * 700 * t);
    s += 0.08 * std::sin(2 * M_PI * 1220 * t);

    // Syllable envelope at ~4 Hz with short gaps, so VAD/AGC have something
    // to react to.
    const double syl = 0.5 * (1.0 - std::cos(2 * M_PI * 4.0 * t));
    const double gate = (std::fmod(t, 1.0) < 0.75) ? 1.0 : 0.05;
    out[i] = s * syl * gate * 0.5;
  }
  return out;
}

aca::AudioBuffer to_buffer(const std::vector<double>& x) {
  aca::AudioBuffer b;
  b.sample_rate = kRate;
  b.channels = 1;
  b.samples.resize(x.size());
  for (size_t i = 0; i < x.size(); ++i) b.samples[i] = to_i16(x[i]);
  return b;
}

double rms(const std::vector<double>& x) {
  double acc = 0.0;
  for (double v : x) acc += v * v;
  return x.empty() ? 0.0 : std::sqrt(acc / x.size());
}

}  // namespace

int main(int argc, char** argv) {
  const std::string dir = argc > 1 ? argv[1] : "assets";

  std::mt19937 rng(12345);  // fixed seed -> reproducible fixtures
  std::normal_distribution<double> gauss(0.0, 1.0);

  const std::vector<double> speech = make_speech(kRate, kSeconds);
  const size_t n = speech.size();

  // 1. clean speech
  aca::write_wav(dir + "/speech.wav", to_buffer(speech));

  // 2. speech + white noise at roughly -20 dB relative to the speech
  const double noise_amp = rms(speech) * 0.1;
  std::vector<double> noisy(n);
  for (size_t i = 0; i < n; ++i) {
    noisy[i] = speech[i] + noise_amp * gauss(rng);
  }
  aca::write_wav(dir + "/noisy.wav", to_buffer(noisy));

  // 3. very quiet speech, for AGC to pull back up
  std::vector<double> quiet(n);
  const double quiet_gain = std::pow(10.0, -27.0 / 20.0);
  for (size_t i = 0; i < n; ++i) quiet[i] = speech[i] * quiet_gain;
  aca::write_wav(dir + "/quiet.wav", to_buffer(quiet));

  // 4. far-end signal: a different tone pattern so it is distinguishable
  std::vector<double> far(n);
  for (size_t i = 0; i < n; ++i) {
    const double t = static_cast<double>(i) / kRate;
    far[i] = 0.45 * (std::sin(2 * M_PI * 320.0 * t) +
                     0.5 * std::sin(2 * M_PI * 880.0 * t)) *
             (0.6 + 0.4 * std::sin(2 * M_PI * 1.7 * t));
  }
  aca::write_wav(dir + "/far_end.wav", to_buffer(far));

  // 5. mic signal = near speech + a simple 3-tap echo of the far end.
  // The delays (~12/25/40 ms) sit inside the default 100 ms AEC tail.
  const size_t d1 = static_cast<size_t>(kRate * 0.012);
  const size_t d2 = static_cast<size_t>(kRate * 0.025);
  const size_t d3 = static_cast<size_t>(kRate * 0.040);
  std::vector<double> mic(n);
  for (size_t i = 0; i < n; ++i) {
    double echo = 0.0;
    if (i >= d1) echo += 0.50 * far[i - d1];
    if (i >= d2) echo += 0.25 * far[i - d2];
    if (i >= d3) echo += 0.12 * far[i - d3];
    mic[i] = speech[i] * 0.7 + echo;
  }
  aca::write_wav(dir + "/mic_echo.wav", to_buffer(mic));

  std::printf("wrote fixtures to %s/: speech.wav noisy.wav quiet.wav "
              "far_end.wav mic_echo.wav\n",
              dir.c_str());
  return 0;
}
