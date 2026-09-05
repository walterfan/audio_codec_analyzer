#include <cstdio>
#include <stdexcept>

#include "aca/metrics.h"
#include "aca/wav_io.h"
#include "aca/cli_args.h"

namespace aca::cli {

int cmd_analyze(const Args& args) {
  args.reject_unknown({});
  if (args.positional().empty()) {
    throw std::runtime_error("analyze: give at least one audio file");
  }

  for (const auto& path : args.positional()) {
    const AudioBuffer buf = read_audio(path);
    const LevelReport lr = analyze_levels(buf);
    std::printf("%s\n", path.c_str());
    std::printf("  format      : %d Hz, %d ch, %.3f s (%zu frames)\n",
                buf.sample_rate, buf.channels, lr.duration_seconds,
                buf.frames());
    std::printf("  rms         : %.1f dBFS\n", lr.rms_dbfs);
    std::printf("  peak        : %.1f dBFS\n", lr.peak_dbfs);
    std::printf("  clipped     : %zu samples\n", lr.clipped_samples);
  }
  return 0;
}

}  // namespace aca::cli
