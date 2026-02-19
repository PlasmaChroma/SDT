find_package(ZLIB REQUIRED)

add_library(aurora_io
  analysis_writer.cpp
  audio_reader.cpp
  wav_writer.cpp
  png_writer.cpp
  midi_writer.cpp
  json_writer.cpp
)

target_include_directories(aurora_io
  PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(aurora_io
  PUBLIC
    aurora_core
    ZLIB::ZLIB
)
