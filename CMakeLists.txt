cmake_minimum_required(VERSION 3.18)

project(bootcamp)

file(GLOB_RECURSE IMPL_SRC "impl/src/*.cpp")

add_executable(bootcamp framework/src/main.cpp ${IMPL_SRC})
set_property(TARGET bootcamp PROPERTY CXX_STANDARD 17)
target_compile_options(bootcamp PRIVATE -mtune=skylake -march=skylake -Wall -Wextra)
target_include_directories(bootcamp PRIVATE framework/include impl/include)
