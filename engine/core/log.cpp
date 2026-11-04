#include "engine/core/log.h"

#include <atomic>
#include <chrono>
#include <mutex>

namespace fh::log {
namespace {
std::mutex g_mutex;
Level g_minLevel = Level::Info;
FILE* g_file = nullptr;
std::atomic<int> g_warnings{0};
std::atomic<int> g_errors{0};
const auto g_start = std::chrono::steady_clock::now();

const char* levelName(Level l) {
  switch (l) {
    case Level::Debug: return "DEBUG";
    case Level::Info: return "INFO ";
    case Level::Warn: return "WARN ";
    case Level::Error: return "ERROR";
    case Level::Off: return "OFF  ";
  }
  return "?";
}
}  // namespace

void write(Level level, const char* subsystem, const std::string& message) {
  if (level == Level::Warn) ++g_warnings;
  if (level == Level::Error) ++g_errors;
  if (level < g_minLevel) return;
  const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - g_start).count();
  std::lock_guard lock(g_mutex);
  FILE* out = level >= Level::Warn ? stderr : stdout;
  std::fprintf(out, "[%9.3f] %s [%s] %s\n", t, levelName(level), subsystem, message.c_str());
  std::fflush(out);
  if (g_file) {
    std::fprintf(g_file, "[%9.3f] %s [%s] %s\n", t, levelName(level), subsystem, message.c_str());
    std::fflush(g_file);
  }
}

void setMinLevel(Level level) { g_minLevel = level; }

void setLogFile(const std::string& path) {
  std::lock_guard lock(g_mutex);
  if (g_file) std::fclose(g_file);
  g_file = path.empty() ? nullptr : std::fopen(path.c_str(), "w");
}

int warningCount() { return g_warnings.load(); }
int errorCount() { return g_errors.load(); }

}  // namespace fh::log
