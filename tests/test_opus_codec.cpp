#include "aca/opus_codec.h"

#include <gtest/gtest.h>

#include "aca/metrics.h"
#include "test_helpers.h"

namespace {

TEST(OpusCodec, RejectsInvalidSampleRate) {
  // 44100 is the classic trap: valid WAV, illegal for Opus.
  EXPECT_THROW(aca::validate_opus_sample_rate(44100), std::runtime_error);
  EXPECT_NO_THROW(aca::validate_opus_sample_rate(48000));
  EXPECT_NO_THROW(aca::validate_opus_sample_rate(8000));
}

TEST(OpusCodec, FrameMsValidation) {
  EXPECT_TRUE(aca::is_valid_opus_frame_ms(20));
  EXPECT_TRUE(aca::is_valid_opus_frame_ms(60));
  EXPECT_FALSE(aca::is_valid_opus_frame_ms(15));
  EXPECT_FALSE(aca::is_valid_opus_frame_ms(30));
}

TEST(OpusCodec, EncoderRejectsBadFrameMs) {
  aca::OpusEncodeConfig cfg;
  cfg.frame_ms = 15;
  EXPECT_THROW(aca::OpusEncoderWrapper(48000, 1, cfg), std::runtime_error);
}

TEST(OpusCodec, RoundTripPreservesDurationAndReducesSize) {
  const auto in = aca::test::make_tone(48000, 1.0, 440.0, 0.5);

  aca::OpusEncodeConfig cfg;
  cfg.bitrate_bps = 32000;
  cfg.frame_ms = 20;

  aca::EncodeStats stats;
  const auto fs = aca::encode_to_frames(in, cfg, &stats);

  EXPECT_EQ(stats.frames, 50u);  // 1 s / 20 ms
  EXPECT_GT(stats.encoded_bytes, 0u);
  // A tone at 32 kbps must be far smaller than 48 kHz * 16-bit PCM.
  EXPECT_LT(stats.encoded_bytes, stats.pcm_bytes / 10);
  EXPECT_GT(stats.compression_ratio(), 10.0);

  const auto out = aca::decode_from_frames(fs);
  EXPECT_EQ(out.sample_rate, 48000);
  EXPECT_EQ(out.channels, 1);
  EXPECT_EQ(out.frames(), in.frames());
}

TEST(OpusCodec, RoundTripKeepsToneEnergy) {
  const auto in = aca::test::make_tone(48000, 0.5, 440.0, 0.5);

  aca::OpusEncodeConfig cfg;
  cfg.bitrate_bps = 64000;
  cfg.frame_ms = 20;
  cfg.mode = aca::OpusAppMode::Audio;

  const auto fs = aca::encode_to_frames(in, cfg);
  const auto out = aca::decode_from_frames(fs);

  // Opus is lossy and delays the signal, so compare energy rather than
  // sample-by-sample: the level must land close to the input.
  const double in_rms = aca::rms_dbfs(in.samples);
  const double out_rms = aca::rms_dbfs(out.samples);
  EXPECT_NEAR(out_rms, in_rms, 3.0);
}

TEST(OpusCodec, HigherBitrateProducesMoreBytes) {
  const auto in = aca::test::make_noise(48000, 0.5, 0.3);

  aca::OpusEncodeConfig low;
  low.bitrate_bps = 12000;
  low.frame_ms = 20;
  aca::EncodeStats low_stats;
  aca::encode_to_frames(in, low, &low_stats);

  aca::OpusEncodeConfig high;
  high.bitrate_bps = 96000;
  high.frame_ms = 20;
  aca::EncodeStats high_stats;
  aca::encode_to_frames(in, high, &high_stats);

  EXPECT_GT(high_stats.encoded_bytes, low_stats.encoded_bytes);
}

TEST(OpusCodec, PacketLossConcealmentStillProducesAudio) {
  const auto in = aca::test::make_tone(48000, 0.5, 440.0, 0.5);

  aca::OpusEncodeConfig cfg;
  cfg.frame_ms = 20;
  const auto fs = aca::encode_to_frames(in, cfg);

  const auto clean = aca::decode_from_frames(fs, 0);
  const auto lossy = aca::decode_from_frames(fs, 5);  // drop every 5th

  // PLC must synthesise a full frame for each dropped packet, so the output
  // length is unchanged -- a length change would mean frames were skipped.
  EXPECT_EQ(lossy.frames(), clean.frames());
  EXPECT_GT(aca::rms_dbfs(lossy.samples), -60.0);
}

TEST(OpusCodec, StereoEncodeDecode) {
  const auto in = aca::test::make_tone(48000, 0.2, 440.0, 0.4, 2);

  aca::OpusEncodeConfig cfg;
  cfg.frame_ms = 20;
  const auto fs = aca::encode_to_frames(in, cfg);
  EXPECT_EQ(fs.channels, 2);

  const auto out = aca::decode_from_frames(fs);
  EXPECT_EQ(out.channels, 2);
  EXPECT_EQ(out.frames(), in.frames());
}

TEST(OpusCodec, TailIsZeroPaddedToWholeFrame) {
  // 0.53 s at 20 ms frames = 26.5 frames -> must round up to 27.
  const auto in = aca::test::make_tone(48000, 0.53, 440.0, 0.5);

  aca::OpusEncodeConfig cfg;
  cfg.frame_ms = 20;
  aca::EncodeStats stats;
  aca::encode_to_frames(in, cfg, &stats);

  EXPECT_EQ(stats.frames, 27u);
}

}  // namespace
