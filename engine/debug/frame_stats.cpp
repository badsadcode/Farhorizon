#include "engine/debug/frame_stats.h"

#include <algorithm>
#include <format>

#include <nlohmann/json.hpp>

#include "engine/core/file_io.h"

namespace fh {

void FrameStats::push(const FrameSample& s) {
  last_ = s;
  recent_.push_back(s);
  if (recent_.size() > window_) recent_.erase(recent_.begin());
  if (recording_) recorded_.push_back(s);
}

FrameSummary FrameStats::summarize(const std::vector<FrameSample>& samples) {
  FrameSummary r;
  r.frames = samples.size();
  if (samples.empty()) return r;
  std::vector<double> cpu;
  cpu.reserve(samples.size());
  double gpuSum = 0;
  std::size_t gpuCount = 0;
  for (const auto& s : samples) {
    cpu.push_back(s.cpuMs);
    if (s.gpuMs >= 0) {
      gpuSum += s.gpuMs;
      ++gpuCount;
      r.maxGpuMs = std::max(r.maxGpuMs, s.gpuMs);
    }
  }
  double sum = 0;
  for (double v : cpu) sum += v;
  r.avgCpuMs = sum / double(cpu.size());
  std::sort(cpu.begin(), cpu.end());
  r.minCpuMs = cpu.front();
  r.maxCpuMs = cpu.back();
  r.p99CpuMs = cpu[std::min(cpu.size() - 1, static_cast<std::size_t>(double(cpu.size()) * 0.99))];
  if (gpuCount) r.avgGpuMs = gpuSum / double(gpuCount);
  r.avgFps = r.avgCpuMs > 0 ? 1000.0 / r.avgCpuMs : 0;
  return r;
}

bool FrameStats::exportCsv(const std::filesystem::path& path) const {
  std::string out = "frame,cpu_ms,gpu_ms,draw_calls,triangles\n";
  for (std::size_t i = 0; i < recorded_.size(); ++i) {
    const auto& s = recorded_[i];
    out += std::format("{},{:.4f},{:.4f},{},{}\n", i, s.cpuMs, s.gpuMs, s.drawCalls, s.triangles);
  }
  return writeFileAtomic(path, out.data(), out.size());
}

bool FrameStats::exportSummaryJson(const std::filesystem::path& path, const std::string& scenario,
                                   const std::string& deviceName) const {
  const FrameSummary s = recordingSummary();
  nlohmann::json j = {
      {"scenario", scenario},       {"device", deviceName},      {"frames", s.frames},
      {"avgCpuMs", s.avgCpuMs},     {"minCpuMs", s.minCpuMs},    {"maxCpuMs", s.maxCpuMs},
      {"p99CpuMs", s.p99CpuMs},     {"avgGpuMs", s.avgGpuMs},    {"maxGpuMs", s.maxGpuMs},
      {"avgFps", s.avgFps},
      {"drawCalls", recorded_.empty() ? 0u : recorded_.back().drawCalls},
      {"triangles", recorded_.empty() ? 0ull : static_cast<unsigned long long>(recorded_.back().triangles)},
  };
  const std::string text = j.dump(2);
  return writeFileAtomic(path, text.data(), text.size());
}

}  // namespace fh
