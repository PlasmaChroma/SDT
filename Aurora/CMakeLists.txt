cmake_minimum_required(VERSION 3.20)
project(aurora LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(AURORA_ENABLE_WARNINGS "Enable strict compiler warnings" ON)

if(AURORA_ENABLE_WARNINGS)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast)
    add_compile_options(-fno-fast-math)
  elseif(MSVC)
    add_compile_options(/W4 /permissive- /fp:precise)
  endif()
endif()

add_subdirectory(src/aurora_lang)
add_subdirectory(src/aurora_core)
add_subdirectory(src/aurora_io)
add_subdirectory(src/aurora_cli)

