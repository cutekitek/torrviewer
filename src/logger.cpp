#include "logger.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <string>

namespace torrview {
namespace {

std::mutex log_mutex;

std::string lowercase(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

LogLevel parse_level(const char* value) {
  if (value == nullptr || value[0] == '\0') {
    return LogLevel::info;
  }

  const std::string level = lowercase(value);
  if (level == "debug" || level == "trace" || level == "0") {
    return LogLevel::debug;
  }
  if (level == "info" || level == "1") {
    return LogLevel::info;
  }
  if (level == "warning" || level == "warn" || level == "2") {
    return LogLevel::warning;
  }
  if (level == "error" || level == "err" || level == "3") {
    return LogLevel::error;
  }
  if (level == "off" || level == "none" || level == "quiet" || level == "4") {
    return LogLevel::off;
  }
  return LogLevel::info;
}

LogLevel threshold() {
  static const LogLevel level = parse_level(std::getenv("TORRVIEW_LOG_LEVEL"));
  return level;
}

const char* level_label(LogLevel level) {
  switch (level) {
  case LogLevel::debug:
    return "debug";
  case LogLevel::info:
    return "info";
  case LogLevel::warning:
    return "warning";
  case LogLevel::error:
    return "error";
  case LogLevel::off:
    return "off";
  }
  return "unknown";
}

const char* basename(const char* path) {
  if (path == nullptr) {
    return "";
  }

  const char* name = path;
  for (const char* cursor = path; *cursor != '\0'; ++cursor) {
    if (*cursor == '/' || *cursor == '\\') {
      name = cursor + 1;
    }
  }
  return name;
}

std::string timestamp() {
  const auto now = std::chrono::system_clock::now();
  const auto time = std::chrono::system_clock::to_time_t(now);
  std::tm local_time{};
#if defined(_WIN32)
  localtime_s(&local_time, &time);
#else
  localtime_r(&time, &local_time);
#endif

  std::ostringstream out;
  out << std::put_time(&local_time, "%H:%M:%S");
  return out.str();
}

} // namespace

bool log_enabled(LogLevel level) {
  return static_cast<int>(level) >= static_cast<int>(threshold()) && level != LogLevel::off;
}

void log_message(LogLevel level, const char* file, int line, std::string_view message) {
  std::lock_guard lock(log_mutex);
  std::cerr << timestamp() << " [" << level_label(level) << "] " << basename(file) << ':' << line
            << " " << message << '\n';
}

} // namespace torrview
