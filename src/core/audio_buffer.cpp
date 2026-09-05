#include "aca/audio_buffer.h"

namespace aca {

void AudioBuffer::to_mono() {
  if (channels <= 1) {
    channels = 1;
    return;
  }
  const size_t n = frames();
  Pcm16 mono(n);
  for (size_t i = 0; i < n; ++i) {
    int32_t acc = 0;
    for (int c = 0; c < channels; ++c) {
      acc += samples[i * static_cast<size_t>(channels) + static_cast<size_t>(c)];
    }
    mono[i] = static_cast<int16_t>(acc / channels);
  }
  samples = std::move(mono);
  channels = 1;
}

}  // namespace aca
