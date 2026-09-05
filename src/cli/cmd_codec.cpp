#include <cstdio>
#include <stdexcept>
#include <string>

#include "aca/frame_stream.h"
#include "aca/metrics.h"
#include "aca/opus_codec.h"
#include "aca/wav_io.h"
#include "aca/cli_args.h"

namespace aca::cli {
namespace {

std::string require_input(const Args& args) {
  if (args.positional().empty()) {
    throw std::runtime_error("missing input file");
  }
  return args.positional().front();
}

std::string require_output(const Args& args) {
  const std::string out = args.str("out");
  if (out.empty()) throw std::runtime_error("missing output path (-o)");
  return out;
}

OpusAppMode parse_mode(const std::string& s) {
  if (s == "voip") return OpusAppMode::Voip;
  if (s == "audio") return OpusAppMode::Audio;
  if (s == "lowdelay") return OpusAppMode::LowDelay;
  throw std::runtime_error("--mode must be voip, audio or lowdelay, got '" +
                           s + "'");
}

}  // namespace

int cmd_encode(const Args& args) {
  args.reject_unknown({"out", "bitrate", "frame-ms", "complexity", "mode",
                       "vbr", "fec", "loss-pct", "dtx"});

  const std::string in_path = require_input(args);
  const std::string out_path = require_output(args);

  OpusEncodeConfig cfg;
  cfg.bitrate_bps = args.integer("bitrate", 24000);
  cfg.frame_ms = args.integer("frame-ms", 20);
  cfg.complexity = args.integer("complexity", 10);
  cfg.mode = parse_mode(args.str("mode", "voip"));
  cfg.vbr = args.flag("vbr", true);
  cfg.fec = args.flag("fec", false);
  cfg.expected_loss_pct = args.integer("loss-pct", 0);
  cfg.dtx = args.flag("dtx", false);

  AudioBuffer in = read_audio(in_path);
  if (in.empty()) throw std::runtime_error("input '" + in_path + "' is empty");

  // Opus only takes 8/12/16/24/48 kHz -- fail loudly rather than produce junk.
  validate_opus_sample_rate(in.sample_rate);
  if (in.channels > 2) {
    throw std::runtime_error("Opus supports at most 2 channels, input has " +
                             std::to_string(in.channels));
  }

  EncodeStats stats;
  FrameStream fs = encode_to_frames(in, cfg, &stats);
  write_frame_stream(out_path, fs);

  std::printf("encoded %s -> %s\n", in_path.c_str(), out_path.c_str());
  std::printf("  input       : %d Hz, %d ch, %.3f s\n", in.sample_rate,
              in.channels, stats.duration_seconds);
  std::printf("  frames      : %zu x %d ms\n", stats.frames, cfg.frame_ms);
  std::printf("  pcm bytes   : %zu\n", stats.pcm_bytes);
  std::printf("  opus bytes  : %zu\n", stats.encoded_bytes);
  std::printf("  bitrate     : %.0f bps (target %d)\n",
              stats.effective_bitrate_bps(), cfg.bitrate_bps);
  std::printf("  compression : %.1f:1\n", stats.compression_ratio());
  return 0;
}

int cmd_decode(const Args& args) {
  args.reject_unknown({"out", "drop-every-nth"});

  const std::string in_path = require_input(args);
  const std::string out_path = require_output(args);
  const int drop = args.integer("drop-every-nth", 0);

  FrameStream fs = read_frame_stream(in_path);
  AudioBuffer out = decode_from_frames(fs, drop);
  write_wav(out_path, out);

  std::printf("decoded %s -> %s\n", in_path.c_str(), out_path.c_str());
  std::printf("  frames      : %zu x %d ms\n", fs.frames.size(), fs.frame_ms);
  std::printf("  output      : %d Hz, %d ch, %.3f s\n", out.sample_rate,
              out.channels, out.duration_seconds());
  if (drop > 0) {
    std::printf("  packet loss : every %dth frame dropped (Opus PLC applied)\n",
                drop);
  }
  const LevelReport lr = analyze_levels(out);
  std::printf("  level       : rms %.1f dBFS, peak %.1f dBFS\n", lr.rms_dbfs,
              lr.peak_dbfs);
  return 0;
}

}  // namespace aca::cli
