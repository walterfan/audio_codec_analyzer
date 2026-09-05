#include <cstdio>
#include <stdexcept>

#include "aca/cli_args.h"

#ifdef ACA_HAVE_PORTAUDIO
#include "aca/live_session.h"
#endif

namespace aca::cli {

#ifndef ACA_HAVE_PORTAUDIO

namespace {
[[noreturn]] void no_portaudio() {
  throw std::runtime_error(
      "this build has no PortAudio support -- install it "
      "(brew install portaudio) and re-run cmake");
}
}  // namespace

int cmd_live(const Args&) { no_portaudio(); }
int cmd_devices(const Args&) { no_portaudio(); }

#else

int cmd_devices(const Args& args) {
  args.reject_unknown({});
  const auto devices = list_devices();
  std::printf("%-4s %-38s %-12s %3s %3s %8s %s\n", "idx", "name", "api", "in",
              "out", "rate", "default");
  for (const auto& d : devices) {
    std::string def;
    if (d.is_default_input) def += "input ";
    if (d.is_default_output) def += "output";
    std::printf("%-4d %-38.38s %-12.12s %3d %3d %8.0f %s\n", d.index,
                d.name.c_str(), d.api.c_str(), d.max_input_channels,
                d.max_output_channels, d.default_sample_rate, def.c_str());
  }
  return 0;
}

int cmd_live(const Args& args) {
  args.reject_unknown({"seconds", "rate", "frame-ms", "aec", "no-ans",
                       "no-agc", "in-device", "out-device", "no-monitor",
                       "record", "suppress-db", "target-dbfs", "filter-ms"});

  LiveConfig cfg;
  cfg.seconds = args.real("seconds", 10.0);
  cfg.sample_rate = args.integer("rate", 48000);
  cfg.frame_ms = args.integer("frame-ms", 10);
  cfg.input_device = args.integer("in-device", -1);
  cfg.output_device = args.integer("out-device", -1);
  cfg.monitor = !args.flag("no-monitor", false);
  cfg.record_path = args.str("record");

  cfg.pipeline.frame_ms = cfg.frame_ms;
  cfg.pipeline.enable_aec = args.flag("aec", false);
  cfg.pipeline.enable_ans = !args.flag("no-ans", false);
  cfg.pipeline.enable_agc = !args.flag("no-agc", false);
  cfg.pipeline.enable_codec = false;  // no codec in the realtime monitor path
  cfg.pipeline.aec.frame_ms = cfg.frame_ms;
  cfg.pipeline.aec.filter_ms = args.integer("filter-ms", 100);
  cfg.pipeline.ans.frame_ms = cfg.frame_ms;
  cfg.pipeline.ans.suppression_db = args.integer("suppress-db", -25);
  cfg.pipeline.agc.frame_ms = cfg.frame_ms;
  cfg.pipeline.agc.target_level_dbfs =
      static_cast<float>(args.real("target-dbfs", -3.0));

  if (cfg.pipeline.enable_aec && !cfg.monitor) {
    std::fprintf(stderr,
                 "aca: warning: --aec with --no-monitor has nothing to cancel "
                 "(the reference is the monitored playback)\n");
  }
  if (cfg.monitor) {
    std::fprintf(stderr,
                 "aca: monitoring is ON -- use headphones, or the speaker will "
                 "feed back into the mic\n");
  }

  std::printf("live: %d Hz, %d ms frames, stages:%s%s%s, %.1f s\n",
              cfg.sample_rate, cfg.frame_ms,
              cfg.pipeline.enable_aec ? " AEC" : "",
              cfg.pipeline.enable_ans ? " ANS" : "",
              cfg.pipeline.enable_agc ? " AGC" : "", cfg.seconds);

  const LiveStats stats = run_live_session(cfg);

  std::printf("  frames      : %zu\n", stats.frames);
  std::printf("  input rms   : %.1f dBFS\n", stats.input_rms_dbfs);
  std::printf("  output rms  : %.1f dBFS\n", stats.output_rms_dbfs);
  std::printf("  xruns       : %zu underflow, %zu overflow\n",
              stats.input_underflows, stats.output_overflows);
  if (!cfg.record_path.empty()) {
    std::printf("  recorded    : %s\n", cfg.record_path.c_str());
  }
  return 0;
}

#endif

}  // namespace aca::cli
