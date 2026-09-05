#include <cstdio>
#include <stdexcept>
#include <string>

#include "aca/echo_canceller.h"
#include "aca/metrics.h"
#include "aca/noise_suppressor.h"
#include "aca/pipeline.h"
#include "aca/wav_io.h"
#include "aca/cli_args.h"

namespace aca::cli {
namespace {

std::string require_output(const Args& args) {
  const std::string out = args.str("out");
  if (out.empty()) throw std::runtime_error("missing output path (-o)");
  return out;
}

std::string require_input(const Args& args) {
  if (args.positional().empty()) throw std::runtime_error("missing input file");
  return args.positional().front();
}

void print_levels(const char* label, const LevelReport& lr) {
  std::printf("  %-12s: rms %7.1f dBFS, peak %7.1f dBFS", label, lr.rms_dbfs,
              lr.peak_dbfs);
  if (lr.clipped_samples > 0) {
    std::printf(", %zu clipped", lr.clipped_samples);
  }
  std::printf("\n");
}

}  // namespace

int cmd_aec(const Args& args) {
  args.reject_unknown({"mic", "ref", "out", "frame-ms", "filter-ms",
                       "no-preprocess"});

  const std::string mic_path = args.str("mic");
  const std::string ref_path = args.str("ref");
  if (mic_path.empty()) throw std::runtime_error("--mic is required");
  if (ref_path.empty()) {
    throw std::runtime_error(
        "--ref is required: AEC needs the far-end/speaker signal, not just "
        "the mic");
  }
  const std::string out_path = require_output(args);

  AecConfig cfg;
  cfg.frame_ms = args.integer("frame-ms", 10);
  cfg.filter_ms = args.integer("filter-ms", 100);
  cfg.with_preprocess = !args.flag("no-preprocess", false);

  AudioBuffer mic = read_audio(mic_path);
  AudioBuffer ref = read_audio(ref_path);
  AudioBuffer out = cancel_echo(mic, ref, cfg);
  write_wav(out_path, out);

  AudioBuffer mic_mono = mic;
  mic_mono.to_mono();

  std::printf("AEC %s (ref %s) -> %s\n", mic_path.c_str(), ref_path.c_str(),
              out_path.c_str());
  std::printf("  frame       : %d ms, tail filter %d ms\n", cfg.frame_ms,
              cfg.filter_ms);
  print_levels("mic in", analyze_levels(mic));
  print_levels("out", analyze_levels(out));
  std::printf("  ERLE        : %.1f dB\n",
              erle_db(mic_mono.samples, out.samples));
  return 0;
}

int cmd_ans(const Args& args) {
  args.reject_unknown({"out", "frame-ms", "suppress-db", "dereverb"});

  const std::string in_path = require_input(args);
  const std::string out_path = require_output(args);

  AnsConfig cfg;
  cfg.frame_ms = args.integer("frame-ms", 10);
  cfg.suppression_db = args.integer("suppress-db", -25);
  cfg.dereverb = args.flag("dereverb", false);

  AudioBuffer in = read_audio(in_path);
  AudioBuffer out = suppress_noise(in, cfg);
  write_wav(out_path, out);

  std::printf("ANS %s -> %s\n", in_path.c_str(), out_path.c_str());
  std::printf("  frame       : %d ms, suppression %d dB\n", cfg.frame_ms,
              cfg.suppression_db);
  print_levels("in", analyze_levels(in));
  print_levels("out", analyze_levels(out));
  return 0;
}

int cmd_agc(const Args& args) {
  args.reject_unknown({"out", "frame-ms", "target-dbfs", "max-gain-db"});

  const std::string in_path = require_input(args);
  const std::string out_path = require_output(args);

  AgcConfig cfg;
  cfg.frame_ms = args.integer("frame-ms", 10);
  cfg.target_level_dbfs = static_cast<float>(args.real("target-dbfs", -3.0));
  cfg.max_gain_db = args.integer("max-gain-db", 30);

  AudioBuffer in = read_audio(in_path);
  AudioBuffer out = control_gain(in, cfg);
  write_wav(out_path, out);

  std::printf("AGC %s -> %s\n", in_path.c_str(), out_path.c_str());
  std::printf("  frame       : %d ms, target %.1f dBFS, max gain %d dB\n",
              cfg.frame_ms, cfg.target_level_dbfs, cfg.max_gain_db);
  print_levels("in", analyze_levels(in));
  print_levels("out", analyze_levels(out));
  return 0;
}

int cmd_pipeline(const Args& args) {
  args.reject_unknown({"mic", "ref", "out", "aec", "no-ans", "no-agc", "codec",
                       "frame-ms", "bitrate", "suppress-db", "target-dbfs",
                       "filter-ms"});

  const std::string mic_path = args.str("mic");
  if (mic_path.empty()) throw std::runtime_error("--mic is required");
  const std::string out_path = require_output(args);
  const std::string ref_path = args.str("ref");

  PipelineConfig cfg;
  cfg.frame_ms = args.integer("frame-ms", 10);
  cfg.enable_aec = args.flag("aec", false);
  cfg.enable_ans = !args.flag("no-ans", false);
  cfg.enable_agc = !args.flag("no-agc", false);
  cfg.enable_codec = args.flag("codec", false);
  cfg.aec.frame_ms = cfg.frame_ms;
  cfg.aec.filter_ms = args.integer("filter-ms", 100);
  cfg.ans.frame_ms = cfg.frame_ms;
  cfg.ans.suppression_db = args.integer("suppress-db", -25);
  cfg.agc.frame_ms = cfg.frame_ms;
  cfg.agc.target_level_dbfs = static_cast<float>(args.real("target-dbfs", -3.0));
  cfg.opus.bitrate_bps = args.integer("bitrate", 24000);
  // The pipeline's Opus leg needs a legal frame size; 10 ms is legal, but a
  // 15 ms DSP frame would not be, so pick the closest valid value.
  cfg.opus.frame_ms = is_valid_opus_frame_ms(cfg.frame_ms) ? cfg.frame_ms : 20;

  if (cfg.enable_aec && ref_path.empty()) {
    throw std::runtime_error("--aec requires --ref <speaker.wav>");
  }

  AudioBuffer mic = read_audio(mic_path);
  AudioBuffer ref;
  if (cfg.enable_aec) ref = read_audio(ref_path);

  PipelineReport report;
  AudioBuffer out =
      run_pipeline(mic, cfg.enable_aec ? &ref : nullptr, cfg, &report);
  write_wav(out_path, out);

  std::printf("pipeline %s -> %s\n", mic_path.c_str(), out_path.c_str());
  std::printf("  stages      : %s%s%s%s\n",
              cfg.enable_aec ? "AEC " : "",
              cfg.enable_ans ? "ANS " : "",
              cfg.enable_agc ? "AGC " : "",
              cfg.enable_codec ? "Opus" : "");
  std::printf("  frames      : %zu x %d ms\n", report.frames_processed,
              cfg.frame_ms);
  print_levels("in", report.input);
  print_levels("out", report.output);
  if (cfg.enable_aec) {
    std::printf("  ERLE        : %.1f dB\n", report.erle_db);
  }
  if (report.codec) {
    std::printf("  opus        : %zu bytes, %.0f bps, %.1f:1\n",
                report.codec->encoded_bytes,
                report.codec->effective_bitrate_bps(),
                report.codec->compression_ratio());
  }
  return 0;
}

}  // namespace aca::cli
