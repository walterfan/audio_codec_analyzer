#include "aca/echo_canceller.h"

#include <speex/speex_echo.h>
#include <speex/speex_preprocess.h>

#include <algorithm>
#include <stdexcept>
#include <string>


namespace aca {

struct EchoCanceller::Impl {
  SpeexEchoState* echo = nullptr;
  SpeexPreprocessState* pre = nullptr;

  ~Impl() {
    // Destroy the preprocessor first: it holds a pointer to the echo state
    // (SPEEX_PREPROCESS_SET_ECHO_STATE) and touches it while tearing down.
    if (pre) speex_preprocess_state_destroy(pre);
    if (echo) speex_echo_state_destroy(echo);
  }
};

EchoCanceller::EchoCanceller(int sample_rate, const AecConfig& cfg)
    : impl_(std::make_unique<Impl>()) {
  if (sample_rate <= 0) throw std::runtime_error("AEC: invalid sample rate");
  if (cfg.frame_ms <= 0) throw std::runtime_error("AEC: invalid frame_ms");

  frame_size_ = frames_for_ms(sample_rate, cfg.frame_ms);
  const int filter_len =
      static_cast<int>(frames_for_ms(sample_rate, cfg.filter_ms));
  if (frame_size_ == 0 || filter_len <= 0) {
    throw std::runtime_error("AEC: frame/filter length resolves to 0 samples");
  }

  impl_->echo = speex_echo_state_init(static_cast<int>(frame_size_), filter_len);
  if (!impl_->echo) throw std::runtime_error("speex_echo_state_init failed");

  // The echo state must be told the rate explicitly; it does not infer it.
  int rate = sample_rate;
  speex_echo_ctl(impl_->echo, SPEEX_ECHO_SET_SAMPLING_RATE, &rate);

  if (cfg.with_preprocess) {
    impl_->pre = speex_preprocess_state_init(static_cast<int>(frame_size_),
                                             sample_rate);
    if (!impl_->pre) throw std::runtime_error("speex_preprocess_state_init failed");

    // Wiring the echo state into the preprocessor enables residual echo
    // suppression, which cleans up what the linear filter leaves behind.
    speex_preprocess_ctl(impl_->pre, SPEEX_PREPROCESS_SET_ECHO_STATE,
                         impl_->echo);
    int suppress = -40;
    speex_preprocess_ctl(impl_->pre, SPEEX_PREPROCESS_SET_ECHO_SUPPRESS,
                         &suppress);
    int suppress_active = -15;
    speex_preprocess_ctl(impl_->pre, SPEEX_PREPROCESS_SET_ECHO_SUPPRESS_ACTIVE,
                         &suppress_active);
  }
}

EchoCanceller::~EchoCanceller() = default;

void EchoCanceller::process_frame(const int16_t* mic, const int16_t* speaker,
                                  int16_t* out) {
  speex_echo_cancellation(impl_->echo, mic, speaker, out);
  if (impl_->pre) {
    speex_preprocess_run(impl_->pre, out);
  }
}

AudioBuffer cancel_echo(const AudioBuffer& mic, const AudioBuffer& speaker,
                        const AecConfig& cfg) {
  if (mic.sample_rate != speaker.sample_rate) {
    throw std::runtime_error(
        "AEC: mic and reference sample rates differ (" +
        std::to_string(mic.sample_rate) + " vs " +
        std::to_string(speaker.sample_rate) + ")");
  }

  AudioBuffer m = mic;
  AudioBuffer s = speaker;
  m.to_mono();
  s.to_mono();

  EchoCanceller aec(m.sample_rate, cfg);
  const size_t fs = aec.frame_size();

  // Pad both to a whole number of frames and to equal length.
  const size_t n = std::max(m.samples.size(), s.samples.size());
  const size_t padded = ((n + fs - 1) / fs) * fs;
  m.samples.resize(padded, 0);
  s.samples.resize(padded, 0);

  AudioBuffer out;
  out.sample_rate = m.sample_rate;
  out.channels = 1;
  out.samples.resize(padded);

  for (size_t off = 0; off + fs <= padded; off += fs) {
    aec.process_frame(m.samples.data() + off, s.samples.data() + off,
                      out.samples.data() + off);
  }
  return out;
}

}  // namespace aca
