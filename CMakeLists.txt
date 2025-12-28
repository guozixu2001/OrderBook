cmake_minimum_required(VERSION 3.18)

project(bootcamp)

# Enable testing
enable_testing()

# Add GoogleTest using FetchContent (gitee mirror)
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://gitee.com/mirrors/googletest.git
  GIT_TAG        v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

file(GLOB_RECURSE IMPL_SRC "impl/src/*.cpp")

# Create a static library for implementation code
add_library(impl_lib STATIC ${IMPL_SRC})
set_property(TARGET impl_lib PROPERTY CXX_STANDARD 17)
target_compile_options(impl_lib PRIVATE -mtune=skylake -march=skylake -Wall -Wextra)
target_include_directories(impl_lib PUBLIC framework/include impl/include)

# Main executable
add_executable(bootcamp framework/src/main.cpp)
set_property(TARGET bootcamp PROPERTY CXX_STANDARD 17)
target_compile_options(bootcamp PRIVATE -mtune=skylake -march=skylake -Wall -Wextra)
target_include_directories(bootcamp PRIVATE framework/include impl/include)
target_link_libraries(bootcamp PRIVATE impl_lib)

# Set LOG_LEVEL if provided
if(DEFINED LOG_LEVEL)
    target_compile_definitions(bootcamp PRIVATE LOG_LEVEL=${LOG_LEVEL})
    target_compile_definitions(impl_lib PRIVATE LOG_LEVEL=${LOG_LEVEL})
    message(STATUS "Log level set to: ${LOG_LEVEL} (0=OFF, 1=ERROR, 2=WARN, 3=INFO, 4=DEBUG)")
else()
    message(STATUS "Using default log level: 2 (WARN)")
endif()

# Add tests subdirectory
add_subdirectory(tests)
