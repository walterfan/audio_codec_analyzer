#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "aca/echo_canceller.h"
#include "aca/metrics.h"
#include "aca/noise_suppressor.h"
#include "aca/pipeline.h"
#include "test_helpers.h"

namespace {

// Builds a (mic, far_end) pair where the mic contains a delayed, attenuated
// copy of the far end -- the situation AEC exists to fix.
struct EchoScenario {
  aca::AudioBuffer mic;
  aca::AudioBuffer far;
};

EchoScenario make_echo_scenario(int rate, double seconds,
                                double near_gain = 0.0) {
  auto far = aca::test::make_tone(rate, seconds, 320.0, 0.5);
  auto near = aca::test::make_tone(rate, seconds, 140.0, 0.5);

  const size_t delay = static_cast<size_t>(rate * 0.010);  // 10 ms
  aca::AudioBuffer mic;
  mic.sample_rate = rate;
  mic.channels = 1;
  mic.samples.assign(far.samples.size(), 0);

  for (size_t i = 0; i < mic.samples.size(); ++i) {
    double v = 0.0;
    if (i >= delay) v += 0.6 * far.samples[i - delay] / 32768.0;
    v += near_gain * near.samples[i] / 32768.0;
    v = std::max(-1.0, std::min(1.0, v));
    mic.samples[i] = static_cast<int16_t>(v * 32767.0);
  }
  return {mic, far};
}

TEST(Aec, ReducesEchoEnergy) {
  // Pure echo, no near-end speech: the canceller should remove most of it.
  const auto sc = make_echo_scenario(16000, 4.0, 0.0);

  aca::AecConfig cfg;
  cfg.frame_ms = 10;
  cfg.filter_ms = 100;

  const auto out = aca::cancel_echo(sc.mic, sc.far, cfg);

  const double erle = aca::erle_db(sc.mic.samples, out.samples);
  // The adaptive filter needs time to converge; over 4 s it should achieve a
  // clear reduction. Threshold kept loose so the test is not flaky.
  EXPECT_GT(erle, 6.0) << "ERLE was only " << erle << " dB";
}

TEST(Aec, PreservesNearEndSpeechSomewhat) {
  const auto sc = make_echo_scenario(16000, 3.0, 0.5);

  aca::AecConfig cfg;
  const auto out = aca::cancel_echo(sc.mic, sc.far, cfg);

  // Near-end speech must survive: output should not be near-silent.
  EXPECT_GT(aca::rms_dbfs(out.samples), -50.0);
}

TEST(Aec, MismatchedSampleRatesThrow) {
  const auto mic = aca::test::make_tone(16000, 0.2, 200.0);
  const auto far = aca::test::make_tone(48000, 0.2, 200.0);
  EXPECT_THROW(aca::cancel_echo(mic, far, aca::AecConfig{}),
               std::runtime_error);
}

TEST(Aec, OutputLengthIsFrameAligned) {
  const auto sc = make_echo_scenario(16000, 0.25, 0.0);
  aca::AecConfig cfg;
  cfg.frame_ms = 10;  // 160 samples at 16 kHz

  const auto out = aca::cancel_echo(sc.mic, sc.far, cfg);
  EXPECT_EQ(out.samples.size() % 160u, 0u);
  EXPECT_GE(out.samples.size(), sc.mic.samples.size());
}

TEST(Ans, ReducesNoiseFloorOnPureNoise) {
  const auto noise = aca::test::make_noise(16000, 2.0, 0.05);

  aca::AnsConfig cfg;
  cfg.frame_ms = 10;
  cfg.suppression_db = -30;

  const auto out = aca::suppress_noise(noise, cfg);

  const double before = aca::rms_dbfs(noise.samples);
  const double after = aca::rms_dbfs(out.samples);
  EXPECT_LT(after, before) << "before " << before << " after " << after;
}

TEST(Ans, PositiveSuppressionDbIsNormalized) {
  const auto noise = aca::test::make_noise(16000, 1.0, 0.05);

  // speexdsp wants a negative dB value; the wrapper flips a positive one so
  // callers cannot silently get the opposite of what they asked for.
  aca::AnsConfig neg;
  neg.suppression_db = -30;
  aca::AnsConfig pos;
  pos.suppression_db = 30;

  const auto a = aca::suppress_noise(noise, neg);
  const auto b = aca::suppress_noise(noise, pos);
  EXPECT_EQ(a.samples, b.samples);
}

TEST(Ans, KeepsToneWhileRemovingNoise) {
  const auto tone = aca::test::make_tone(16000, 2.0, 440.0, 0.4);
  const auto noise = aca::test::make_noise(16000, 2.0, 0.02);
  const auto noisy = aca::test::mix(tone, noise);

  aca::AnsConfig cfg;
  cfg.suppression_db = -25;
  const auto out = aca::suppress_noise(noisy, cfg);

  // The tone dominates, so the level should stay in the same ballpark rather
  // than collapse to silence.
  EXPECT_NEAR(aca::rms_dbfs(out.samples), aca::rms_dbfs(noisy.samples), 12.0);
}

TEST(Agc, RaisesQuietSignal) {
  // Speech-like (non-stationary) input at ~-48 dBFS; the AGC ramps gain in
  // over a few seconds, so 4 s gives it room to converge.
  const auto quiet = aca::test::make_speech_like(16000, 4.0, 0.03);

  aca::AgcConfig cfg;
  cfg.frame_ms = 10;
  cfg.target_level_dbfs = -3.0f;
  cfg.max_gain_db = 30;

  const auto out = aca::control_gain(quiet, cfg);

  const double before = aca::rms_dbfs(quiet.samples);
  const double after = aca::rms_dbfs(out.samples);
  EXPECT_GT(after, before + 10.0)
      << "before " << before << " after " << after;
}

TEST(Agc, DoesNotBlowUpLoudSignal) {
  const auto loud = aca::test::make_speech_like(16000, 2.0, 0.9);

  aca::AgcConfig cfg;
  cfg.target_level_dbfs = -3.0f;
  const auto out = aca::control_gain(loud, cfg);

  // Must not push an already-hot signal into heavy clipping.
  EXPECT_LE(aca::peak_dbfs(out.samples), 0.1);
}

TEST(Ans, AttenuatesStationaryToneMoreThanSpeech) {
  // Documents a real speexdsp behaviour that surprises people: the noise
  // estimator is stationary-signal based, so a constant sine is treated as
  // noise (measured ~-7 dB) while speech-like audio passes almost untouched
  // (~-0.8 dB). Never use a plain tone to "verify" ANS keeps speech intact.
  aca::AnsConfig cfg;
  cfg.suppression_db = -30;

  const auto tone = aca::test::make_tone(16000, 2.0, 440.0, 0.4);
  const auto speech = aca::test::make_speech_like(16000, 2.0, 0.4);

  const double tone_delta = aca::rms_dbfs(aca::suppress_noise(tone, cfg).samples) -
                            aca::rms_dbfs(tone.samples);
  const double speech_delta =
      aca::rms_dbfs(aca::suppress_noise(speech, cfg).samples) -
      aca::rms_dbfs(speech.samples);

  EXPECT_LT(tone_delta, -3.0) << "tone delta " << tone_delta;
  EXPECT_GT(speech_delta, -3.0) << "speech delta " << speech_delta;
  EXPECT_LT(tone_delta, speech_delta - 3.0);
}

TEST(Ans, PreservesSpeechLikeSignal) {
  const auto speech = aca::test::make_speech_like(16000, 2.0, 0.5);

  aca::AnsConfig cfg;
  cfg.suppression_db = -25;
  const auto out = aca::suppress_noise(speech, cfg);

  // Non-stationary speech must pass through essentially untouched.
  EXPECT_NEAR(aca::rms_dbfs(out.samples), aca::rms_dbfs(speech.samples), 3.0);
}

TEST(Pipeline, RequiresReferenceWhenAecEnabled) {
  const auto mic = aca::test::make_tone(16000, 0.2, 300.0);
  aca::PipelineConfig cfg;
  cfg.enable_aec = true;
  EXPECT_THROW(aca::run_pipeline(mic, nullptr, cfg), std::runtime_error);
}

TEST(Pipeline, AnsAgcOnlyRunsWithoutReference) {
  // Speech-like, not a tone: ANS would suppress a stationary tone and the AGC
  // gain would be swamped by that attenuation.
  const auto quiet = aca::test::make_speech_like(16000, 4.0, 0.03);

  aca::PipelineConfig cfg;
  cfg.frame_ms = 10;
  cfg.enable_aec = false;
  cfg.enable_ans = true;
  cfg.enable_agc = true;

  aca::PipelineReport report;
  const auto out = aca::run_pipeline(quiet, nullptr, cfg, &report);

  EXPECT_GT(report.frames_processed, 0u);
  EXPECT_EQ(out.channels, 1);
  EXPECT_GT(report.output.rms_dbfs, report.input.rms_dbfs);
}

TEST(Pipeline, FullChainWithAecAndCodec) {
  const auto sc = make_echo_scenario(48000, 2.0, 0.4);

  aca::PipelineConfig cfg;
  cfg.frame_ms = 10;
  cfg.enable_aec = true;
  cfg.enable_ans = true;
  cfg.enable_agc = true;
  cfg.enable_codec = true;
  cfg.opus.frame_ms = 20;
  cfg.opus.bitrate_bps = 24000;

  aca::PipelineReport report;
  const auto out = aca::run_pipeline(sc.mic, &sc.far, cfg, &report);

  ASSERT_TRUE(report.codec.has_value());
  EXPECT_GT(report.codec->frames, 0u);
  EXPECT_GT(report.codec->encoded_bytes, 0u);
  EXPECT_GT(report.erle_db, 0.0);
  EXPECT_FALSE(out.empty());
}

TEST(Pipeline, DisablingEverythingIsPassthrough) {
  const auto in = aca::test::make_tone(16000, 0.2, 300.0, 0.5);

  aca::PipelineConfig cfg;
  cfg.frame_ms = 10;
  cfg.enable_aec = false;
  cfg.enable_ans = false;
  cfg.enable_agc = false;
  cfg.enable_codec = false;

  const auto out = aca::run_pipeline(in, nullptr, cfg);

  ASSERT_GE(out.samples.size(), in.samples.size());
  for (size_t i = 0; i < in.samples.size(); ++i) {
    ASSERT_EQ(out.samples[i], in.samples[i]) << "diverged at " << i;
  }
}

TEST(CaptureChain, FrameSizeMatchesConfig) {
  aca::PipelineConfig cfg;
  cfg.frame_ms = 20;
  aca::CaptureChain chain(48000, cfg);
  EXPECT_EQ(chain.frame_size(), 960u);
}

TEST(CaptureChain, RejectsZeroFrame) {
  aca::PipelineConfig cfg;
  cfg.frame_ms = 0;
  EXPECT_THROW(aca::CaptureChain(48000, cfg), std::runtime_error);
}

TEST(CaptureChain, InPlaceProcessingIsSafe) {
  aca::PipelineConfig cfg;
  cfg.frame_ms = 10;
  cfg.enable_ans = true;
  cfg.enable_agc = false;

  aca::CaptureChain chain(16000, cfg);
  std::vector<int16_t> buf(chain.frame_size(), 1000);

  // mic and out alias the same memory -- the live callback relies on this.
  EXPECT_NO_THROW(chain.process_frame(buf.data(), nullptr, buf.data()));
}

}  // namespace
