#include "aca/metrics.h"

#include <gtest/gtest.h>

#include "test_helpers.h"

namespace {

TEST(Metrics, RmsOfSilenceIsFloor) {
  const aca::Pcm16 silence(1000, 0);
  EXPECT_DOUBLE_EQ(aca::rms_dbfs(silence), -120.0);
}

TEST(Metrics, RmsOfEmptyIsFloor) {
  const aca::Pcm16 empty;
  EXPECT_DOUBLE_EQ(aca::rms_dbfs(empty), -120.0);
}

TEST(Metrics, PeakOfFullScaleIsZeroDbfs) {
  const aca::Pcm16 v = {32767, -32768, 0};
  EXPECT_NEAR(aca::peak_dbfs(v), 0.0, 0.01);
}

TEST(Metrics, RmsOfHalfScaleSineIsAboutMinus9) {
  // A sine at amplitude 0.5 has RMS 0.5/sqrt(2) = 0.3536 -> about -9 dBFS.
  const auto tone = aca::test::make_tone(48000, 1.0, 1000.0, 0.5);
  EXPECT_NEAR(aca::rms_dbfs(tone.samples), -9.03, 0.2);
}

TEST(Metrics, SnrOfIdenticalSignalsIsHuge) {
  const auto a = aca::test::make_tone(16000, 0.1, 440.0, 0.5);
  EXPECT_GT(aca::snr_db(a.samples, a.samples), 100.0);
}

TEST(Metrics, SnrDegradesWithNoise) {
  const auto clean = aca::test::make_tone(16000, 0.5, 440.0, 0.5);
  const auto small = aca::test::mix(clean, aca::test::make_noise(16000, 0.5, 0.01));
  const auto large = aca::test::mix(clean, aca::test::make_noise(16000, 0.5, 0.1));

  const double snr_small = aca::snr_db(clean.samples, small.samples);
  const double snr_large = aca::snr_db(clean.samples, large.samples);
  EXPECT_GT(snr_small, snr_large);
}

TEST(Metrics, ErleIsPositiveWhenEnergyDrops) {
  const auto before = aca::test::make_tone(16000, 0.5, 440.0, 0.5);
  const auto after = aca::test::make_tone(16000, 0.5, 440.0, 0.05);  // -20 dB
  EXPECT_NEAR(aca::erle_db(before.samples, after.samples), 20.0, 1.0);
}

TEST(Metrics, ErleIsZeroForSilentInput) {
  const aca::Pcm16 silence(100, 0);
  EXPECT_DOUBLE_EQ(aca::erle_db(silence, silence), 0.0);
}

TEST(Metrics, AnalyzeLevelsCountsClipping) {
  aca::AudioBuffer b;
  b.sample_rate = 8000;
  b.channels = 1;
  b.samples = {32767, 32767, -32768, 100};

  const auto r = aca::analyze_levels(b);
  EXPECT_EQ(r.clipped_samples, 3u);
  EXPECT_NEAR(r.duration_seconds, 4.0 / 8000.0, 1e-9);
}

}  // namespace
