#pragma once

#include <string>

#include "aca/audio_buffer.h"

namespace aca {

// Reads any format libsndfile understands (wav/flac/aiff/...) and converts to
// interleaved int16. Throws std::runtime_error on failure.
AudioBuffer read_audio(const std::string& path);

// Always writes 16-bit PCM WAV. Throws std::runtime_error on failure.
void write_wav(const std::string& path, const AudioBuffer& buf);

}  // namespace aca
