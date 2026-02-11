add_executable(aurora
  main.cpp
)

target_include_directories(aurora
  PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(aurora
  PRIVATE
    aurora_lang
    aurora_core
    aurora_io
)

