#include "aca/wav_io.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <string>

#include "test_helpers.h"

namespace {

std::string temp_path(const char* name) {
  return (std::filesystem::temp_directory_path() / name).string();
}

TEST(WavIo, RoundTripPreservesSamples) {
  const auto in = aca::test::make_tone(48000, 0.1, 440.0, 0.5);
  const std::string p = temp_path("aca_roundtrip.wav");

  aca::write_wav(p, in);
  const auto out = aca::read_audio(p);

  EXPECT_EQ(out.sample_rate, in.sample_rate);
  EXPECT_EQ(out.channels, in.channels);
  ASSERT_EQ(out.samples.size(), in.samples.size());
  // 16-bit PCM in, 16-bit PCM out -> must be bit-exact.
  EXPECT_EQ(out.samples, in.samples);

  std::filesystem::remove(p);
}

TEST(WavIo, StereoRoundTrip) {
  const auto in = aca::test::make_tone(16000, 0.05, 300.0, 0.4, 2);
  const std::string p = temp_path("aca_stereo.wav");

  aca::write_wav(p, in);
  const auto out = aca::read_audio(p);

  EXPECT_EQ(out.channels, 2);
  EXPECT_EQ(out.frames(), in.frames());
  std::filesystem::remove(p);
}

TEST(WavIo, MissingFileThrows) {
  EXPECT_THROW(aca::read_audio("/nonexistent/aca/nope.wav"),
               std::runtime_error);
}

TEST(WavIo, InvalidBufferThrows) {
  aca::AudioBuffer bad;  // channels == 0, sample_rate == 0
  EXPECT_THROW(aca::write_wav(temp_path("aca_bad.wav"), bad),
               std::runtime_error);
}

TEST(AudioBuffer, ToMonoAveragesChannels) {
  aca::AudioBuffer b;
  b.sample_rate = 8000;
  b.channels = 2;
  b.samples = {1000, 2000, -500, 500};  // two stereo frames

  b.to_mono();

  EXPECT_EQ(b.channels, 1);
  ASSERT_EQ(b.samples.size(), 2u);
  EXPECT_EQ(b.samples[0], 1500);
  EXPECT_EQ(b.samples[1], 0);
}

TEST(AudioBuffer, FramesAndDuration) {
  const auto b = aca::test::make_tone(48000, 0.5, 100.0);
  EXPECT_EQ(b.frames(), 24000u);
  EXPECT_NEAR(b.duration_seconds(), 0.5, 1e-9);
}

}  // namespace
