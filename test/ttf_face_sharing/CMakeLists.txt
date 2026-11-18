set(FREEINK_FONT_ROOT ${REPO_ROOT}/freeink-sdk/libs/font/FreeInkFont)
file(GLOB FREETYPE_HOST_SOURCES CONFIGURE_DEPENDS ${FREEINK_FONT_ROOT}/src/freetype/*.c)
add_library(TtfFaceSharingBackend STATIC
  ${FREEINK_FONT_ROOT}/src/FtFont.cpp ${FREEINK_FONT_ROOT}/src/FontAlloc.c
  ${FREEINK_FONT_ROOT}/src/Gsub.cpp ${FREEINK_FONT_ROOT}/src/Gpos.cpp ${FREETYPE_HOST_SOURCES})
target_include_directories(TtfFaceSharingBackend PUBLIC
  ${FREEINK_FONT_ROOT}/include ${FREEINK_FONT_ROOT}/third_party/freetype/include
  ${FREEINK_FONT_ROOT}/third_party/stb)
target_compile_definitions(TtfFaceSharingBackend PRIVATE
  FREEINK_FONT_ENABLE_AUTOHINT=1 FREEINK_FONT_ENABLE_NATIVE_HINTING=1
  FREEINK_FONT_ENABLE_MONOCHROME=1 FREEINK_FONT_ENABLE_CFF=1)
add_executable(TtfFaceSharingTest test.cpp
  ${REPO_ROOT}/lib/EpdFont/TtfEpdFont.cpp ${REPO_ROOT}/lib/EpdFont/EpdFont.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFontFamily.cpp ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/lib/MiniBidi/minibidi.c ${REPO_ROOT}/lib/MiniBidi/BidiUtils.cpp)
target_compile_definitions(TtfFaceSharingTest PRIVATE BOARD_HAS_PSRAM=1 CROSSPOINT_EMULATED=1)
target_include_directories(TtfFaceSharingTest PRIVATE
  ${FREEINK_FONT_ROOT}/include ${REPO_ROOT}/test/chapter_html_slim_parser/stubs
  ${REPO_ROOT}/test/ble_input/stubs ${REPO_ROOT}/lib/EpdFont ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/MiniBidi ${REPO_ROOT}/lib/Memory ${REPO_ROOT}/test/minibidi_arabic/stubs)
target_link_libraries(TtfFaceSharingTest PRIVATE TtfFaceSharingBackend)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  foreach(target TtfFaceSharingBackend TtfFaceSharingTest)
    target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  endforeach()
  target_link_options(TtfFaceSharingTest PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME TtfFaceSharing COMMAND TtfFaceSharingTest
  ${REPO_ROOT}/freeink-sdk/libs/book/FreeInkBook/test/fixtures/fonts/DejaVuSans.ttf)
