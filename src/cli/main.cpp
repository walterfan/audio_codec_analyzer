#include <cstdio>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

#include "aca/cli_args.h"

namespace {

void print_usage() {
  std::puts(R"(aca -- audio codec & voice-processing analyzer

usage: aca <command> [options]

codec (libopus):
  encode    <in.wav> -o <out.opus-frames> [--bitrate 24000] [--frame-ms 20]
            [--complexity 10] [--mode voip|audio|lowdelay] [--vbr true]
            [--fec false] [--loss-pct 0] [--dtx false]
  decode    <in.opus-frames> -o <out.wav> [--drop-every-nth 0]

voice processing (speexdsp):
  aec       --mic <mic.wav> --ref <speaker.wav> -o <out.wav>
            [--frame-ms 10] [--filter-ms 100] [--no-preprocess]
  ans       <in.wav> -o <out.wav> [--frame-ms 10] [--suppress-db -25]
            [--dereverb false]
  agc       <in.wav> -o <out.wav> [--frame-ms 10] [--target-dbfs -3]
            [--max-gain-db 30]

combined:
  pipeline  --mic <mic.wav> [--ref <speaker.wav>] -o <out.wav>
            [--aec] [--no-ans] [--no-agc] [--codec] [--frame-ms 10]
            [--bitrate 24000]
  analyze   <file.wav> [<file2.wav> ...]

live (portaudio):
  devices
  live      [--seconds 10] [--rate 48000] [--frame-ms 10] [--aec]
            [--no-ans] [--no-agc] [--in-device N] [--out-device N]
            [--no-monitor] [--record out.wav]

Order of the capture chain is fixed: AEC -> ANS -> AGC -> Opus.
Run `aca <command> --help` is not supported; see the list above.)");
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    print_usage();
    return 2;
  }

  const std::string cmd = argv[1];
  if (cmd == "-h" || cmd == "--help" || cmd == "help") {
    print_usage();
    return 0;
  }

  try {
    aca::cli::Args args(argc, argv, 2);

    if (cmd == "encode")   return aca::cli::cmd_encode(args);
    if (cmd == "decode")   return aca::cli::cmd_decode(args);
    if (cmd == "aec")      return aca::cli::cmd_aec(args);
    if (cmd == "ans")      return aca::cli::cmd_ans(args);
    if (cmd == "agc")      return aca::cli::cmd_agc(args);
    if (cmd == "pipeline") return aca::cli::cmd_pipeline(args);
    if (cmd == "analyze")  return aca::cli::cmd_analyze(args);
    if (cmd == "live")     return aca::cli::cmd_live(args);
    if (cmd == "devices")  return aca::cli::cmd_devices(args);

    std::fprintf(stderr, "aca: unknown command '%s'\n\n", cmd.c_str());
    print_usage();
    return 2;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "aca: error: %s\n", e.what());
    return 1;
  }
}
