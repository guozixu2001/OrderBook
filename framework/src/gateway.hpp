#pragma once

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "framework/logger.hpp"

namespace framework {
class Gateway {
private:
  int fd;

public:
  explicit Gateway(const std::string &file_name) {
    fd = open(file_name.c_str(), O_RDWR | O_CREAT | O_EXCL, (mode_t)0644);
    if (fd == -1) {
      LOG_ERROR("open failed: %s(%d)", strerror(errno), errno);
      abort();
    }
  }

  void signal(const char *name, const char* symbol, uint64_t time, double value) {
    constexpr int BUFFER_SIZE = 1024;
    char buffer[BUFFER_SIZE];
    auto ret = snprintf(buffer, BUFFER_SIZE, "%s,%s,%lu,%f\n", name, symbol, time, value);
    if (ret >= BUFFER_SIZE) {
      LOG_ERROR("snprintf exceed buffer");
      abort();
    }

    // Write to file
    ssize_t written = write(fd, buffer, ret);
    if (written == -1) {
      LOG_ERROR("write failed: %s(%d)", strerror(errno), errno);
      abort();
    }
  }
};
} // namespace framework
