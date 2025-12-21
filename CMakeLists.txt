cmake_minimum_required(VERSION 3.18)

project(bootcamp)

file(GLOB_RECURSE IMPL_SRC "impl/src/*.cpp")

add_executable(bootcamp framework/src/main.cpp ${IMPL_SRC})
set_property(TARGET bootcamp PROPERTY CXX_STANDARD 17)
target_compile_options(bootcamp PRIVATE -mtune=skylake -march=skylake -Wall -Wextra)
target_include_directories(bootcamp PRIVATE framework/include impl/include)

# Set LOG_LEVEL if provided
if(DEFINED LOG_LEVEL)
    target_compile_definitions(bootcamp PRIVATE LOG_LEVEL=${LOG_LEVEL})
    message(STATUS "Log level set to: ${LOG_LEVEL} (0=OFF, 1=ERROR, 2=WARN, 3=INFO, 4=DEBUG)")
else()
    message(STATUS "Using default log level: 2 (WARN)")
endif()
