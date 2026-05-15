cmake_minimum_required(VERSION 3.16)
project(crosspoint_simulator LANGUAGES CXX C)

get_filename_component(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR} DIRECTORY)

# -----------------------------------------------------------------------------
add_subdirectory(arduino-host)

# Third-party: ArduinoJson (header-only, fetched once).
include(FetchContent)
FetchContent_Declare(
    arduinojson
    GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
    GIT_TAG v7.4.2
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(arduinojson)

# SDL2 — required for the simulator window.
find_package(SDL2 QUIET)
if(NOT SDL2_FOUND)
    find_program(SDL2_CONFIG NAMES sdl2-config)
    if(SDL2_CONFIG)
        execute_process(COMMAND ${SDL2_CONFIG} --cflags
            OUTPUT_VARIABLE SDL2_CFLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
        execute_process(COMMAND ${SDL2_CONFIG} --libs
            OUTPUT_VARIABLE SDL2_LDFLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
        separate_arguments(SDL2_CFLAGS UNIX_COMMAND "${SDL2_CFLAGS}")
        separate_arguments(SDL2_LDFLAGS UNIX_COMMAND "${SDL2_LDFLAGS}")
    else()
        message(FATAL_ERROR "SDL2 not found. Install via 'brew install sdl2' or 'apt install libsdl2-dev'.")
    endif()
endif()

# -----------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# -----------------------------------------------------------------------------
# Project source globs
# -----------------------------------------------------------------------------
file(GLOB_RECURSE CROSSPOINT_SRC "${REPO_ROOT}/src/*.cpp")
file(GLOB_RECURSE CROSSPOINT_LIB
    "${REPO_ROOT}/lib/Epub/*.cpp"
    "${REPO_ROOT}/lib/EpdFont/*.cpp"
    "${REPO_ROOT}/lib/FsHelpers/*.cpp"
    "${REPO_ROOT}/lib/GfxRenderer/*.cpp"
    "${REPO_ROOT}/lib/I18n/*.cpp"
    "${REPO_ROOT}/lib/InflateReader/*.cpp"
    "${REPO_ROOT}/lib/JpegToBmpConverter/*.cpp"
    "${REPO_ROOT}/lib/JsonParser/*.cpp"
    "${REPO_ROOT}/lib/Logging/*.cpp"
    "${REPO_ROOT}/lib/OpdsParser/*.cpp"
    "${REPO_ROOT}/lib/PngToBmpConverter/*.cpp"
    "${REPO_ROOT}/lib/Serialization/*.cpp"
    "${REPO_ROOT}/lib/Txt/*.cpp"
    "${REPO_ROOT}/lib/Utf8/*.cpp"
    "${REPO_ROOT}/lib/XmlParserUtils/*.cpp"
    "${REPO_ROOT}/lib/Xtc/*.cpp"
    "${REPO_ROOT}/lib/ZipFile/*.cpp"
    "${REPO_ROOT}/lib/AvatarGen/*.cpp"
)

# Real HAL .cpp is replaced by simulator/hal_native/.
list(FILTER CROSSPOINT_LIB EXCLUDE REGEX "lib/hal/Hal.*\\.cpp$")

# Network / OTA / image-decoder .cpp files use APIs (real PNGdec / JPEGDEC / ESPmDNS /
# precise WiFi calls) that are too deep to shim for first-version simulator scope.
# Exclude them from sources; symbols their public classes expose are stubbed via
# simulator/missing_symbols.cpp so the linker still resolves vtables and constructors.

list(FILTER CROSSPOINT_SRC EXCLUDE REGEX "src/network/")
list(FILTER CROSSPOINT_SRC EXCLUDE REGEX "src/activities/network/")
list(FILTER CROSSPOINT_SRC EXCLUDE REGEX "OpdsBookBrowserActivity")
list(FILTER CROSSPOINT_SRC EXCLUDE REGEX "OtaUpdateActivity")
list(FILTER CROSSPOINT_SRC EXCLUDE REGEX "SdFirmwareUpdateActivity")
list(FILTER CROSSPOINT_SRC EXCLUDE REGEX "FontDownloadActivity")
list(FILTER CROSSPOINT_SRC EXCLUDE REGEX "KOReaderAuthActivity")
list(FILTER CROSSPOINT_SRC EXCLUDE REGEX "KOReaderSettingsActivity")
list(FILTER CROSSPOINT_SRC EXCLUDE REGEX "KOReaderSyncActivity")
list(FILTER CROSSPOINT_SRC EXCLUDE REGEX "QrUtils")

list(FILTER CROSSPOINT_LIB EXCLUDE REGEX "lib/JpegToBmpConverter/")
list(FILTER CROSSPOINT_LIB EXCLUDE REGEX "lib/PngToBmpConverter/")
list(FILTER CROSSPOINT_LIB EXCLUDE REGEX "lib/Epub/Epub/converters/")
list(FILTER CROSSPOINT_LIB EXCLUDE REGEX "lib/Serialization/ObfuscationUtils")

# KOReaderSync credential store referenced via the KOREADER_STORE singleton in main.cpp.
list(APPEND CROSSPOINT_LIB
    ${REPO_ROOT}/lib/KOReaderSync/KOReaderCredentialStore.cpp
)

set(SIMULATOR_HAL_NATIVE
    hal_native/HalDisplay_native.cpp
    hal_native/HalGPIO_native.cpp
    hal_native/HalPowerManager_native.cpp
    hal_native/HalStorage_native.cpp
    hal_native/HalSystem_native.cpp
    hal_native/HalTiltSensor_native.cpp
)

# Vendored third-party C libraries inside the repo.
set(VENDOR_C
    ${REPO_ROOT}/lib/expat/xmlparse.c
    ${REPO_ROOT}/lib/expat/xmlrole.c
    ${REPO_ROOT}/lib/expat/xmltok.c
    ${REPO_ROOT}/lib/uzlib/src/tinflate.c
)

# Generate I18n headers (mirrors what scripts/gen_i18n.py does pre-build for PlatformIO).
# Simulator builds with ENABLE_CHINESE_VERSION=1 so the i18n blob must contain
# english + simplified_chinese only (matching gh_release_cn), not all 22+
# languages — otherwise the Language picker shows the wrong list.
#
# This is an ALL target (not add_custom_command with OUTPUT) so it runs on every
# cmake --build invocation. We cannot rely on mtime checks: PlatformIO may have
# just written a 22-language i18n into the same lib/I18n/ files (because it
# excludes simplified_chinese from the international build), and that file
# would falsely appear "up-to-date" relative to the YAML mtimes.
set(I18N_GENERATED
    ${REPO_ROOT}/lib/I18n/I18nKeys.h
    ${REPO_ROOT}/lib/I18n/I18nStrings.h
    ${REPO_ROOT}/lib/I18n/I18nStrings.cpp
)
file(GLOB I18N_TRANSLATIONS CONFIGURE_DEPENDS "${REPO_ROOT}/lib/I18n/translations/*.yaml")
add_custom_target(i18n_generated ALL
    BYPRODUCTS ${I18N_GENERATED}
    COMMAND python3 ${REPO_ROOT}/scripts/gen_i18n.py
            ${REPO_ROOT}/lib/I18n/translations ${REPO_ROOT}/lib/I18n
            --whitelist english,simplified_chinese
    DEPENDS ${REPO_ROOT}/scripts/gen_i18n.py ${I18N_TRANSLATIONS}
            ${CMAKE_CURRENT_LIST_FILE}
    COMMENT "Generating I18n strings (simulator, CN whitelist)"
    VERBATIM
)

add_executable(crosspoint_simulator
    simulator_main.cpp
    simulator_window.cpp
    simulator_shell.cpp
    simulator_overlay.cpp
    simulator_settings.cpp
    missing_symbols.cpp
    ${SIMULATOR_HAL_NATIVE}
    ${CROSSPOINT_SRC}
    ${CROSSPOINT_LIB}
    ${VENDOR_C}
    ${REPO_ROOT}/lib/I18n/I18nStrings.cpp
)
add_dependencies(crosspoint_simulator i18n_generated)

target_link_libraries(crosspoint_simulator PRIVATE arduino_host ArduinoJson)

if(SDL2_FOUND)
    target_link_libraries(crosspoint_simulator PRIVATE SDL2::SDL2)
else()
    target_compile_options(crosspoint_simulator PRIVATE ${SDL2_CFLAGS})
    target_link_options(crosspoint_simulator PRIVATE ${SDL2_LDFLAGS})
endif()

target_include_directories(crosspoint_simulator PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/shims
    ${REPO_ROOT}/src
    ${REPO_ROOT}/lib/hal
    ${REPO_ROOT}/lib/Logging
    ${REPO_ROOT}/lib/I18n
    ${REPO_ROOT}/lib/GfxRenderer
    ${REPO_ROOT}/lib/EpdFont
    ${REPO_ROOT}/lib/EpdFont/builtinFonts
    ${REPO_ROOT}/lib/Epub
    ${REPO_ROOT}/lib/Epub/Epub
    ${REPO_ROOT}/lib/FsHelpers
    ${REPO_ROOT}/lib/InflateReader
    ${REPO_ROOT}/lib/JpegToBmpConverter
    ${REPO_ROOT}/lib/JsonParser
    ${REPO_ROOT}/lib/OpdsParser
    ${REPO_ROOT}/lib/PngToBmpConverter
    ${REPO_ROOT}/lib/Serialization
    ${REPO_ROOT}/lib/Txt
    ${REPO_ROOT}/lib/Utf8
    ${REPO_ROOT}/lib/XmlParserUtils
    ${REPO_ROOT}/lib/Xtc
    ${REPO_ROOT}/lib/ZipFile
    ${REPO_ROOT}/lib/expat
    ${REPO_ROOT}/lib/uzlib/src
    ${REPO_ROOT}/lib/KOReaderSync
    ${REPO_ROOT}/lib/JpegToBmpConverter
    ${REPO_ROOT}/lib/PngToBmpConverter
    ${REPO_ROOT}/lib/Epub/Epub/converters
    ${REPO_ROOT}/lib/AvatarGen
)

# Rename Arduino entry points so simulator_main.cpp can drive them explicitly.
target_compile_definitions(crosspoint_simulator PRIVATE
    CROSSPOINT_EMULATED=1
    EINK_DISPLAY_SINGLE_BUFFER_MODE=1
    LOG_LEVEL=2
    ENABLE_SERIAL_LOG=1
    ENABLE_CHINESE_VERSION=1
    XML_GE=0
    XML_CONTEXT_BYTES=1024
    USE_UTF8_LONG_NAMES=1
    CROSSPOINT_VERSION="simulator-dev"
    "setup=firmware_setup"
    "loop=firmware_loop"
)

target_compile_options(crosspoint_simulator PRIVATE -fno-exceptions -fno-rtti
    -Wno-unused-parameter -Wno-deprecated-declarations -Wno-bidi-chars)

