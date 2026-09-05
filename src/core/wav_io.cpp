#include "aca/wav_io.h"

#include <sndfile.h>

#include <stdexcept>
#include <string>

namespace aca {

AudioBuffer read_audio(const std::string& path) {
  SF_INFO info{};
  SNDFILE* f = sf_open(path.c_str(), SFM_READ, &info);
  if (!f) {
    throw std::runtime_error("cannot open audio file '" + path + "': " +
                             sf_strerror(nullptr));
  }

  AudioBuffer buf;
  buf.sample_rate = info.samplerate;
  buf.channels = info.channels;
  buf.samples.resize(static_cast<size_t>(info.frames) *
                     static_cast<size_t>(info.channels));

  const sf_count_t got = sf_readf_short(f, buf.samples.data(), info.frames);
  sf_close(f);

  if (got != info.frames) {
    buf.samples.resize(static_cast<size_t>(got) *
                       static_cast<size_t>(info.channels));
  }
  return buf;
}

void write_wav(const std::string& path, const AudioBuffer& buf) {
  if (buf.channels <= 0 || buf.sample_rate <= 0) {
    throw std::runtime_error("write_wav: invalid channels/sample_rate for '" +
                             path + "'");
  }

  SF_INFO info{};
  info.samplerate = buf.sample_rate;
  info.channels = buf.channels;
  info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;

  SNDFILE* f = sf_open(path.c_str(), SFM_WRITE, &info);
  if (!f) {
    throw std::runtime_error("cannot write '" + path + "': " +
                             sf_strerror(nullptr));
  }

  const sf_count_t n = static_cast<sf_count_t>(buf.frames());
  const sf_count_t wrote = sf_writef_short(f, buf.samples.data(), n);
  sf_close(f);

  if (wrote != n) {
    throw std::runtime_error("short write to '" + path + "'");
  }
}

}  // namespace aca
