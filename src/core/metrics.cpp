#include "aca/metrics.h"

#include <algorithm>
#include <cmath>

namespace aca {
namespace {

constexpr double kFullScale = 32768.0;
constexpr double kSilenceFloorDb = -120.0;

double to_dbfs(double linear) {
  if (linear <= 0.0) return kSilenceFloorDb;
  return std::max(kSilenceFloorDb, 20.0 * std::log10(linear));
}

}  // namespace

double rms_dbfs(const int16_t* data, size_t count) {
  if (count == 0) return kSilenceFloorDb;
  double acc = 0.0;
  for (size_t i = 0; i < count; ++i) {
    const double s = data[i] / kFullScale;
    acc += s * s;
  }
  return to_dbfs(std::sqrt(acc / static_cast<double>(count)));
}

double rms_dbfs(const Pcm16& v) { return rms_dbfs(v.data(), v.size()); }

double peak_dbfs(const int16_t* data, size_t count) {
  int32_t peak = 0;
  for (size_t i = 0; i < count; ++i) {
    peak = std::max(peak, std::abs(static_cast<int32_t>(data[i])));
  }
  return to_dbfs(peak / kFullScale);
}

double peak_dbfs(const Pcm16& v) { return peak_dbfs(v.data(), v.size()); }

double snr_db(const Pcm16& reference, const Pcm16& processed) {
  const size_t n = std::min(reference.size(), processed.size());
  if (n == 0) return 0.0;

  double sig = 0.0;
  double err = 0.0;
  for (size_t i = 0; i < n; ++i) {
    const double r = reference[i] / kFullScale;
    const double d = (processed[i] - reference[i]) / kFullScale;
    sig += r * r;
    err += d * d;
  }
  if (err <= 0.0) return -kSilenceFloorDb;  // identical signals
  if (sig <= 0.0) return kSilenceFloorDb;
  return 10.0 * std::log10(sig / err);
}

double erle_db(const Pcm16& before, const Pcm16& after) {
  const size_t n = std::min(before.size(), after.size());
  if (n == 0) return 0.0;

  double e_before = 0.0;
  double e_after = 0.0;
  for (size_t i = 0; i < n; ++i) {
    const double b = before[i] / kFullScale;
    const double a = after[i] / kFullScale;
    e_before += b * b;
    e_after += a * a;
  }
  if (e_before <= 0.0) return 0.0;
  if (e_after <= 0.0) return -kSilenceFloorDb;
  return 10.0 * std::log10(e_before / e_after);
}

LevelReport analyze_levels(const AudioBuffer& buf) {
  LevelReport r;
  r.rms_dbfs = rms_dbfs(buf.samples);
  r.peak_dbfs = peak_dbfs(buf.samples);
  r.duration_seconds = buf.duration_seconds();
  for (int16_t s : buf.samples) {
    if (s >= 32767 || s <= -32768) ++r.clipped_samples;
  }
  return r;
}

}  // namespace aca
