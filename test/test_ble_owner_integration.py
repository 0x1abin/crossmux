"""Compile production BLE compatibility gates, legacy mapping and WiFi handoff with fault injection."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def method(source, signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def run():
    source = (ROOT / 'src/BleInput.cpp').read_text()
    header = (ROOT / 'src/BleInput.h').read_text()
    constants = header[header.index('enum class StartContext'):header.index('// The module owns')]
    snapshot = source[source.index('struct MemorySnapshot'):source.index('MemorySnapshot readMemory()')]
    failure = source[source.index('enum class GateFailure'):source.index('GateFailure gateFailure(')]
    sdk = (ROOT / 'freeink-sdk/libs/network/BleKeyboardHost/include/BleKeyboardHost.h').read_text()
    keys = sdk[sdk.index('enum class SpecialKey'):sdk.index('// One button edge')]
    network = method((ROOT / 'src/NetworkStartup.cpp').read_text(), 'bool setMode(')
    settings = (ROOT / 'src/CrossPointSettings.cpp').read_text()
    migration = settings[settings.index('  if (!bleturner::readJson(ble, doc))'):settings.index('\n  if (needsResave) {\n    LOG_DBG("CPS", "Resaving')]
    harness = r'''
#include <cassert>
#include <cstring>
#include <vector>
#include "BlePageTurnerJson.h"
#include "BleKeyMapping.h"
#define LOG_ERR(...) ((void)0)
namespace freeink {
''' + keys + r'''
}
struct Settings { bleturner::Config ble; bleinput::KeyMap bleKeyMap; } SETTINGS;
namespace bleinput {
''' + constants + snapshot + failure + r'''
MemorySnapshot memory{};
MemorySnapshot readMemory() { return memory; }
void reportStartFailure(GateFailure, StartContext, const MemorySnapshot&, const MemorySnapshot&) {}
int probes = 0, frees = 0;
bool external = true, allocOk = true;
void* nimble_platform_mem_malloc(size_t size) { assert(size == 16); ++probes; return allocOk ? &probes : nullptr; }
void nimble_platform_mem_free(void* p) { assert(p == &probes); ++frees; }
bool esp_ptr_external_ram(void*) { return external; }
''' + method(source, 'GateFailure gateFailure(') + '\n' + method(source, 'bleturner::Why validateStart(') + '\n' + method(source, 'bool encodeKey(') + '\n' + method(source, 'bleturner::Action legacyKeyAction(') + r'''
bool stopped = false;
int stops = 0;
bool stop() { ++stops; return stopped; }
}
struct GfxRenderer {};
enum wifi_mode_t { WIFI_OFF, WIFI_STA, WIFI_AP };
int prepared = 0, wifiStarts = 0;
struct { bool result = true; bool mode(wifi_mode_t) { ++wifiStarts; return result; } } WiFi;
namespace NetworkStartup {
void logMemory(const char*) {}
void prepare(GfxRenderer&) { ++prepared; }
''' + network + r'''
}
void loadBle(JsonVariantConst doc, uint8_t& bluetoothEnabled, bleturner::Config& ble) {
  bool needsResave = false;
''' + migration + r'''
  (void)needsResave;
}
int main() {
  using bleturner::Why;
  using namespace bleinput;
  memory = {kReaderMinFreeInternal, kReaderMinLargestInternal, 0, kMinFreePsram, kMinLargestPsram, 0, kMinFreePsram};
  assert(validateStart(false, false) == Why::Ok);
  --memory.freeInternal;
  assert(validateStart(false, false) == Why::HeapLow);
  ++memory.freeInternal;
  --memory.largestInternal;
  assert(validateStart(false, false) == Why::HeapInPieces);
  memory.freeInternal = kExplicitMinFreeInternal;
  memory.largestInternal = kExplicitMinLargestInternal;
  assert(validateStart(true, false) == Why::Ok);
#if CROSSPOINT_BLE_HOST_PSRAM
  assert(probes == frees);
  external = false;
  assert(validateStart(true, false) == Why::HeapLow && probes == frees);
  external = true;
  allocOk = false;
  assert(validateStart(true, false) == Why::HeapLow);
  allocOk = true;
  --memory.freePsram;
  assert(validateStart(true, false) == Why::HeapLow);
  ++memory.freePsram;
  --memory.largestPsram;
  assert(validateStart(true, false) == Why::HeapLow);
  ++memory.largestPsram;
#endif
#if FREEINK_DEVICE_READPICO
  constexpr size_t post = 20 * 1024;
#elif CROSSPOINT_BLE_HOST_PSRAM
  constexpr size_t post = 24 * 1024;
#else
  constexpr size_t post = 32 * 1024;
#endif
  memory.largestInternal = post;
  assert(validateStart(false, true) == Why::Ok);
  --memory.largestInternal;
  assert(validateStart(false, true) == Why::HeapInPieces);
  SETTINGS.ble.enabled = 1;
  const bleturner::Action actions[] = {bleturner::Action::NextPage, bleturner::Action::PrevPage,
    bleturner::Action::Confirm, bleturner::Action::Back, bleturner::Action::Up,
    bleturner::Action::Down, bleturner::Action::Left, bleturner::Action::Right};
  for (uint8_t i = 0; i < 8; ++i) {
    SETTINGS.bleKeyMap = {};
    assert(assign(SETTINGS.bleKeyMap, 0, static_cast<uint8_t>(freeink::SpecialKey::Right), static_cast<Action>(i)));
    assert(legacyKeyAction(0x4F, 0) == actions[i]);
    assert(legacyKeyAction(0x4F, 1) == bleturner::Action::None);
    assert(legacyKeyAction(0x50, 0) == bleturner::Action::None);
  }
  SETTINGS.ble.enabled = 0;
  assert(legacyKeyAction(0x4F, 0) == bleturner::Action::None);
  for (const char* json : {"{}", "{\"blePageTurnerEnabled\":0}", "{\"blePageTurnerEnabled\":1}"}) {
    for (uint8_t legacy : {uint8_t{0}, uint8_t{1}}) {
      JsonDocument doc;
      assert(!deserializeJson(doc, json));
      bleturner::Config ble;
      uint8_t old = legacy;
      loadBle(doc.as<JsonVariantConst>(), old, ble);
      const uint8_t expected = doc["blePageTurnerEnabled"].isNull() ? legacy : doc["blePageTurnerEnabled"].as<uint8_t>();
      assert(ble.enabled == expected && old == expected);
    }
  }
  GfxRenderer renderer;
  assert(!NetworkStartup::setMode(renderer, WIFI_STA));
  assert(stops == 1 && prepared == 0 && wifiStarts == 0);
  stopped = true;
  assert(NetworkStartup::setMode(renderer, WIFI_AP));
  assert(stops == 2 && prepared == 1 && wifiStarts == 1);
  WiFi.result = false;
  assert(!NetworkStartup::setMode(renderer, WIFI_STA));
  stopped = false;
  WiFi.result = true;
  assert(NetworkStartup::setMode(renderer, WIFI_OFF));
  assert(stops == 3 && wifiStarts == 3);
}
'''
    with tempfile.TemporaryDirectory(prefix='ble-owner-') as tmp:
        tmp = Path(tmp)
        cpp = tmp / 'check.cpp'
        cpp.write_text(harness)
        for pico, psram in ((0, 0), (0, 1), (1, 1)):
            binary = tmp / f'check-{pico}-{psram}'
            subprocess.run(['c++', '-std=c++20', '-DFREEINK_CAP_BLE_HID_HOST=1',
                            f'-DFREEINK_DEVICE_READPICO={pico}', f'-DCROSSPOINT_BLE_HOST_PSRAM={psram}',
                            '-I', str(ROOT / 'src'), '-I', str(ROOT / 'lib/BlePageTurner/include'),
                            '-I', str(ROOT / 'freeink-sdk/libs/network/BleKeyboardHost/include'),
                            '-I', os.environ.get('ARDUINOJSON_INCLUDE', str(ROOT / '.pio/libdeps/default/ArduinoJson/src')),
                            str(cpp), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
    print('BLE owner: C3/S3/ReadPico gates, allocator faults, eight legacy actions, settings precedence and WiFi handoff passed')


if __name__ == '__main__':
    run()
