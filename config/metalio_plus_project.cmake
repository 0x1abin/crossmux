# Copied to the ignored root CMakeLists.txt by the S31-only pre-build script.
cmake_minimum_required(VERSION 3.22)
if(IDF_TARGET STREQUAL "esp32s31")
  set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/config/metalio_plus_sdkconfig.defaults")
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
if(IDF_TARGET STREQUAL "esp32s31")
  project(crossmux_metalio_plus VERSION 2.0.0)
else()
  # A subsequent C3/S3 HybridCompile reuses this root before deleting it.
  project(crossmux)
endif()
