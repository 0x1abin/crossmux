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

# Third-party: ricmoo/QRCode (the same lib the firmware pulls via lib_deps). Plain
# Arduino C lib with no CMakeLists, so populate then compile qrcode.c ourselves.
# Lets the simulator render the real AirPage QR instead of the no-op shim.
FetchContent_Declare(
    qrcode
    GIT_REPOSITORY https://github.com/ricmoo/QRCode.git
    GIT_TAG v0.0.1
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(qrcode)
if(NOT qrcode_POPULATED)
    FetchContent_Populate(qrcode)
endif()

# libcurl — backs the simulator HTTPClient shim so WeRead (and any other on-device
# HTTPS code paths that we don't exclude) can hit the real network from host.
find_package(CURL REQUIRED)

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
# ...but keep the real HttpDownloader: the libcurl-backed HTTPClient shim lets it
# actually fetch over the network, so AirPage can pull images in the simulator.
list(APPEND CROSSPOINT_SRC ${REPO_ROOT}/src/network/HttpDownloader.cpp)
list(FILTER CROSSPOINT_SRC EXCLUDE REGEX "src/activities/network/")
list(FILTER CROSSPOINT_SRC EXCLUDE REGEX "OpdsBookBrowserActivity")
list(FILTER CROSSPOINT_SRC EXCLUDE REGEX "OtaUpdateActivity")
list(FILTER CROSSPOINT_SRC EXCLUDE REGEX "SdFirmwareUpdateActivity")
list(FILTER CROSSPOINT_SRC EXCLUDE REGEX "FontDownloadActivity")
list(FILTER CROSSPOINT_SRC EXCLUDE REGEX "KOReaderAuthActivity")
list(FILTER CROSSPOINT_SRC EXCLUDE REGEX "KOReaderSettingsActivity")
list(FILTER CROSSPOINT_SRC EXCLUDE REGEX "KOReaderSyncActivity")

list(FILTER CROSSPOINT_LIB EXCLUDE REGEX "lib/JpegToBmpConverter/")
list(FILTER CROSSPOINT_LIB EXCLUDE REGEX "lib/PngToBmpConverter/")
list(FILTER CROSSPOINT_LIB EXCLUDE REGEX "lib/Epub/Epub/converters/")
list(FILTER CROSSPOINT_LIB EXCLUDE REGEX "lib/Serialization/ObfuscationUtils")

# KOReaderSync credential store referenced via the KOREADER_STORE singleton in main.cpp.
# KOReaderJsonIO.cpp implements the load/save backing the store and must link with it.
list(APPEND CROSSPOINT_LIB
    ${REPO_ROOT}/lib/KOReaderSync/KOReaderCredentialStore.cpp
    ${REPO_ROOT}/lib/KOReaderSync/KOReaderJsonIO.cpp
)

set(SIMULATOR_HAL_NATIVE
    hal_native/HalClock_native.cpp
    hal_native/HalDisplay_native.cpp
    hal_native/HalGPIO_native.cpp
    hal_native/HalPowerManager_native.cpp
    hal_native/HalStorage_native.cpp
    hal_native/HalSystem_native.cpp
    hal_native/HalTiltSensor_native.cpp
)

# Vendored third-party C libraries inside the repo (+ the fetched QRCode lib).
set(VENDOR_C
    ${REPO_ROOT}/lib/expat/xmlparse.c
    ${REPO_ROOT}/lib/expat/xmlrole.c
    ${REPO_ROOT}/lib/expat/xmltok.c
    ${REPO_ROOT}/lib/uzlib/src/tinflate.c
    ${qrcode_SOURCE_DIR}/src/qrcode.c
)

# Generate I18n headers (mirrors what scripts/gen_i18n.py does pre-build for PlatformIO).
# All translation YAMLs are listed as inputs so edits to any one of them re-run the
# generator; otherwise downstream code referencing a freshly-added STR_* key would
# fail to compile after a merge that only touched the YAMLs.
set(I18N_GENERATED
    ${REPO_ROOT}/lib/I18n/I18nKeys.h
    ${REPO_ROOT}/lib/I18n/I18nStrings.h
    ${REPO_ROOT}/lib/I18n/I18nStrings.cpp
)
file(GLOB I18N_TRANSLATIONS "${REPO_ROOT}/lib/I18n/translations/*.yaml")
add_custom_command(
    OUTPUT ${I18N_GENERATED}
    COMMAND python3 ${REPO_ROOT}/scripts/gen_i18n.py ${REPO_ROOT}/lib/I18n/translations ${REPO_ROOT}/lib/I18n
    DEPENDS ${REPO_ROOT}/scripts/gen_i18n.py ${I18N_TRANSLATIONS}
    COMMENT "Generating I18n strings"
)
add_custom_target(i18n_generated DEPENDS ${I18N_GENERATED})

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

target_link_libraries(crosspoint_simulator PRIVATE arduino_host ArduinoJson CURL::libcurl)

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
    ${REPO_ROOT}/lib/Memory
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
    ${qrcode_SOURCE_DIR}/src
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

