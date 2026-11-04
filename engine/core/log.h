#pragma once
// Minimal thread-safe logger. Every message carries a subsystem tag so failures
// have context (asset ID, scene, state) instead of being swallowed.
#include <cstdio>
#include <string>
#include <format>
#include <utility>

namespace fh::log {

enum class Level { Debug, Info, Warn, Error, Off };

void write(Level level, const char* subsystem, const std::string& message);
void setMinLevel(Level level);
// Optional mirror of all log lines into a file (e.g. farhorizon.log). Empty path disables.
void setLogFile(const std::string& path);
// Number of Warn/Error messages emitted so far (shown on the developer overlay).
int warningCount();
int errorCount();

template <class... Args>
void debug(const char* sys, std::format_string<Args...> fmt, Args&&... args) {
  write(Level::Debug, sys, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void info(const char* sys, std::format_string<Args...> fmt, Args&&... args) {
  write(Level::Info, sys, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void warn(const char* sys, std::format_string<Args...> fmt, Args&&... args) {
  write(Level::Warn, sys, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void error(const char* sys, std::format_string<Args...> fmt, Args&&... args) {
  write(Level::Error, sys, std::format(fmt, std::forward<Args>(args)...));
}

}  // namespace fh::log
