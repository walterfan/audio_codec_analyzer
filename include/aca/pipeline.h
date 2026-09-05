#pragma once

#include <memory>
#include <optional>

#include "aca/audio_buffer.h"
#include "aca/echo_canceller.h"
#include "aca/metrics.h"
#include "aca/noise_suppressor.h"
#include "aca/opus_codec.h"

namespace aca {

// The real-world capture chain, in the order a conferencing client uses:
//
//   mic -> AEC -> ANS -> AGC -> Opus encode -> [network] -> Opus decode -> out
//
// Order matters and is not arbitrary:
//   * AEC must run FIRST, on the unmodified mic signal. Noise suppression or
//     gain changes applied beforehand make the mic no longer a linear function
//     of the speaker reference, and the adaptive filter stops converging.
//   * AGC must run LAST of the three, so it measures the already-cleaned
//     signal instead of riding the noise floor up and down.
struct PipelineConfig {
  int frame_ms = 10;
  bool enable_aec = false;  // requires a reference/speaker signal
  bool enable_ans = true;
  bool enable_agc = true;
  bool enable_codec = false;

  AecConfig aec{};
  AnsConfig ans{};
  AgcConfig agc{};
  OpusEncodeConfig opus{};
};

struct PipelineReport {
  LevelReport input;
  LevelReport output;
  double erle_db = 0.0;          // only set when AEC ran
  std::optional<EncodeStats> codec;
  size_t frames_processed = 0;
};

// Runs the capture chain over a whole signal. `reference` is required when
// cfg.enable_aec is true.
AudioBuffer run_pipeline(const AudioBuffer& mic,
                         const AudioBuffer* reference,
                         const PipelineConfig& cfg,
                         PipelineReport* report = nullptr);

// Streaming form of the same chain, shared by the offline pipeline and the
// live PortAudio callback. One speexdsp preprocess state backs both ANS+AGC.
class CaptureChain {
 public:
  CaptureChain(int sample_rate, const PipelineConfig& cfg);
  ~CaptureChain();
  CaptureChain(const CaptureChain&) = delete;
  CaptureChain& operator=(const CaptureChain&) = delete;

  // `speaker` may be nullptr when AEC is disabled. Operates on exactly
  // frame_size() mono samples; `mic` and `out` may be the same pointer.
  void process_frame(const int16_t* mic, const int16_t* speaker, int16_t* out);

  size_t frame_size() const { return frame_size_; }

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  size_t frame_size_ = 0;
};

}  // namespace aca
