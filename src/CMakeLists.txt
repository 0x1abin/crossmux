# PlatformIO resolves reader libraries and their private build scripts.
file(GLOB_RECURSE crossmux_sources "${CMAKE_CURRENT_LIST_DIR}/*.cpp" "${CMAKE_CURRENT_LIST_DIR}/*.c")
idf_component_register(SRCS ${crossmux_sources} INCLUDE_DIRS "." REQUIRES framework-arduinoespressif32)
# Source options follow IDF's C++26 default in the compiler command. A target
# CXX_STANDARD property precedes that flag and cannot override it.
set_source_files_properties(${crossmux_sources} PROPERTIES COMPILE_OPTIONS "$<$<COMPILE_LANGUAGE:CXX>:-std=gnu++20>")
