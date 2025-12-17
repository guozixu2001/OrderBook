#include "gateway.hpp"
#include "reader.hpp"
#include "grids.hpp"

#include "impl/impl.hpp"

int main(int argc, char **argv) {
  if (argc < 4) {
    return 1;
  }

  framework::Reader reader{argv[1]};
  framework::Gateway gateway{argv[2]};
  auto grids = framework::Grids::read(argv[3]);

  impl::Impl{reader, gateway, grids}.run();
}
