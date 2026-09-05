#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace aca {

// A raw Opus packet has no self-delimiting framing, so an encoded file needs a
// container. Rather than pull in libogg, this project uses its own trivial
// length-prefixed format with the magic "ACA1" (files use the .opus-frames
// extension). It is NOT interoperable with .opus/Ogg files on purpose --
// `aca decode` is the only reader.
//
// Layout (all little-endian):
//   magic    char[4]  "ACA1"
//   sample_rate  int32
//   channels     int32
//   frame_ms     int32   (encoder frame size, needed to size the decode buffer)
//   frame_count  int32
//   then frame_count records of:
//     length   uint16
//     payload  uint8[length]
struct FrameStream {
  int sample_rate = 0;
  int channels = 0;
  int frame_ms = 0;
  std::vector<std::vector<uint8_t>> frames;

  size_t total_payload_bytes() const;
};

void write_frame_stream(const std::string& path, const FrameStream& fs);
FrameStream read_frame_stream(const std::string& path);

}  // namespace aca
