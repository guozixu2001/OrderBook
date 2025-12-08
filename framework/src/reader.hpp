#pragma once

#include <cerrno>
#include <climits>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <tuple>
#include <unistd.h>

#include "framework/define.hpp"

namespace framework {
class Reader {
private:
  int fd_;
  char s[1024];
  size_t offset = 0;
  size_t size = 0;
  size_t total_bytes = 0;

public:
  explicit Reader(const std::string &file_name) {
    fd_ = open(file_name.c_str(), O_RDONLY);
    if (fd_ == -1) {
      fprintf(stderr, "open failed: %s(%d)\n", strerror(errno), errno);
      abort();
    }
  }
  std::tuple<ReaderStatus, const char *, size_t> try_get_tick() {
    while (offset < sizeof(message_header)) {
      ssize_t r = read(fd_, s + offset, sizeof(message_header) - offset);
      if (r < 0) {
        if (errno == EINTR) {
          continue;
        }
        fprintf(stderr, "read failed: %s(%d)\n", strerror(errno), errno);
        abort();
      }
      if (r == 0) {
        return {ReaderStatus::FINISHED, nullptr, 0};
      }
      offset += r;
    }
    message_header *header = reinterpret_cast<message_header *>(s);
    while (offset < header->size) {
      ssize_t r = read(fd_, s + offset, header->size - offset);
      if (r < 0) {
        if (errno == EINTR) {
          continue;
        }
        fprintf(stderr, "read failed: %s(%d)\n", strerror(errno), errno);
        abort();
      }
      if (r == 0) {
        fprintf(stderr, "unexpected file end!\n");
        abort();
      }
      offset += r;
    }
    size = offset;
    offset = 0;
    total_bytes += size;
    return {ReaderStatus::OK, s, size};
  }
};
} // namespace framework
