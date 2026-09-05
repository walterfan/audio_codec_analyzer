#include "aca/live_session.h"

#include <portaudio.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <string>
#include <cmath>
#include <thread>
#include <vector>

#include "aca/metrics.h"
#include "aca/wav_io.h"

namespace aca {
namespace {

void pa_check(PaError err, const char* what) {
  if (err != paNoError) {
    throw std::runtime_error(std::string(what) + ": " + Pa_GetErrorText(err));
  }
}

// RAII around Pa_Initialize/Pa_Terminate. PortAudio refcounts these, so
// nesting is safe.
struct PaGuard {
  PaGuard() { pa_check(Pa_Initialize(), "Pa_Initialize"); }
  ~PaGuard() { Pa_Terminate(); }
  PaGuard(const PaGuard&) = delete;
  PaGuard& operator=(const PaGuard&) = delete;
};

struct CallbackState {
  CaptureChain* chain = nullptr;
  size_t frame_size = 0;
  bool monitor = true;

  // The far-end reference for AEC. In a loopback demo the only thing coming
  // out of the speaker is our own previous output frame, so we feed that back
  // as the reference, delayed by one frame to approximate device latency.
  std::vector<int16_t> last_playback;

  std::vector<int16_t> recorded;
  bool record = false;

  std::atomic<size_t> frames{0};
  std::atomic<size_t> underflows{0};
  std::atomic<size_t> overflows{0};
  double in_energy = 0.0;
  double out_energy = 0.0;
  size_t energy_count = 0;
};

int audio_callback(const void* input, void* output, unsigned long frame_count,
                   const PaStreamCallbackTimeInfo*,
                   PaStreamCallbackFlags status_flags, void* user_data) {
  auto* st = static_cast<CallbackState*>(user_data);
  auto* in = static_cast<const int16_t*>(input);
  auto* out = static_cast<int16_t*>(output);

  if (status_flags & paInputUnderflow) st->underflows.fetch_add(1);
  if (status_flags & paOutputOverflow) st->overflows.fetch_add(1);

  if (!out) return paContinue;
  if (!in) {
    std::memset(out, 0, frame_count * sizeof(int16_t));
    return paContinue;
  }

  // PortAudio is configured with framesPerBuffer == frame_size, so this should
  // always match; bail out safely rather than overrun the chain's buffers.
  if (frame_count != st->frame_size) {
    std::memset(out, 0, frame_count * sizeof(int16_t));
    return paContinue;
  }

  st->chain->process_frame(in, st->last_playback.data(), out);

  for (unsigned long i = 0; i < frame_count; ++i) {
    const double a = in[i] / 32768.0;
    const double b = out[i] / 32768.0;
    st->in_energy += a * a;
    st->out_energy += b * b;
  }
  st->energy_count += frame_count;

  if (st->record) {
    st->recorded.insert(st->recorded.end(), out, out + frame_count);
  }

  // Remember what we are about to play, for the next frame's AEC reference.
  std::memcpy(st->last_playback.data(), out, frame_count * sizeof(int16_t));

  if (!st->monitor) {
    std::memset(out, 0, frame_count * sizeof(int16_t));
  }

  st->frames.fetch_add(1);
  return paContinue;
}

}  // namespace

bool live_audio_available() { return true; }

std::vector<DeviceInfo> list_devices() {
  PaGuard guard;
  std::vector<DeviceInfo> devices;

  const int count = Pa_GetDeviceCount();
  if (count < 0) pa_check(count, "Pa_GetDeviceCount");

  const int def_in = Pa_GetDefaultInputDevice();
  const int def_out = Pa_GetDefaultOutputDevice();

  for (int i = 0; i < count; ++i) {
    const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
    if (!info) continue;
    const PaHostApiInfo* api = Pa_GetHostApiInfo(info->hostApi);

    DeviceInfo d;
    d.index = i;
    d.name = info->name ? info->name : "(unnamed)";
    d.api = api && api->name ? api->name : "(unknown)";
    d.max_input_channels = info->maxInputChannels;
    d.max_output_channels = info->maxOutputChannels;
    d.default_sample_rate = info->defaultSampleRate;
    d.is_default_input = (i == def_in);
    d.is_default_output = (i == def_out);
    devices.push_back(std::move(d));
  }
  return devices;
}

LiveStats run_live_session(const LiveConfig& cfg) {
  PaGuard guard;

  PipelineConfig pcfg = cfg.pipeline;
  pcfg.frame_ms = cfg.frame_ms;

  CaptureChain chain(cfg.sample_rate, pcfg);
  const size_t frame_size = chain.frame_size();

  CallbackState state;
  state.chain = &chain;
  state.frame_size = frame_size;
  state.monitor = cfg.monitor;
  state.last_playback.assign(frame_size, 0);
  state.record = !cfg.record_path.empty();

  const int in_dev =
      cfg.input_device >= 0 ? cfg.input_device : Pa_GetDefaultInputDevice();
  const int out_dev =
      cfg.output_device >= 0 ? cfg.output_device : Pa_GetDefaultOutputDevice();
  if (in_dev == paNoDevice) throw std::runtime_error("no input device available");
  if (out_dev == paNoDevice) throw std::runtime_error("no output device available");

  // Mono in / mono out keeps the frame layout identical to CaptureChain's.
  PaStreamParameters in_params{};
  in_params.device = in_dev;
  in_params.channelCount = 1;
  in_params.sampleFormat = paInt16;
  in_params.suggestedLatency = Pa_GetDeviceInfo(in_dev)->defaultLowInputLatency;

  PaStreamParameters out_params{};
  out_params.device = out_dev;
  out_params.channelCount = 1;
  out_params.sampleFormat = paInt16;
  out_params.suggestedLatency =
      Pa_GetDeviceInfo(out_dev)->defaultLowOutputLatency;

  PaStream* stream = nullptr;
  pa_check(Pa_OpenStream(&stream, &in_params, &out_params, cfg.sample_rate,
                         static_cast<unsigned long>(frame_size),
                         paClipOff, audio_callback, &state),
           "Pa_OpenStream");

  pa_check(Pa_StartStream(stream), "Pa_StartStream");

  if (cfg.seconds > 0) {
    Pa_Sleep(static_cast<long>(cfg.seconds * 1000));
  } else {
    while (Pa_IsStreamActive(stream) == 1) {
      Pa_Sleep(200);
    }
  }

  pa_check(Pa_StopStream(stream), "Pa_StopStream");
  pa_check(Pa_CloseStream(stream), "Pa_CloseStream");

  LiveStats stats;
  stats.frames = state.frames.load();
  stats.input_underflows = state.underflows.load();
  stats.output_overflows = state.overflows.load();
  if (state.energy_count > 0) {
    const double n = static_cast<double>(state.energy_count);
    stats.input_rms_dbfs = 20.0 * std::log10(std::sqrt(state.in_energy / n) + 1e-12);
    stats.output_rms_dbfs = 20.0 * std::log10(std::sqrt(state.out_energy / n) + 1e-12);
  }

  if (state.record) {
    AudioBuffer buf;
    buf.sample_rate = cfg.sample_rate;
    buf.channels = 1;
    buf.samples = std::move(state.recorded);
    write_wav(cfg.record_path, buf);
  }
  return stats;
}

}  // namespace aca
