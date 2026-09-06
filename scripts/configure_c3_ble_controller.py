"""Keep Arduino's controller archive consistent with the C3 custom core."""

from pathlib import Path

Import("env")  # noqa: F821

if env.BoardConfig().get("build.mcu") != "esp32c3":
    raise RuntimeError("The Flash BLE controller override is C3-only")
if "CONFIG_BT_CTRL_RUN_IN_FLASH_ONLY=y" not in env.GetProjectOption("custom_sdkconfig", ""):
    raise RuntimeError("The Flash BLE controller requires a matching custom core")

# Pioarduino rebuilds libbt.a but keeps the stock Arduino -lbtdm_app flag.
# Select the matching archive from that same ESP-IDF package, without replacing
# any shared package files (other environments still use the stock controller).
controller = Path(env.PioPlatform().get_package_dir("framework-espidf")) / (
    "components/bt/controller/lib_esp32c3_family/esp32c3/libbtdm_app_flash.a"
)
if not controller.is_file():
    raise RuntimeError(f"Missing C3 Flash controller: {controller}")
env.Replace(LIBS=[env.File(str(controller)) if str(lib) in ("btdm_app", "-lbtdm_app") else lib
                  for lib in env.get("LIBS", [])])

# Match esp_rom/CMakeLists.txt: Flash-only controllers must not use the ROM
# function tables. Arduino's fixed -T flags otherwise override archive symbols
# (including r_ke_init), mixing incompatible Flash and ROM implementations.
rom_tables = {f"esp32c3.rom.{name}.ld" for name in ("bt_funcs", "eco3_bt_funcs", "eco7_bt_funcs")}
flags = iter(env.get("LINKFLAGS", []))
link_flags = []
for flag in flags:
    if flag == "-T":
        script = next(flags)
        if Path(str(script)).name not in rom_tables:
            link_flags.extend([flag, script])
    else:
        link_flags.append(flag)
env.Replace(LINKFLAGS=link_flags)
