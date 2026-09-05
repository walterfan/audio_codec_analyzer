#include "aca/frame_stream.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace {

std::string temp_path(const char* name) {
  return (std::filesystem::temp_directory_path() / name).string();
}

TEST(FrameStream, RoundTrip) {
  aca::FrameStream fs;
  fs.sample_rate = 48000;
  fs.channels = 1;
  fs.frame_ms = 20;
  fs.frames = {{1, 2, 3}, {4, 5}, {}, {6}};

  const std::string p = temp_path("aca_frames.bin");
  aca::write_frame_stream(p, fs);
  const auto back = aca::read_frame_stream(p);

  EXPECT_EQ(back.sample_rate, 48000);
  EXPECT_EQ(back.channels, 1);
  EXPECT_EQ(back.frame_ms, 20);
  ASSERT_EQ(back.frames.size(), 4u);
  EXPECT_EQ(back.frames[0], std::vector<uint8_t>({1, 2, 3}));
  EXPECT_TRUE(back.frames[2].empty());  // empty DTX frame must survive
  EXPECT_EQ(back.frames[3], std::vector<uint8_t>({6}));

  std::filesystem::remove(p);
}

TEST(FrameStream, TotalPayloadBytes) {
  aca::FrameStream fs;
  fs.frames = {{1, 2, 3}, {4, 5}};
  EXPECT_EQ(fs.total_payload_bytes(), 5u);
}

TEST(FrameStream, RejectsForeignFile) {
  const std::string p = temp_path("aca_not_frames.bin");
  {
    std::ofstream os(p, std::ios::binary);
    os << "RIFFsomething else entirely";
  }
  EXPECT_THROW(aca::read_frame_stream(p), std::runtime_error);
  std::filesystem::remove(p);
}

TEST(FrameStream, RejectsTruncatedFile) {
  aca::FrameStream fs;
  fs.sample_rate = 48000;
  fs.channels = 1;
  fs.frame_ms = 20;
  fs.frames = {{1, 2, 3, 4, 5, 6, 7, 8}};

  const std::string p = temp_path("aca_trunc.bin");
  aca::write_frame_stream(p, fs);

  // Chop off the tail so the declared payload is missing.
  const auto size = std::filesystem::file_size(p);
  std::filesystem::resize_file(p, size - 4);

  EXPECT_THROW(aca::read_frame_stream(p), std::runtime_error);
  std::filesystem::remove(p);
}

TEST(FrameStream, MissingFileThrows) {
  EXPECT_THROW(aca::read_frame_stream("/nonexistent/aca/none.bin"),
               std::runtime_error);
}

}  // namespace
