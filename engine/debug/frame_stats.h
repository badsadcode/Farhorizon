#pragma once
// Rolling frame-time telemetry plus a full-run recorder for benchmark CSV/JSON export.
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace fh {

struct FrameSample {
  double cpuMs = 0;      // wall time of the whole frame on the main thread
  double gpuMs = -1;     // GPU time from timestamp queries, -1 if unavailable
  std::uint32_t drawCalls = 0;
  std::uint64_t triangles = 0;
};

struct FrameSummary {
  std::size_t frames = 0;
  double avgCpuMs = 0, minCpuMs = 0, maxCpuMs = 0, p99CpuMs = 0;
  double avgGpuMs = -1, maxGpuMs = -1;
  double avgFps = 0;
};

class FrameStats {
 public:
  explicit FrameStats(std::size_t window = 120) : window_(window) {}

  void push(const FrameSample& s);
  void setRecording(bool on) { recording_ = on; }
  // Statistics over the rolling window (for the overlay) or the full recording (benchmarks).
  FrameSummary windowSummary() const { return summarize(recent_); }
  FrameSummary recordingSummary() const { return summarize(recorded_); }
  const FrameSample& last() const { return last_; }

  bool exportCsv(const std::filesystem::path& path) const;
  bool exportSummaryJson(const std::filesystem::path& path, const std::string& scenario,
                         const std::string& deviceName) const;

  static FrameSummary summarize(const std::vector<FrameSample>& samples);

 private:
  std::size_t window_;
  std::vector<FrameSample> recent_;
  std::vector<FrameSample> recorded_;
  FrameSample last_;
  bool recording_ = false;
};

}  // namespace fh
