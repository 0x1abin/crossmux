"""Compile a build-local SDK copy compatible with pinned NimBLE-Arduino 2.3.8."""

from pathlib import Path

Import("env")  # noqa: F821

METHOD = """  uint32_t onPassKeyDisplay(NimBLEConnInfo&) override {
    const uint32_t passkey = NimBLEDevice::getSecurityPasskey();
    self().onPairingPasskey(passkey);
#if FREEINK_BLE_HID_SCAN_DEBUG
    Serial.printf("[BleHid] pairing passkey: %06lu\\n", static_cast<unsigned long>(passkey));
#endif
    return passkey;
  }
"""


def write_compatible_source(target, source, env):
    original = Path(source[0].get_abspath()).read_text(encoding="utf-8")
    if original.count(METHOD) != 1:
        raise RuntimeError("Unsupported BleKeyboardHost passkey callback; check the SDK/NimBLE pins")
    output = Path(target[0].get_abspath())
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(original.replace(METHOD, "", 1), encoding="utf-8")


def compile_compatible_host(build_env, node):
    compatible = build_env.Command(
        "$BUILD_DIR/ble-compat/BleKeyboardHost.cpp", node, write_compatible_source
    )
    build_env.Depends(compatible, build_env.subst("$PROJECT_DIR/scripts/patch_ble_keyboard_host.py"))
    shim = build_env.subst("$PROJECT_DIR/src/platform/BtLibraryInUseShim.h")
    config = build_env.GetProjectOption("custom_nimble_config", "")
    config_flags = []
    if config:
        config = str(Path(build_env.subst("$PROJECT_DIR")) / config)
        config_flags = ["-include", config]
    compiled = build_env.Object(
        compatible,
        CPPPATH=build_env.get("CPPPATH", []) + [str(Path(node.get_abspath()).parent)],
        # Custom-core bootstrap omits application sources, so the existing weak
        # flag must travel with this library in both bootstrap and final links.
        CCFLAGS=build_env.get("CCFLAGS", [])
        + ["-include", shim] + config_flags,
    )[0]
    build_env.Depends(compiled, shim)
    if config:
        build_env.Depends(compiled, config)
    build_env.Depends(compiled, build_env.subst("$PROJECT_DIR/src/platform/BleIpcStack.h"))
    return compiled


env.AddBuildMiddleware(compile_compatible_host, "*/BleKeyboardHost/src/BleKeyboardHost.cpp")
