#include "aca/frame_stream.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace aca {
namespace {

constexpr char kMagic[4] = {'A', 'C', 'A', '1'};

void put_i32(std::ostream& os, int32_t v) {
  unsigned char b[4] = {
      static_cast<unsigned char>(v & 0xFF),
      static_cast<unsigned char>((v >> 8) & 0xFF),
      static_cast<unsigned char>((v >> 16) & 0xFF),
      static_cast<unsigned char>((v >> 24) & 0xFF)};
  os.write(reinterpret_cast<const char*>(b), 4);
}

int32_t get_i32(std::istream& is) {
  unsigned char b[4];
  is.read(reinterpret_cast<char*>(b), 4);
  if (!is) throw std::runtime_error("frame stream: truncated header");
  return static_cast<int32_t>(b[0] | (b[1] << 8) | (b[2] << 16) |
                              (static_cast<uint32_t>(b[3]) << 24));
}

}  // namespace

size_t FrameStream::total_payload_bytes() const {
  size_t n = 0;
  for (const auto& f : frames) n += f.size();
  return n;
}

void write_frame_stream(const std::string& path, const FrameStream& fs) {
  std::ofstream os(path, std::ios::binary);
  if (!os) throw std::runtime_error("cannot write '" + path + "'");

  os.write(kMagic, 4);
  put_i32(os, fs.sample_rate);
  put_i32(os, fs.channels);
  put_i32(os, fs.frame_ms);
  put_i32(os, static_cast<int32_t>(fs.frames.size()));

  for (const auto& f : fs.frames) {
    if (f.size() > 0xFFFF) {
      throw std::runtime_error("frame stream: packet larger than 65535 bytes");
    }
    const unsigned char len[2] = {
        static_cast<unsigned char>(f.size() & 0xFF),
        static_cast<unsigned char>((f.size() >> 8) & 0xFF)};
    os.write(reinterpret_cast<const char*>(len), 2);
    if (!f.empty()) {
      os.write(reinterpret_cast<const char*>(f.data()),
               static_cast<std::streamsize>(f.size()));
    }
  }
  if (!os) throw std::runtime_error("frame stream: write failed for '" + path + "'");
}

FrameStream read_frame_stream(const std::string& path) {
  std::ifstream is(path, std::ios::binary);
  if (!is) throw std::runtime_error("cannot open '" + path + "'");

  char magic[4];
  is.read(magic, 4);
  if (!is || std::memcmp(magic, kMagic, 4) != 0) {
    throw std::runtime_error(
        "'" + path +
        "' is not an ACA1 frame stream (produced by `aca encode`)");
  }

  FrameStream fs;
  fs.sample_rate = get_i32(is);
  fs.channels = get_i32(is);
  fs.frame_ms = get_i32(is);
  const int32_t count = get_i32(is);
  if (count < 0) throw std::runtime_error("frame stream: negative frame count");

  fs.frames.reserve(static_cast<size_t>(count));
  for (int32_t i = 0; i < count; ++i) {
    unsigned char len[2];
    is.read(reinterpret_cast<char*>(len), 2);
    if (!is) throw std::runtime_error("frame stream: truncated at frame " +
                                      std::to_string(i));
    const size_t n = static_cast<size_t>(len[0] | (len[1] << 8));
    std::vector<uint8_t> payload(n);
    if (n > 0) {
      is.read(reinterpret_cast<char*>(payload.data()),
              static_cast<std::streamsize>(n));
      if (!is) throw std::runtime_error("frame stream: truncated payload at frame " +
                                        std::to_string(i));
    }
    fs.frames.push_back(std::move(payload));
  }
  return fs;
}

}  // namespace aca
