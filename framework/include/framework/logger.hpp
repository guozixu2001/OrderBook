#pragma once

#include <cstdio>
#include <cstdarg>

// Compile-time configurable log level
// Change this to adjust logging verbosity at compile time
// Levels: 0 = OFF, 1 = ERROR, 2 = WARN, 3 = INFO, 4 = DEBUG
#ifndef LOG_LEVEL
#define LOG_LEVEL 4  // Default to WARN level (ERROR and WARN only)
#endif

namespace framework {
namespace logger {

// Log levels as enum
enum class Level {
  ERROR = 1,
  WARN = 2,
  INFO = 3,
  DEBUG = 4
};

// Internal logging function - not for direct use
inline void logv(Level level, const char* format, va_list args) {
  // Minimal stack buffer for log formatting
  // Must be large enough for typical log messages but small enough for cache friendliness
  char buffer[512];

  // Determine prefix based on level
  const char* prefix = "";
  switch (level) {
    case Level::ERROR:
      prefix = "ERROR";
      break;
    case Level::WARN:
      prefix = "WARN";
      break;
    case Level::INFO:
      prefix = "INFO";
      break;
    case Level::DEBUG:
      prefix = "DEBUG";
      break;
  }

  // Format with prefix: "<LEVEL>: <message>\n"
  int prefix_len = snprintf(buffer, sizeof(buffer), "%s: ", prefix);
  if (prefix_len < 0 || prefix_len >= static_cast<int>(sizeof(buffer))) {
    // Fallback if prefix couldn't be written
    fprintf(stderr, "%s: <log formatting error>\n", prefix);
    return;
  }

  // Append the actual message
  int msg_len = vsnprintf(buffer + prefix_len, sizeof(buffer) - prefix_len, format, args);
  if (msg_len < 0) {
    fprintf(stderr, "%s: <log formatting error>\n", prefix);
    return;
  }

  // Ensure newline at end
  int total_len = prefix_len + msg_len;
  if (total_len < static_cast<int>(sizeof(buffer)) - 1 && buffer[total_len - 1] != '\n') {
    int ret = snprintf(buffer + total_len, sizeof(buffer) - total_len, "\n");
    if (ret > 0) {
      total_len += ret;
    }
  }

  // Log to stderr (no heap allocation, direct I/O)
  fwrite(buffer, 1, total_len, stderr);
  fflush(stderr);  // Ensure immediate output
}

// Public logging functions
// Each function is compiled out if LOG_LEVEL is below the function's level

inline void error(const char* format, ...) {
#if LOG_LEVEL >= 1
  va_list args;
  va_start(args, format);
  logv(Level::ERROR, format, args);
  va_end(args);
#endif
}

inline void warn(const char* format, ...) {
#if LOG_LEVEL >= 2
  va_list args;
  va_start(args, format);
  logv(Level::WARN, format, args);
  va_end(args);
#endif
}

inline void info(const char* format, ...) {
#if LOG_LEVEL >= 3
  va_list args;
  va_start(args, format);
  logv(Level::INFO, format, args);
  va_end(args);
#endif
}

inline void debug(const char* format, ...) {
#if LOG_LEVEL >= 4
  va_list args;
  va_start(args, format);
  logv(Level::DEBUG, format, args);
  va_end(args);
#endif
}

// For signal messages (trading signals) - always enabled
inline void signal(const char* format, ...) {
  char buffer[512];

  // Add SIGNAL prefix
  int prefix_len = snprintf(buffer, sizeof(buffer), "SIGNAL: ");
  if (prefix_len < 0 || prefix_len >= static_cast<int>(sizeof(buffer))) {
    fprintf(stderr, "SIGNAL: <log formatting error>\n");
    return;
  }

  va_list args;
  va_start(args, format);
  int msg_len = vsnprintf(buffer + prefix_len, sizeof(buffer) - prefix_len, format, args);
  va_end(args);

  if (msg_len < 0) {
    fprintf(stderr, "SIGNAL: <log formatting error>\n");
    return;
  }

  // Ensure newline
  int total_len = prefix_len + msg_len;
  if (total_len < static_cast<int>(sizeof(buffer)) - 1 && buffer[total_len - 1] != '\n') {
    int ret = snprintf(buffer + total_len, sizeof(buffer) - total_len, "\n");
    if (ret > 0) {
      total_len += ret;
    }
  }

  fwrite(buffer, 1, total_len, stderr);
  fflush(stderr);  // Ensure immediate output
}

} // namespace logger
} // namespace framework

// Convenience macros for file and line information
#define LOG_ERROR(...) framework::logger::error(__VA_ARGS__)
#define LOG_WARN(...)  framework::logger::warn(__VA_ARGS__)
#define LOG_INFO(...)  framework::logger::info(__VA_ARGS__)
#define LOG_DEBUG(...) framework::logger::debug(__VA_ARGS__)
#define LOG_SIGNAL(...) framework::logger::signal(__VA_ARGS__)

