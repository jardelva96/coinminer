cmake_minimum_required(VERSION 3.16)
project(coinminer C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(coinminer
  src/main.c
  src/sha256.c
)

if (MSVC)
  target_compile_options(coinminer PRIVATE /W4 /O2)
else()
  target_compile_options(coinminer PRIVATE -O2 -Wall -Wextra -Wpedantic)
endif()