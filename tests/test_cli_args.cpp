#include "aca/cli_args.h"

#include <gtest/gtest.h>

#include <vector>

namespace {

// Builds an Args the way main() does: argv[0] is the program, argv[1] the
// command, and parsing starts at index 2.
aca::cli::Args parse(std::vector<const char*> tokens) {
  std::vector<char*> argv;
  argv.push_back(const_cast<char*>("aca"));
  argv.push_back(const_cast<char*>("cmd"));
  for (const char* t : tokens) argv.push_back(const_cast<char*>(t));
  return aca::cli::Args(static_cast<int>(argv.size()), argv.data(), 2);
}

TEST(CliArgs, ParsesKeyValuePairs) {
  const auto a = parse({"--bitrate", "24000", "--mode", "audio"});
  EXPECT_EQ(a.integer("bitrate", 0), 24000);
  EXPECT_EQ(a.str("mode"), "audio");
}

TEST(CliArgs, ParsesEqualsForm) {
  const auto a = parse({"--bitrate=32000"});
  EXPECT_EQ(a.integer("bitrate", 0), 32000);
}

// Regression: "--suppress-db -25" used to parse as the bare flag
// "--suppress-db true" because the value starts with '-', which then failed
// with "expects an integer, got 'true'".
TEST(CliArgs, ParsesNegativeIntegerValue) {
  const auto a = parse({"--suppress-db", "-25"});
  EXPECT_EQ(a.integer("suppress-db", 0), -25);
}

TEST(CliArgs, ParsesNegativeRealValue) {
  const auto a = parse({"--target-dbfs", "-3.5"});
  EXPECT_DOUBLE_EQ(a.real("target-dbfs", 0.0), -3.5);
}

TEST(CliArgs, NegativeValueViaEqualsAlsoWorks) {
  const auto a = parse({"--target-dbfs=-3.5"});
  EXPECT_DOUBLE_EQ(a.real("target-dbfs", 0.0), -3.5);
}

// The fix must not make a bare flag swallow the following option.
TEST(CliArgs, BareFlagFollowedByOptionStaysBare) {
  const auto a = parse({"--dereverb", "--frame-ms", "10"});
  EXPECT_TRUE(a.flag("dereverb", false));
  EXPECT_EQ(a.integer("frame-ms", 0), 10);
}

TEST(CliArgs, TrailingBareFlagIsTrue) {
  const auto a = parse({"--aec"});
  EXPECT_TRUE(a.flag("aec", false));
}

TEST(CliArgs, ShortOutputOption) {
  const auto a = parse({"-o", "result.wav"});
  EXPECT_EQ(a.str("out"), "result.wav");
}

TEST(CliArgs, PositionalArguments) {
  const auto a = parse({"in.wav", "-o", "out.wav", "other.wav"});
  ASSERT_EQ(a.positional().size(), 2u);
  EXPECT_EQ(a.positional()[0], "in.wav");
  EXPECT_EQ(a.positional()[1], "other.wav");
}

TEST(CliArgs, DefaultsWhenAbsent) {
  const auto a = parse({});
  EXPECT_EQ(a.integer("bitrate", 24000), 24000);
  EXPECT_EQ(a.str("mode", "voip"), "voip");
  EXPECT_FALSE(a.has("bitrate"));
}

TEST(CliArgs, BooleanSpellings) {
  const auto a = parse({"--a", "true", "--b", "false", "--c", "1", "--d", "no"});
  EXPECT_TRUE(a.flag("a", false));
  EXPECT_FALSE(a.flag("b", true));
  EXPECT_TRUE(a.flag("c", false));
  EXPECT_FALSE(a.flag("d", true));
}

TEST(CliArgs, RejectsNonNumericInteger) {
  const auto a = parse({"--bitrate", "abc"});
  EXPECT_THROW(a.integer("bitrate", 0), std::runtime_error);
}

TEST(CliArgs, RejectsNonBooleanFlag) {
  const auto a = parse({"--vbr", "maybe"});
  EXPECT_THROW(a.flag("vbr", false), std::runtime_error);
}

TEST(CliArgs, RejectUnknownCatchesTypos) {
  const auto a = parse({"--frame-size", "10"});
  EXPECT_THROW(a.reject_unknown({"frame-ms"}), std::runtime_error);
}

TEST(CliArgs, RejectUnknownAcceptsAllowedFlags) {
  const auto a = parse({"--frame-ms", "10", "-o", "x.wav"});
  EXPECT_NO_THROW(a.reject_unknown({"frame-ms", "out"}));
}

}  // namespace
