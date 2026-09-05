#pragma once

#include <memory>

#include "aca/audio_buffer.h"

namespace aca {

struct AnsConfig {
  int frame_ms = 10;
  int suppression_db = -25;  // speexdsp wants a NEGATIVE value, e.g. -25
  bool dereverb = false;
};

struct AgcConfig {
  int frame_ms = 10;
  float target_level_dbfs = -3.0f;  // mapped onto speexdsp's linear AGC_LEVEL
  int max_gain_db = 30;
  int gain_increment_db = 12;
  int gain_decrement_db = -40;
};

// ANS (noise suppression) and AGC (auto gain) both live in speexdsp's single
// SpeexPreprocessState. They are exposed as two classes for clarity, but a
// combined `Pipeline` shares one state so the gain control sees the denoised
// signal -- see aca/pipeline.h.
class NoiseSuppressor {
 public:
  NoiseSuppressor(int sample_rate, const AnsConfig& cfg);
  ~NoiseSuppressor();
  NoiseSuppressor(const NoiseSuppressor&) = delete;
  NoiseSuppressor& operator=(const NoiseSuppressor&) = delete;

  // In-place on exactly frame_size() samples. Returns speexdsp's VAD verdict
  // (true = speech present); only meaningful when VAD is compiled in.
  bool process_frame(int16_t* frame);
  size_t frame_size() const { return frame_size_; }

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  size_t frame_size_ = 0;
};

class GainController {
 public:
  GainController(int sample_rate, const AgcConfig& cfg);
  ~GainController();
  GainController(const GainController&) = delete;
  GainController& operator=(const GainController&) = delete;

  void process_frame(int16_t* frame);
  size_t frame_size() const { return frame_size_; }

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  size_t frame_size_ = 0;
};

AudioBuffer suppress_noise(const AudioBuffer& in, const AnsConfig& cfg);
AudioBuffer control_gain(const AudioBuffer& in, const AgcConfig& cfg);

}  // namespace aca
