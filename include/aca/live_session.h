#pragma once

#include <string>
#include <vector>

#include "aca/pipeline.h"

namespace aca {

struct DeviceInfo {
  int index = 0;
  std::string name;
  std::string api;
  int max_input_channels = 0;
  int max_output_channels = 0;
  double default_sample_rate = 0.0;
  bool is_default_input = false;
  bool is_default_output = false;
};

// Throws std::runtime_error if the build has no PortAudio.
std::vector<DeviceInfo> list_devices();

struct LiveConfig {
  int input_device = -1;   // -1 = system default
  int output_device = -1;
  int sample_rate = 48000;
  int frame_ms = 10;
  double seconds = 10.0;   // <= 0 means run until Ctrl-C
  bool monitor = true;     // play the processed mic back out (loopback demo)
  std::string record_path; // optional WAV dump of the processed capture
  PipelineConfig pipeline{};
};

struct LiveStats {
  size_t frames = 0;
  size_t input_underflows = 0;
  size_t output_overflows = 0;
  double input_rms_dbfs = 0.0;
  double output_rms_dbfs = 0.0;
};

// Opens a duplex stream and runs `CaptureChain` in the audio callback.
// The far-end reference for AEC is whatever we most recently played out, which
// is exactly the monitor signal -- so `monitor` must be on for AEC to do
// anything meaningful in the live demo.
LiveStats run_live_session(const LiveConfig& cfg);

bool live_audio_available();

}  // namespace aca
