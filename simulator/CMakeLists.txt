cmake_minimum_required(VERSION 3.16)
project(crosspoint_simulator LANGUAGES CXX C)

get_filename_component(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR} DIRECTORY)

# -----------------------------------------------------------------------------
# WASM uses pthreads (Web Workers + SharedArrayBuffer) to back FreeRTOS-on-std::thread.
# Shared-memory linking requires EVERY object — including the arduino-host subdirectory
# and the vendored C libs — to be built with atomics/bulk-memory (-pthread). Set it at
# directory scope BEFORE add_subdirectory so it propagates to all targets below.
if(EMSCRIPTEN)
    add_compile_options(-pthread)
    add_link_options(-pthread)
endif()

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
# Lets the simulator render the real AirPage QR instead of the no-op shim. Fetched
# for both native and WASM (qrcode_SOURCE_DIR feeds VENDOR_C + include dirs below).
FetchContent_Declare(
    qrcode
    GIT_REPOSITORY https://github.com/ricmoo/QRCode.git
    GIT_TAG v0.0.1
    GIT_SHALLOW TRUE
)
# QRCode ships no CMakeLists, so MakeAvailable just populates (no add_subdirectory)
# and still sets qrcode_SOURCE_DIR. Replaces the deprecated FetchContent_Populate().
FetchContent_MakeAvailable(qrcode)

# libcurl — backs the native simulator HTTPClient shim so WeRead (and any other
# on-device HTTPS code paths that we don't exclude) can hit the real network from host.
# The WASM build is offline-only (shims/HTTPClient.h compiles a curl-free stub under
# #ifdef __EMSCRIPTEN__), so it skips libcurl entirely.
if(NOT EMSCRIPTEN)
    find_package(CURL REQUIRED)
endif()

# SDL2 — native simulator window only. The WASM build renders via the browser canvas
# and reads input from JavaScript, so it has no SDL dependency.
if(NOT EMSCRIPTEN)
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

# Real HAL .cpp is replaced by simulator/hal/.
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

# HAL backends for the simulator. All implementations live in hal/ and compile for both the
# native (SDL) and WASM (browser) builds. HalDisplay/HalGPIO carry the only platform split,
# behind #ifdef __EMSCRIPTEN__ inside the file: native pushes the framebuffer to an SDL texture
# and reads SDL events; WASM flips a framebuffer dirty-flag and takes input from JavaScript.
# Clock/Power/Storage/System/Tilt are pure POSIX + std::chrono, which Emscripten supports as-is.
set(SIMULATOR_HAL
    hal/HalClock.cpp
    hal/HalDisplay.cpp
    hal/HalGPIO.cpp
    hal/HalPowerManager.cpp
    hal/HalStorage.cpp
    hal/HalSystem.cpp
    hal/HalTiltSensor.cpp
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

# Entry point + windowing. Native drives an SDL window/event loop; WASM has no window
# (rendering + input are owned by the browser), so it uses a minimal entry point.
if(EMSCRIPTEN)
    set(SIMULATOR_ENTRY simulator_main_wasm.cpp)
else()
    set(SIMULATOR_ENTRY
        simulator_main.cpp
        simulator_window.cpp
        simulator_shell.cpp
        simulator_overlay.cpp
        simulator_settings.cpp)
endif()

add_executable(crosspoint_simulator
    ${SIMULATOR_ENTRY}
    missing_symbols.cpp
    ${SIMULATOR_HAL}
    ${CROSSPOINT_SRC}
    ${CROSSPOINT_LIB}
    ${VENDOR_C}
    ${REPO_ROOT}/lib/I18n/I18nStrings.cpp
)
add_dependencies(crosspoint_simulator i18n_generated)

target_link_libraries(crosspoint_simulator PRIVATE arduino_host ArduinoJson)

if(NOT EMSCRIPTEN)
    target_link_libraries(crosspoint_simulator PRIVATE CURL::libcurl)
    if(SDL2_FOUND)
        target_link_libraries(crosspoint_simulator PRIVATE SDL2::SDL2)
    else()
        target_compile_options(crosspoint_simulator PRIVATE ${SDL2_CFLAGS})
        target_link_options(crosspoint_simulator PRIVATE ${SDL2_LDFLAGS})
    endif()
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
    ${REPO_ROOT}/lib/Epub/Epub/converters
    ${REPO_ROOT}/lib/AvatarGen
)

# Rename Arduino entry points so the simulator entry can drive them explicitly.
# ENABLE_CHINESE_VERSION is on for both native and WASM: the browser demo ships the
# Chinese build (CJK fonts + WeRead/中国象棋/农历/CJK typography), with the startup
# language following navigator.language (see simulator_main_wasm.cpp --lang).
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

# -----------------------------------------------------------------------------
# WebAssembly (Emscripten) link configuration.
#   - pthreads (Web Workers + SharedArrayBuffer) back FreeRTOS-on-std::thread; this
#     requires COOP/COEP headers when served (see crosspoint-web).
#   - the demo SD card (one small public-domain epub + minimal .crosspoint cache) is
#     preloaded into MEMFS at /sd via --preload-file.
#   - EXIT_RUNTIME=0 keeps the runtime alive after main() returns so the firmware
#     worker keeps running and the exported functions stay callable.
#   - output is crosspoint_simulator_wasm.{js,wasm,data}, MODULARIZE'd as
#     createSimulator() for clean loading from the browser page.
# -----------------------------------------------------------------------------
if(EMSCRIPTEN)
    # -pthread is already applied at directory scope (top of this file) and inherited here for
    # both compile and link, so it is not repeated on the target.
    set(SIMULATOR_SD_DEMO ${CMAKE_CURRENT_SOURCE_DIR}/sd_root_demo)
    target_link_options(crosspoint_simulator PRIVATE
        -sPTHREAD_POOL_SIZE=6
        -sALLOW_MEMORY_GROWTH=1
        -sINITIAL_MEMORY=64MB
        -sMAXIMUM_MEMORY=512MB
        -sEXIT_RUNTIME=0
        -sMODULARIZE=1
        -sEXPORT_NAME=createSimulator
        -sENVIRONMENT=web,worker
        "-sEXPORTED_RUNTIME_METHODS=ccall,cwrap,UTF8ToString,HEAPU8,FS"
        "-sEXPORTED_FUNCTIONS=_main,_malloc,_free,_simulator_framebuffer_ptr,_simulator_consume_dirty,_simulator_display_width,_simulator_display_height,_simulator_buffer_size,_simulator_inject_button"
        "--preload-file=${SIMULATOR_SD_DEMO}@/sd"
    )
    set_target_properties(crosspoint_simulator PROPERTIES
        OUTPUT_NAME "crosspoint_simulator_wasm"
        SUFFIX ".js")
endif()

