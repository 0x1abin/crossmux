import hashlib
import importlib.util
import shutil
import sys
from pathlib import Path


def cache_matches(requested, cached_request):
    return bool(requested.strip()) and requested.strip() == cached_request.strip()


if "--prepare-platform" in sys.argv:
    from platformio.package.manager.platform import PlatformPackageManager
    from platformio.project.config import ProjectConfig

    config = ProjectConfig.get_instance()
    package = PlatformPackageManager().install(config.get("env:default", "platform"), skip_dependencies=True)
    platform_file = Path(package.path) / "platform.py"
    source = platform_file.read_text(encoding="utf-8")
    old_tools = 'COMMON_IDF_PACKAGES = [\n    "tool-cmake",\n    "tool-ninja",\n    "tool-scons",'
    new_tools = old_tools.replace('\n    "tool-scons",', "")
    # The platform installer must not replace the Core's running SCons package.
    if old_tools in source:
        platform_file.write_text(source.replace(old_tools, new_tools, 1), encoding="utf-8")
    elif new_tools not in source:
        raise RuntimeError("Unsupported pioarduino IDF tool list")
    raise SystemExit(0)


if "--self-test" in sys.argv:
    assert cache_matches("CONFIG_A=y\nCONFIG_B=n", "CONFIG_A=y\nCONFIG_B=n")
    assert not cache_matches("CONFIG_A=y", "CONFIG_A=y\nCONFIG_B=n")
    raise SystemExit(0)


Import("env")  # noqa: F821 -- provided by PlatformIO

platform = env.PioPlatform()
builder = Path(platform.get_dir()) / "builder" / "frameworks" / "arduino.py"
source = builder.read_text(encoding="utf-8")
# The package already stores libraries per chip; only its cache-presence check is global.
old_check = (
    "flag_any_custom_sdkconfig = (FRAMEWORK_LIB_DIR is not None and \n"
    '                            exists(str(Path(FRAMEWORK_LIB_DIR) / "sdkconfig")))'
)
new_check = '''flag_any_custom_sdkconfig = (
    FRAMEWORK_LIB_DIR is not None
    and exists(str(Path(FRAMEWORK_LIB_DIR) / chip_variant / "sdkconfig.orig"))
)'''

# pioarduino 55.03.39 removed trailing whitespace from the same expression.
if old_check not in source:
    old_check = old_check.replace("and \n", "and\n")

if old_check in source:
    builder.write_text(source.replace(old_check, new_check, 1), encoding="utf-8")
elif new_check not in source:
    raise RuntimeError("Unsupported pioarduino cache check")

# pioarduino's tool-scons package is trimmed on some releases (missing
# SCons.Tool.FortranCommon / SCons.Scanner.Fortran, imported lazily at link
# time). If a complete pip SCons is present, fill only the missing files into
# the package SCons that PlatformIO already loaded.
def _fill_missing_scons_modules():
    import SCons

    loaded = Path(SCons.__file__).resolve().parent
    for entry in sys.path:
        candidate = Path(entry) / "SCons"
        try:
            if not (candidate / "__init__.py").is_file():
                continue
            if candidate.resolve() == loaded:
                continue
        except OSError:
            continue
        for src in candidate.rglob("*.py"):
            dst = loaded / src.relative_to(candidate)
            if not dst.exists():
                dst.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(src, dst)
        print(f"Filled missing SCons modules from {candidate}")
        return


_fill_missing_scons_modules()

# pioarduino 55.03.37 hands a NodeList to this build middleware when a source
# filter matches several files; the stock callback calls node.srcnode()
# directly and aborts with AttributeError. Handle both shapes.
espidf = Path(platform.get_dir()) / "builder" / "frameworks" / "espidf.py"
source = espidf.read_text(encoding="utf-8")
old_skip = (
    "def _skip_prj_source_files(node):\n"
    "    project_src_resolved = Path(PROJECT_SRC_DIR).resolve()\n"
    "    node_path_resolved = Path(node.srcnode().get_path()).resolve()\n"
)
new_skip = (
    "def _skip_prj_source_files(node):\n"
    "    # CrossMux: 55.03.37 hands a NodeList here for multi-file filters.\n"
    "    if not hasattr(node, \"srcnode\"):\n"
    "        kept = [_skip_prj_source_files(n) for n in node]\n"
    "        kept = [n for n in kept if n is not None]\n"
    "        return type(node)(kept) if kept else None\n"
    "    project_src_resolved = Path(PROJECT_SRC_DIR).resolve()\n"
    "    node_path_resolved = Path(node.srcnode().get_path()).resolve()\n"
)
if old_skip in source:
    espidf.write_text(source.replace(old_skip, new_skip, 1), encoding="utf-8")
elif new_skip not in source:
    raise RuntimeError("Unsupported pioarduino src-filter callback")

# The hybrid pass clears BUILD_FLAGS before building the project libraries.
# Keep the -D macros (device selection, wolfSSL features, expat config) so
# library translation units see the same configuration as the application.
old_hybrid_flags = (
    "        PROJECT_SRC_DIR=PROJECT_SRC_DIR,\n"
    "        BUILD_FLAGS=\"\",\n"
)
new_hybrid_flags = (
    "        PROJECT_SRC_DIR=PROJECT_SRC_DIR,\n"
    "        BUILD_FLAGS=[_f for _f in env.get(\"BUILD_FLAGS\", [])"
    " if str(_f).startswith(\"-D\")],\n"
)
if old_hybrid_flags in source:
    espidf.write_text(
        espidf.read_text(encoding="utf-8").replace(
            old_hybrid_flags, new_hybrid_flags, 1
        ),
        encoding="utf-8",
    )
elif "startswith(\"-D\")" not in source:
    raise RuntimeError("Unsupported pioarduino hybrid build flags")

# 55.03.37 library envs do not reliably inherit project BUILD_FLAGS, so
# FREEINK_*/WOLFSSL_* guarded headers are missing during library compilation.
# Mirror every -D flag into CPPDEFINES on the project env (library envs clone
# it later). Idempotent via the _mirror_defines marker.
arduino_file = Path(platform.get_dir()) / "builder" / "frameworks" / "arduino.py"
text = arduino_file.read_text(encoding="utf-8")
old_env = (
    "env = DefaultEnvironment()\n"
    "pm = ToolPackageManager()\n"
    "platform = env.PioPlatform()\n"
)
new_env = (
    old_env
    + "\n"
    + "_mirror_defines = []\n"
    + "for _flag in env.get(\"BUILD_FLAGS\", []):\n"
    + "    if not isinstance(_flag, str) or not _flag.startswith(\"-D\"):\n"
    + "        continue\n"
    + "    _macro = _flag[2:]\n"
    + "    if \"=\" in _macro:\n"
    + "        _key, _value = _macro.split(\"=\", 1)\n"
    + "        _mirror_defines.append((_key, _value))\n"
    + "    else:\n"
    + "        _mirror_defines.append(_macro)\n"
    + "if _mirror_defines:\n"
    + "    env.Append(CPPDEFINES=_mirror_defines)\n"
)
if old_env in text:
    arduino_file.write_text(text.replace(old_env, new_env, 1), encoding="utf-8")
elif "_mirror_defines" not in text:
    raise RuntimeError("Unsupported pioarduino arduino env init")

requested = env.GetProjectOption("custom_sdkconfig", "")
board = env.BoardConfig()
mcu = board.get("build.mcu", "esp32")
chip_variant = board.get("build.chip_variant", "").lower() or mcu
component_spec = importlib.util.spec_from_file_location(
    "crosspoint_pio_components", builder.with_name("component_manager.py")
)
components = importlib.util.module_from_spec(component_spec)
component_spec.loader.exec_module(components)
fingerprint_fn = getattr(components, "board_memory_fingerprint", None)
memory_fingerprint = fingerprint_fn(env, board) if fingerprint_fn else ""
cache_request = requested.strip() + memory_fingerprint if requested.strip() else ""
framework_libs = Path(platform.get_package_dir("framework-arduinoespressif32-libs"))
target_sdkconfig = framework_libs / chip_variant / "sdkconfig"
original_sdkconfig = framework_libs / chip_variant / "sdkconfig.orig"
request_file = framework_libs / chip_variant / "sdkconfig.crosspoint"

rebuild = bool(requested.strip())
if original_sdkconfig.is_file():
    cached_request = (
        request_file.read_text(encoding="utf-8") if request_file.is_file() else ""
    )
    if cache_matches(cache_request, cached_request):
        rebuild = False
        marker = "# TASMOTA__" + hashlib.md5(
            (requested.strip() + mcu + memory_fingerprint).encode("utf-8")
        ).hexdigest()[:16]
        defaults = Path(env.subst("$PROJECT_DIR")) / "sdkconfig.defaults"
        lines = (
            defaults.read_text(encoding="utf-8").splitlines()
            if defaults.exists()
            else []
        )
        if not lines or lines[0] != marker:
            defaults.write_text(
                "\n".join([marker, *lines[1:]]) + "\n", encoding="utf-8"
            )
            print(f"Restored cached Arduino framework for {mcu}")
    elif requested.strip():
        # Recompile only this chip from its original template after config changes.
        original_sdkconfig.replace(target_sdkconfig)
    # A prebuilt target must retain sdkconfig.orig: PlatformIO uses it to
    # reinstall the original libraries, not just their configuration header.

if rebuild:
    # IDF component environments clone application flags before post hooks.
    # Keep SDK/bootstrap objects non-LTO: GCC merges incompatible DRAM_ATTR
    # sections otherwise. The child Arduino pass reads the original project
    # flags again once these libraries are cached, retaining application LTO.
    env.Replace(
        BUILD_FLAGS=[flag for flag in env.get("BUILD_FLAGS", []) if not str(flag).startswith("-flto")],
        BUILD_UNFLAGS=[flag for flag in env.get("BUILD_UNFLAGS", []) if flag != "-fno-lto"],
    )

if rebuild and not board.get("build.esp-idf.sdkconfig_path", ""):
    # Resolved Kconfig values override sdkconfig.defaults, even across SDK upgrades.
    # Only reset the generated file for this environment; explicit paths are user-owned.
    resolved = Path(env.subst("$PROJECT_DIR")) / f"sdkconfig.{env.subst('$PIOENV')}"
    if resolved.is_file() and "# Automatically generated file. DO NOT EDIT." in (
        resolved.read_text(encoding="utf-8").splitlines()[:5]
    ):
        resolved.unlink()
        print(f"Reset generated SDK configuration: {resolved.name}")

if request_file.parent.is_dir():
    request_file.write_text(cache_request + "\n", encoding="utf-8")
else:
    # C61 is assembled from framework-arduino-c61-skeleton-lib by espidf.py
    # after this pre-script runs. Defer the marker until that directory exists.
    print(f"Deferred framework cache marker for {mcu}: {request_file.parent} is not prepared yet")
