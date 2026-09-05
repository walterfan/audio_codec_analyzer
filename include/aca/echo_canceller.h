#pragma once

#include <memory>

#include "aca/audio_buffer.h"

namespace aca {

struct AecConfig {
  int frame_ms = 10;    // speexdsp AEC works best with 10-20 ms frames
  int filter_ms = 100;  // tail length: must exceed the room's reverb time
  bool with_preprocess = true;  // link a preprocessor for residual echo removal
};

// Acoustic Echo Cancellation.
//
// Terminology (easy to get backwards):
//   far-end / reference / "speaker" = what we PLAY OUT
//   near-end / "mic"                = what the MIC PICKED UP (speech + echo)
// The canceller subtracts an adaptively-filtered copy of the far-end from the
// near-end. Mono, int16 only.
class EchoCanceller {
 public:
  EchoCanceller(int sample_rate, const AecConfig& cfg);
  ~EchoCanceller();
  EchoCanceller(const EchoCanceller&) = delete;
  EchoCanceller& operator=(const EchoCanceller&) = delete;

  // Processes exactly frame_size() samples. `out` may alias neither input.
  void process_frame(const int16_t* mic, const int16_t* speaker, int16_t* out);

  size_t frame_size() const { return frame_size_; }

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  size_t frame_size_ = 0;
};

// Whole-signal convenience wrapper. `mic` and `speaker` are downmixed to mono
// and zero-padded to the same length before processing.
AudioBuffer cancel_echo(const AudioBuffer& mic, const AudioBuffer& speaker,
                        const AecConfig& cfg);

}  // namespace aca
