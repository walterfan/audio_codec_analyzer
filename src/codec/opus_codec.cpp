#include "aca/opus_codec.h"

// NOTE: opus.pkg-config already adds -I.../include/opus, so the include is
// <opus.h>, NOT <opus/opus.h>.
#include <opus.h>

#include <algorithm>
#include <stdexcept>
#include <string>

namespace aca {
namespace {

// Opus permits only these input rates.
constexpr int kValidRates[] = {8000, 12000, 16000, 24000, 48000};

// Max bytes an Opus packet can occupy; the API recommends ~4000 for a single
// frame at any sane bitrate.
constexpr size_t kMaxPacket = 4000;

int to_opus_application(OpusAppMode m) {
  switch (m) {
    case OpusAppMode::Audio:    return OPUS_APPLICATION_AUDIO;
    case OpusAppMode::LowDelay: return OPUS_APPLICATION_RESTRICTED_LOWDELAY;
    case OpusAppMode::Voip:
    default:                    return OPUS_APPLICATION_VOIP;
  }
}

void check(int err, const char* what) {
  if (err != OPUS_OK) {
    throw std::runtime_error(std::string(what) + ": " + opus_strerror(err));
  }
}

}  // namespace

void validate_opus_sample_rate(int rate) {
  for (int r : kValidRates) {
    if (r == rate) return;
  }
  throw std::runtime_error(
      "Opus accepts only 8000/12000/16000/24000/48000 Hz, got " +
      std::to_string(rate) + " Hz -- resample the input first");
}

bool is_valid_opus_frame_ms(int frame_ms) {
  return frame_ms == 5 || frame_ms == 10 || frame_ms == 20 ||
         frame_ms == 40 || frame_ms == 60;
}

// ---------------- encoder ----------------

struct OpusEncoderWrapper::Impl {
  OpusEncoder* enc = nullptr;
  int channels = 0;
  ~Impl() {
    if (enc) opus_encoder_destroy(enc);
  }
};

OpusEncoderWrapper::OpusEncoderWrapper(int sample_rate, int channels,
                                       const OpusEncodeConfig& cfg)
    : impl_(std::make_unique<Impl>()) {
  validate_opus_sample_rate(sample_rate);
  if (channels != 1 && channels != 2) {
    throw std::runtime_error("Opus supports 1 or 2 channels, got " +
                             std::to_string(channels));
  }
  if (!is_valid_opus_frame_ms(cfg.frame_ms)) {
    throw std::runtime_error(
        "frame_ms must be 5, 10, 20, 40 or 60, got " +
        std::to_string(cfg.frame_ms));
  }

  int err = OPUS_OK;
  impl_->enc = opus_encoder_create(sample_rate, channels,
                                   to_opus_application(cfg.mode), &err);
  check(err, "opus_encoder_create");
  impl_->channels = channels;

  check(opus_encoder_ctl(impl_->enc, OPUS_SET_BITRATE(cfg.bitrate_bps)),
        "OPUS_SET_BITRATE");
  check(opus_encoder_ctl(impl_->enc, OPUS_SET_COMPLEXITY(cfg.complexity)),
        "OPUS_SET_COMPLEXITY");
  check(opus_encoder_ctl(impl_->enc, OPUS_SET_VBR(cfg.vbr ? 1 : 0)),
        "OPUS_SET_VBR");
  check(opus_encoder_ctl(impl_->enc, OPUS_SET_INBAND_FEC(cfg.fec ? 1 : 0)),
        "OPUS_SET_INBAND_FEC");
  check(opus_encoder_ctl(impl_->enc,
                         OPUS_SET_PACKET_LOSS_PERC(cfg.expected_loss_pct)),
        "OPUS_SET_PACKET_LOSS_PERC");
  check(opus_encoder_ctl(impl_->enc, OPUS_SET_DTX(cfg.dtx ? 1 : 0)),
        "OPUS_SET_DTX");

  frame_size_ = frames_for_ms(sample_rate, cfg.frame_ms);
}

OpusEncoderWrapper::~OpusEncoderWrapper() = default;

std::vector<uint8_t> OpusEncoderWrapper::encode_frame(const int16_t* pcm) {
  std::vector<uint8_t> out(kMaxPacket);
  const int n = opus_encode(impl_->enc, pcm, static_cast<int>(frame_size_),
                            out.data(), static_cast<opus_int32>(out.size()));
  if (n < 0) {
    throw std::runtime_error(std::string("opus_encode: ") + opus_strerror(n));
  }
  out.resize(static_cast<size_t>(n));
  return out;
}

// ---------------- decoder ----------------

struct OpusDecoderWrapper::Impl {
  OpusDecoder* dec = nullptr;
  int channels = 0;
  int sample_rate = 0;
  ~Impl() {
    if (dec) opus_decoder_destroy(dec);
  }
};

OpusDecoderWrapper::OpusDecoderWrapper(int sample_rate, int channels)
    : impl_(std::make_unique<Impl>()) {
  validate_opus_sample_rate(sample_rate);
  int err = OPUS_OK;
  impl_->dec = opus_decoder_create(sample_rate, channels, &err);
  check(err, "opus_decoder_create");
  impl_->channels = channels;
  impl_->sample_rate = sample_rate;
}

OpusDecoderWrapper::~OpusDecoderWrapper() = default;

Pcm16 OpusDecoderWrapper::decode_frame(const uint8_t* data, size_t size,
                                       int max_frame_ms) {
  const size_t max_samples = frames_for_ms(impl_->sample_rate, max_frame_ms);
  Pcm16 out(max_samples * static_cast<size_t>(impl_->channels));

  // data == nullptr tells Opus to synthesise a concealment frame (PLC).
  const int n = opus_decode(impl_->dec, data, static_cast<opus_int32>(size),
                            out.data(), static_cast<int>(max_samples), 0);
  if (n < 0) {
    throw std::runtime_error(std::string("opus_decode: ") + opus_strerror(n));
  }
  out.resize(static_cast<size_t>(n) * static_cast<size_t>(impl_->channels));
  return out;
}

// ---------------- whole-file helpers ----------------

FrameStream encode_to_frames(const AudioBuffer& in, const OpusEncodeConfig& cfg,
                             EncodeStats* stats) {
  OpusEncoderWrapper enc(in.sample_rate, in.channels, cfg);
  const size_t fs = enc.frame_size();
  const size_t ch = static_cast<size_t>(in.channels);
  const size_t stride = fs * ch;

  FrameStream out;
  out.sample_rate = in.sample_rate;
  out.channels = in.channels;
  out.frame_ms = cfg.frame_ms;

  // Zero-pad the tail so the last partial frame still gets encoded.
  Pcm16 padded = in.samples;
  if (stride > 0 && padded.size() % stride != 0) {
    padded.resize(((padded.size() / stride) + 1) * stride, 0);
  }

  for (size_t off = 0; off + stride <= padded.size(); off += stride) {
    out.frames.push_back(enc.encode_frame(padded.data() + off));
  }

  if (stats) {
    stats->frames = out.frames.size();
    stats->encoded_bytes = out.total_payload_bytes();
    stats->pcm_bytes = in.samples.size() * sizeof(int16_t);
    stats->duration_seconds = in.duration_seconds();
  }
  return out;
}

AudioBuffer decode_from_frames(const FrameStream& fs, int drop_every_nth) {
  AudioBuffer out;
  out.sample_rate = fs.sample_rate;
  out.channels = fs.channels;

  OpusDecoderWrapper dec(fs.sample_rate, fs.channels);
  // Allow room for the largest legal Opus frame regardless of what was used.
  const int max_ms = std::max(fs.frame_ms, 60);

  for (size_t i = 0; i < fs.frames.size(); ++i) {
    const bool drop =
        drop_every_nth > 0 &&
        ((i + 1) % static_cast<size_t>(drop_every_nth) == 0);

    Pcm16 pcm = drop ? dec.decode_frame(nullptr, 0, fs.frame_ms)
                     : dec.decode_frame(fs.frames[i].data(),
                                        fs.frames[i].size(), max_ms);
    out.samples.insert(out.samples.end(), pcm.begin(), pcm.end());
  }
  return out;
}

}  // namespace aca
