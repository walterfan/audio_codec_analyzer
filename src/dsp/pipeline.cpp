#include "aca/pipeline.h"

#include <speex/speex_echo.h>
#include <speex/speex_preprocess.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>

#include "aca/metrics.h"
#include "aca/speex_util.h"

namespace aca {

struct CaptureChain::Impl {
  SpeexEchoState* echo = nullptr;
  SpeexPreprocessState* pre = nullptr;
  bool aec = false;

  ~Impl() {
    if (pre) speex_preprocess_state_destroy(pre);
    if (echo) speex_echo_state_destroy(echo);
  }
};

CaptureChain::CaptureChain(int sample_rate, const PipelineConfig& cfg)
    : impl_(std::make_unique<Impl>()) {
  if (sample_rate <= 0) throw std::runtime_error("pipeline: invalid sample rate");
  frame_size_ = frames_for_ms(sample_rate, cfg.frame_ms);
  if (frame_size_ == 0) throw std::runtime_error("pipeline: frame_ms too small");

  const int fs = static_cast<int>(frame_size_);

  if (cfg.enable_aec) {
    const int filter_len =
        static_cast<int>(frames_for_ms(sample_rate, cfg.aec.filter_ms));
    impl_->echo = speex_echo_state_init(fs, filter_len);
    if (!impl_->echo) throw std::runtime_error("speex_echo_state_init failed");
    int rate = sample_rate;
    speex_echo_ctl(impl_->echo, SPEEX_ECHO_SET_SAMPLING_RATE, &rate);
    impl_->aec = true;
  }

  // One shared preprocess state runs ANS and AGC in the correct internal
  // order (denoise then gain), which is why they are not two states here.
  if (cfg.enable_ans || cfg.enable_agc || cfg.enable_aec) {
    impl_->pre = speex_preprocess_state_init(fs, sample_rate);
    if (!impl_->pre) throw std::runtime_error("speex_preprocess_state_init failed");

    int denoise = cfg.enable_ans ? 1 : 0;
    speex_preprocess_ctl(impl_->pre, SPEEX_PREPROCESS_SET_DENOISE, &denoise);
    if (cfg.enable_ans) {
      int suppress = cfg.ans.suppression_db > 0 ? -cfg.ans.suppression_db
                                                : cfg.ans.suppression_db;
      speex_preprocess_ctl(impl_->pre, SPEEX_PREPROCESS_SET_NOISE_SUPPRESS,
                           &suppress);
      int dereverb = cfg.ans.dereverb ? 1 : 0;
      speex_preprocess_ctl(impl_->pre, SPEEX_PREPROCESS_SET_DEREVERB,
                           &dereverb);
    }

    int agc = cfg.enable_agc ? 1 : 0;
    speex_preprocess_ctl(impl_->pre, SPEEX_PREPROCESS_SET_AGC, &agc);
    if (cfg.enable_agc) {
      float level = dbfs_to_agc_level(cfg.agc.target_level_dbfs);
      speex_preprocess_ctl(impl_->pre, SPEEX_PREPROCESS_SET_AGC_LEVEL, &level);
      int max_gain = cfg.agc.max_gain_db;
      speex_preprocess_ctl(impl_->pre, SPEEX_PREPROCESS_SET_AGC_MAX_GAIN,
                           &max_gain);
      int inc = cfg.agc.gain_increment_db;
      speex_preprocess_ctl(impl_->pre, SPEEX_PREPROCESS_SET_AGC_INCREMENT,
                           &inc);
      int dec = cfg.agc.gain_decrement_db > 0 ? -cfg.agc.gain_decrement_db
                                              : cfg.agc.gain_decrement_db;
      speex_preprocess_ctl(impl_->pre, SPEEX_PREPROCESS_SET_AGC_DECREMENT,
                           &dec);
    }

    if (impl_->echo) {
      speex_preprocess_ctl(impl_->pre, SPEEX_PREPROCESS_SET_ECHO_STATE,
                           impl_->echo);
      int suppress = -40;
      speex_preprocess_ctl(impl_->pre, SPEEX_PREPROCESS_SET_ECHO_SUPPRESS,
                           &suppress);
      int suppress_active = -15;
      speex_preprocess_ctl(impl_->pre,
                           SPEEX_PREPROCESS_SET_ECHO_SUPPRESS_ACTIVE,
                           &suppress_active);
    }
  }
}

CaptureChain::~CaptureChain() = default;

void CaptureChain::process_frame(const int16_t* mic, const int16_t* speaker,
                                 int16_t* out) {
  // 1. AEC first, on the raw mic signal.
  if (impl_->aec && speaker) {
    speex_echo_cancellation(impl_->echo, mic, speaker, out);
  } else if (mic != out) {
    std::memcpy(out, mic, frame_size_ * sizeof(int16_t));
  }

  // 2+3. ANS then AGC, in-place, inside the shared preprocess state.
  if (impl_->pre) {
    speex_preprocess_run(impl_->pre, out);
  }
}

AudioBuffer run_pipeline(const AudioBuffer& mic, const AudioBuffer* reference,
                         const PipelineConfig& cfg, PipelineReport* report) {
  if (cfg.enable_aec && !reference) {
    throw std::runtime_error(
        "pipeline: AEC is enabled but no reference/speaker signal was given");
  }
  if (cfg.enable_aec && reference->sample_rate != mic.sample_rate) {
    throw std::runtime_error("pipeline: mic and reference sample rates differ");
  }

  AudioBuffer m = mic;
  m.to_mono();

  CaptureChain chain(m.sample_rate, cfg);
  const size_t fs = chain.frame_size();

  AudioBuffer ref;
  if (cfg.enable_aec) {
    ref = *reference;
    ref.to_mono();
  }

  const size_t n = std::max(m.samples.size(), ref.samples.size());
  const size_t padded = fs > 0 ? ((n + fs - 1) / fs) * fs : 0;
  m.samples.resize(padded, 0);
  if (cfg.enable_aec) ref.samples.resize(padded, 0);

  AudioBuffer out;
  out.sample_rate = m.sample_rate;
  out.channels = 1;
  out.samples.resize(padded);

  size_t frames = 0;
  for (size_t off = 0; off + fs <= padded; off += fs) {
    chain.process_frame(m.samples.data() + off,
                        cfg.enable_aec ? ref.samples.data() + off : nullptr,
                        out.samples.data() + off);
    ++frames;
  }

  // Optional codec leg: encode then immediately decode, so the returned audio
  // is what a remote peer would actually hear.
  std::optional<EncodeStats> codec_stats;
  if (cfg.enable_codec) {
    EncodeStats st;
    FrameStream fstream = encode_to_frames(out, cfg.opus, &st);
    out = decode_from_frames(fstream);
    codec_stats = st;
  }

  if (report) {
    report->input = analyze_levels(mic);
    report->output = analyze_levels(out);
    report->frames_processed = frames;
    report->codec = codec_stats;
    if (cfg.enable_aec) {
      report->erle_db = erle_db(m.samples, out.samples);
    }
  }
  return out;
}

}  // namespace aca
