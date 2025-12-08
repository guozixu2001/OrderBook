#pragma once

namespace impl {
template <class Reader, class Gateway> class Impl {
private:
  Reader &reader;
  Gateway &gateway;

public:
  Impl(Reader &reader, Gateway &gateway) : reader{reader}, gateway{gateway} {}

  Impl(Impl &&other) noexcept = delete;

  Impl &operator=(Impl &&other) noexcept = delete;

  ~Impl() = default;

  void run() {}
};
} // namespace impl
