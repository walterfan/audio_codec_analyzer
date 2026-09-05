#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "aca/audio_buffer.h"
#include "aca/frame_stream.h"

namespace aca {

enum class OpusAppMode { Voip, Audio, LowDelay };

struct OpusEncodeConfig {
  int bitrate_bps = 24000;
  int frame_ms = 20;         // must be one of 2.5/5/10/20/40/60 -> we allow 5..60
  int complexity = 10;       // 0..10
  bool fec = false;          // in-band forward error correction
  int expected_loss_pct = 0; // only meaningful with fec = true
  bool vbr = true;
  bool dtx = false;          // discontinuous transmission (silence -> tiny frames)
  OpusAppMode mode = OpusAppMode::Voip;
};

// Opus only accepts 8/12/16/24/48 kHz. Throws if `rate` is anything else.
void validate_opus_sample_rate(int rate);
bool is_valid_opus_frame_ms(int frame_ms);

class OpusEncoderWrapper {
 public:
  OpusEncoderWrapper(int sample_rate, int channels, const OpusEncodeConfig& cfg);
  ~OpusEncoderWrapper();
  OpusEncoderWrapper(const OpusEncoderWrapper&) = delete;
  OpusEncoderWrapper& operator=(const OpusEncoderWrapper&) = delete;

  // `pcm` must hold exactly frame_size()*channels samples.
  std::vector<uint8_t> encode_frame(const int16_t* pcm);
  size_t frame_size() const { return frame_size_; }  // samples per channel

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  size_t frame_size_ = 0;
};

class OpusDecoderWrapper {
 public:
  OpusDecoderWrapper(int sample_rate, int channels);
  ~OpusDecoderWrapper();
  OpusDecoderWrapper(const OpusDecoderWrapper&) = delete;
  OpusDecoderWrapper& operator=(const OpusDecoderWrapper&) = delete;

  // Pass data == nullptr to trigger packet-loss concealment for one frame.
  Pcm16 decode_frame(const uint8_t* data, size_t size, int max_frame_ms);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

struct EncodeStats {
  size_t frames = 0;
  size_t encoded_bytes = 0;
  size_t pcm_bytes = 0;
  double duration_seconds = 0.0;
  double effective_bitrate_bps() const {
    return duration_seconds > 0 ? encoded_bytes * 8.0 / duration_seconds : 0.0;
  }
  double compression_ratio() const {
    return encoded_bytes > 0 ? static_cast<double>(pcm_bytes) / encoded_bytes : 0.0;
  }
};

// Whole-file helpers. `encode_to_frames` zero-pads the tail to a whole frame.
FrameStream encode_to_frames(const AudioBuffer& in, const OpusEncodeConfig& cfg,
                             EncodeStats* stats = nullptr);

// `drop_every_nth` > 0 simulates network loss by discarding that packet and
// invoking Opus PLC instead, so the effect of loss is audible in the output.
AudioBuffer decode_from_frames(const FrameStream& fs, int drop_every_nth = 0);

}  // namespace aca
