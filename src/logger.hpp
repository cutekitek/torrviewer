#pragma once

#include <sstream>
#include <string_view>

namespace torrview {

enum class LogLevel {
  debug = 0,
  info = 1,
  warning = 2,
  error = 3,
  off = 4,
};

bool log_enabled(LogLevel level);
void log_message(LogLevel level, const char* file, int line, std::string_view message);

} // namespace torrview

#define TORRVIEW_LOG(level, message)                                                               \
  do {                                                                                             \
    if (::torrview::log_enabled(level)) {                                                          \
      std::ostringstream torrview_log_stream;                                                       \
      torrview_log_stream << message;                                                              \
      ::torrview::log_message(level, __FILE__, __LINE__, torrview_log_stream.str());                \
    }                                                                                              \
  } while (false)

#define TORRVIEW_LOG_DEBUG(message) TORRVIEW_LOG(::torrview::LogLevel::debug, message)
#define TORRVIEW_LOG_INFO(message) TORRVIEW_LOG(::torrview::LogLevel::info, message)
#define TORRVIEW_LOG_WARNING(message) TORRVIEW_LOG(::torrview::LogLevel::warning, message)
#define TORRVIEW_LOG_ERROR(message) TORRVIEW_LOG(::torrview::LogLevel::error, message)
