add_library(aurora_core
  analyzer.cpp
  spectrogram.cpp
  renderer.cpp
)

target_include_directories(aurora_core
  PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(aurora_core
  PUBLIC
    aurora_lang
)
