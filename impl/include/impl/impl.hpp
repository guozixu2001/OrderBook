#pragma once

namespace impl {
template <class Reader, class Gateway> class Impl {
private:
  Reader &reader;
  Gateway &gateway;
  std::vector<int64_t> grids;

public:
  Impl(Reader &reader, Gateway &gateway, const std::vector<int64_t>& grids)
    : reader{reader}, gateway{gateway}, grids(grids) {}

  Impl(Impl &&other) noexcept = delete;

  Impl &operator=(Impl &&other) noexcept = delete;

  ~Impl() = default;

  void run() {}
};
} // namespace impl
