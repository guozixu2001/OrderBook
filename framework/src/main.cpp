#include <cstdio>
#include "gateway.hpp"
#include "reader.hpp"
#include "framework/logger.hpp"

#include "impl/impl.hpp"

int main(int argc, char **argv) {
  if (argc < 3) {
    return 1;
  }

  framework::Reader reader{argv[1]};
  framework::Gateway gateway{argv[2]};
  impl::Impl{reader, gateway}.run();
}
