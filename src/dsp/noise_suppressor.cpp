#include "aca/noise_suppressor.h"

#include <speex/speex_preprocess.h>

#include <stdexcept>

#include "aca/speex_util.h"

namespace aca {

struct NoiseSuppressor::Impl {
  SpeexPreprocessState* st = nullptr;
  ~Impl() {
    if (st) speex_preprocess_state_destroy(st);
  }
};

NoiseSuppressor::NoiseSuppressor(int sample_rate, const AnsConfig& cfg)
    : impl_(std::make_unique<Impl>()) {
  if (sample_rate <= 0) throw std::runtime_error("ANS: invalid sample rate");
  frame_size_ = frames_for_ms(sample_rate, cfg.frame_ms);
  if (frame_size_ == 0) throw std::runtime_error("ANS: frame_ms too small");

  impl_->st = speex_preprocess_state_init(static_cast<int>(frame_size_),
                                          sample_rate);
  if (!impl_->st) throw std::runtime_error("speex_preprocess_state_init failed");

  int on = 1;
  int off = 0;
  speex_preprocess_ctl(impl_->st, SPEEX_PREPROCESS_SET_DENOISE, &on);
  // Keep AGC out of the way so `aca ans` measures denoising alone.
  speex_preprocess_ctl(impl_->st, SPEEX_PREPROCESS_SET_AGC, &off);
  speex_preprocess_ctl(impl_->st, SPEEX_PREPROCESS_SET_VAD, &on);

  // NOTE: speexdsp expects a NEGATIVE dB value here (e.g. -25). Passing a
  // positive number silently gives you *less* suppression, not more, so the
  // sign is normalised for the caller.
  int suppress =
      cfg.suppression_db > 0 ? -cfg.suppression_db : cfg.suppression_db;
  speex_preprocess_ctl(impl_->st, SPEEX_PREPROCESS_SET_NOISE_SUPPRESS,
                       &suppress);

  int dereverb = cfg.dereverb ? 1 : 0;
  speex_preprocess_ctl(impl_->st, SPEEX_PREPROCESS_SET_DEREVERB, &dereverb);
}

NoiseSuppressor::~NoiseSuppressor() = default;

bool NoiseSuppressor::process_frame(int16_t* frame) {
  return speex_preprocess_run(impl_->st, frame) != 0;
}

AudioBuffer suppress_noise(const AudioBuffer& in, const AnsConfig& cfg) {
  NoiseSuppressor ns(in.sample_rate, cfg);
  const size_t fs = ns.frame_size();
  AudioBuffer out = to_padded_mono(in, fs);
  for (size_t off = 0; off + fs <= out.samples.size(); off += fs) {
    ns.process_frame(out.samples.data() + off);
  }
  return out;
}

}  // namespace aca
