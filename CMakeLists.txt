cmake_minimum_required(VERSION 3.10)
project(sh-pointer VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED False)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin" CACHE PATH "Directory to place executables.")

add_subdirectory(googletest)
add_subdirectory(tests)
