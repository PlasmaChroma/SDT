add_library(aurora_lang
  parser.cpp
  validation.cpp
)

target_include_directories(aurora_lang
  PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

