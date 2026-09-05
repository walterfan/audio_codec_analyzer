#include <speex/speex_preprocess.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "aca/noise_suppressor.h"
#include "aca/speex_util.h"

namespace aca {

struct GainController::Impl {
  SpeexPreprocessState* st = nullptr;
  ~Impl() {
    if (st) speex_preprocess_state_destroy(st);
  }
};

GainController::GainController(int sample_rate, const AgcConfig& cfg)
    : impl_(std::make_unique<Impl>()) {
  if (sample_rate <= 0) throw std::runtime_error("AGC: invalid sample rate");
  frame_size_ = frames_for_ms(sample_rate, cfg.frame_ms);
  if (frame_size_ == 0) throw std::runtime_error("AGC: frame_ms too small");

  impl_->st = speex_preprocess_state_init(static_cast<int>(frame_size_),
                                          sample_rate);
  if (!impl_->st) throw std::runtime_error("speex_preprocess_state_init failed");

  int on = 1;
  int off = 0;
  speex_preprocess_ctl(impl_->st, SPEEX_PREPROCESS_SET_AGC, &on);
  // Denoise off so `aca agc` isolates the gain behaviour.
  speex_preprocess_ctl(impl_->st, SPEEX_PREPROCESS_SET_DENOISE, &off);

  float level = dbfs_to_agc_level(cfg.target_level_dbfs);
  speex_preprocess_ctl(impl_->st, SPEEX_PREPROCESS_SET_AGC_LEVEL, &level);

  int max_gain = cfg.max_gain_db;
  speex_preprocess_ctl(impl_->st, SPEEX_PREPROCESS_SET_AGC_MAX_GAIN, &max_gain);
  int inc = cfg.gain_increment_db;
  speex_preprocess_ctl(impl_->st, SPEEX_PREPROCESS_SET_AGC_INCREMENT, &inc);
  int dec = cfg.gain_decrement_db > 0 ? -cfg.gain_decrement_db
                                      : cfg.gain_decrement_db;
  speex_preprocess_ctl(impl_->st, SPEEX_PREPROCESS_SET_AGC_DECREMENT, &dec);
}

GainController::~GainController() = default;

void GainController::process_frame(int16_t* frame) {
  speex_preprocess_run(impl_->st, frame);
}

AudioBuffer control_gain(const AudioBuffer& in, const AgcConfig& cfg) {
  GainController agc(in.sample_rate, cfg);
  const size_t fs = agc.frame_size();
  AudioBuffer out = to_padded_mono(in, fs);
  for (size_t off = 0; off + fs <= out.samples.size(); off += fs) {
    agc.process_frame(out.samples.data() + off);
  }
  return out;
}

}  // namespace aca
