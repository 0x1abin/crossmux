#!/usr/bin/env python3
"""Validate the EEGO A4 template's source, docs, examples, and build matrix."""

from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path


EXPECTED_ENVIRONMENTS = (
    "eego_a4_diagnostics",
    "eego_a4_quickstart",
    "eego_a4_storage_rtc_battery",
    "eego_a4_wifi_ble",
    "eego_a4_safe_sleep",
    "eego_a4_cpfont",
)
REQUIRED_PROJECT_FILES = (
    ".clang-format",
    ".editorconfig",
    ".gitattributes",
    ".github/PULL_REQUEST_TEMPLATE.md",
    ".github/ISSUE_TEMPLATE/bug_report.yml",
    ".github/ISSUE_TEMPLATE/hardware_evidence.yml",
    "CHANGELOG.md",
    "CODE_OF_CONDUCT.md",
    "CONTRIBUTING.md",
    "LICENSE",
    "NOTICE.md",
    "README.en.md",
    "README.md",
    "SECURITY.md",
    "SUPPORT.md",
    "THIRD_PARTY_NOTICES.md",
    "docs/MAINTAINER_GUIDE.md",
    "docs/README.md",
    "docs/VALIDATION.md",
    "requirements.txt",
)
GENERATED_DIRECTORY_NAMES = {
    ".git",
    ".pio",
    ".venv",
    "captures",
    "dist",
    "release",
}
TEMPORARY_DIRECTORY_NAMES = {
    ".mypy_cache",
    ".pytest_cache",
    ".ruff_cache",
    "__pycache__",
}
TEMPORARY_FILE_SUFFIXES = {
    ".bak",
    ".log",
    ".orig",
    ".pyc",
    ".pyo",
    ".rej",
    ".tmp",
}


def fail(message: str) -> None:
    raise ValueError(message)


def read_version(project: Path) -> str:
    hardware = (
        project / "lib/EegoA4Support/include/EegoA4Hardware.h"
    ).read_text(encoding="utf-8")
    header_match = re.search(r'TEMPLATE_VERSION\s*=\s*"([^"]+)"', hardware)
    if not header_match:
        fail("could not parse version from EegoA4Hardware.h")
    return header_match.group(1)


def check_repository_shape(project: Path) -> None:
    for relative in REQUIRED_PROJECT_FILES:
        if not (project / relative).is_file():
            fail(f"required open-source project file is missing: {relative}")

    required_layout = (
        "lib/EegoA4Support/include/EegoA4Hardware.h",
        "lib/EegoA4Ui/include/CpFontRenderer.h",
        "lib/EegoA4Ui/include/PortraitCanvas.h",
        "lib/EegoA4Ui/src/CpFontRenderer.cpp",
        "src/app/DiagnosticApp.cpp",
        "src/app/DiagnosticApp.h",
        "src/main.cpp",
    )
    retired_layout = (
        "include/EegoA4Hardware.h",
        "src/CpFontRenderer.cpp",
        "src/CpFontRenderer.h",
        "src/DiagnosticApp.cpp",
        "src/DiagnosticApp.h",
        "src/PortraitCanvas.h",
    )
    for relative in required_layout:
        if not (project / relative).is_file():
            fail(f"source layout regression: missing {relative}")
    for relative in retired_layout:
        if (project / relative).exists():
            fail(f"source layout regression: obsolete path returned: {relative}")
    if (project / "docs/history").exists():
        fail("public source tree must not contain docs/history")

    ignore = (project / ".gitignore").read_text(encoding="utf-8")
    for marker in (".pio/", "captures/", "release/", "__pycache__/", "*.pyc"):
        if marker not in ignore:
            fail(f".gitignore does not exclude generated content: {marker}")

    for manifest in sorted((project / "lib").glob("*/library.json")):
        try:
            parsed = json.loads(manifest.read_text(encoding="utf-8"))
        except json.JSONDecodeError as error:
            fail(f"invalid library manifest {manifest.relative_to(project)}: {error}")
        for field in ("name", "version", "description", "platforms", "frameworks"):
            if field not in parsed:
                fail(f"{manifest.relative_to(project)} is missing {field!r}")

    unwanted_binaries = [
        path.relative_to(project)
        for path in project.rglob("*")
        if path.is_file()
        and path.suffix.lower() in {".bin", ".elf", ".map", ".raw", ".pbm"}
        and ".git" not in path.parts
        and ".pio" not in path.parts
        and "release" not in path.parts
        and "captures" not in path.parts
    ]
    if unwanted_binaries:
        fail(
            "source tree contains generated/private binary files:\n  "
            + "\n  ".join(map(str, unwanted_binaries))
        )

    oversized = [
        path.relative_to(project)
        for path in project.rglob("*")
        if path.is_file()
        and path.stat().st_size > 1024 * 1024
        and ".git" not in path.parts
        and ".pio" not in path.parts
        and "release" not in path.parts
    ]
    if oversized:
        fail("source tree contains files over 1 MiB:\n  " + "\n  ".join(map(str, oversized)))

    temporary_files: list[Path] = []
    for path in project.rglob("*"):
        relative = path.relative_to(project)
        if any(
            part in GENERATED_DIRECTORY_NAMES or part in TEMPORARY_DIRECTORY_NAMES
            for part in relative.parts
        ):
            continue
        if path.is_file() and (
            path.name == ".DS_Store"
            or path.name.endswith("~")
            or path.suffix.lower() in TEMPORARY_FILE_SUFFIXES
        ):
            temporary_files.append(relative)
    if temporary_files:
        fail(
            "source tree contains temporary/debug artifacts:\n  "
            + "\n  ".join(map(str, temporary_files))
        )

    if (project / ".git").is_dir():
        tracked_output = subprocess.run(
            ["git", "ls-files", "-z"],
            cwd=project,
            check=True,
            capture_output=True,
        ).stdout
        tracked = [
            Path(item.decode("utf-8"))
            for item in tracked_output.split(b"\0")
            if item
        ]
        invalid_tracked = [
            path
            for path in tracked
            if (project / path).exists()
            and (
                "docs/history" in path.as_posix()
                or any(part in GENERATED_DIRECTORY_NAMES for part in path.parts)
                or any(part in TEMPORARY_DIRECTORY_NAMES for part in path.parts)
                or path.name == ".DS_Store"
                or path.name.endswith("~")
                or path.suffix.lower() in TEMPORARY_FILE_SUFFIXES
            )
        ]
        if invalid_tracked:
            fail(
                "Git tracks generated, historical, or temporary artifacts:\n  "
                + "\n  ".join(map(str, invalid_tracked))
            )


def check_text_hygiene(project: Path) -> None:
    text_suffixes = {
        ".cpp",
        ".csv",
        ".h",
        ".ini",
        ".json",
        ".md",
        ".py",
        ".txt",
        ".yaml",
        ".yml",
    }
    private_markers = ("/Users/" + "lakphy", "C:\\Users\\" + "lakphy")
    obsolete_markers = (
        "useSafe" + "Content(",
        "run " + "border",
        "screen " + "border",
        "docs/" + "history",
        "DEVELOPMENT_" + "REVIEW",
        "FINAL_REQUIREMENTS_" + "AUDIT",
        "USER_" + "ACCEPTANCE",
        "COMPLETION_" + "MATRIX",
        "VALIDATION_" + "STATUS",
        "<private-" + "validation-archive>",
    )
    offenders: list[str] = []
    for path in project.rglob("*"):
        if (
            not path.is_file()
            or path.suffix.lower() not in text_suffixes
            or any(part in {".git", ".pio", "release"} for part in path.parts)
        ):
            continue
        text = path.read_text(encoding="utf-8")
        for marker in private_markers:
            if marker in text:
                offenders.append(f"{path.relative_to(project)} contains {marker!r}")
        if path.relative_to(project) != Path("scripts/validate_project.py"):
            for marker in obsolete_markers:
                if marker in text:
                    offenders.append(
                        f"{path.relative_to(project)} contains obsolete marker {marker!r}"
                    )
    if offenders:
        fail(
            "public text hygiene check failed:\n  "
            + "\n  ".join(offenders)
        )


def check_python_scripts(project: Path) -> None:
    for script in sorted((project / "scripts").glob("*.py")):
        source = script.read_text(encoding="utf-8")
        try:
            compile(source, str(script), "exec")
        except SyntaxError as error:
            fail(f"Python syntax error in {script.relative_to(project)}: {error}")


def check_platformio(project: Path) -> None:
    config = (project / "platformio.ini").read_text(encoding="utf-8")
    for environment in EXPECTED_ENVIRONMENTS:
        if f"[env:{environment}]" not in config:
            fail(f"platformio.ini is missing environment {environment}")


def check_examples(project: Path) -> None:
    example_roots = sorted((project / "examples").glob("[0-9][0-9]_*"))
    if len(example_roots) != len(EXPECTED_ENVIRONMENTS) - 1:
        fail(
            f"expected {len(EXPECTED_ENVIRONMENTS) - 1} examples, "
            f"found {len(example_roots)}"
        )
    for example in example_roots:
        source = example / "main.cpp"
        if not source.is_file():
            fail(f"missing example source: {source}")
        text = source.read_text(encoding="utf-8")
        if "eego::holdPower();" not in text:
            fail(f"{source} does not assert the power latch first")
        if "eego::beginStandardHardware(" not in text:
            fail(f"{source} bypasses the standard hardware initializer")
        if "canvas.useUiContentRect();" not in text:
            fail(f"{source} does not opt into the rectangular UI contract")


def check_safe_area_test(project: Path) -> None:
    hardware = (
        project / "lib/EegoA4Support/include/EegoA4Hardware.h"
    ).read_text(encoding="utf-8")
    canvas = (
        project / "lib/EegoA4Ui/include/PortraitCanvas.h"
    ).read_text(encoding="utf-8")
    source = (project / "src/app/DiagnosticApp.cpp").read_text(encoding="utf-8")
    package = (project / "scripts/package.py").read_text(encoding="utf-8")
    safe_area_doc = (project / "docs/SAFE_AREA.md").read_text(encoding="utf-8")
    required_hardware = (
        "DISPLAY_OUTER_RADIUS = 60",
        "SAFE_CONTENT_INSET = 12",
        "DISPLAY_OUTER_RADIUS - SAFE_CONTENT_INSET",
        "SAFE_CONTENT_WIDTH == 528 && SAFE_CONTENT_HEIGHT == 744",
        "isInsideSafeContent",
        "UI_CONTENT_INSET = 28",
        "UI_CONTENT_WIDTH == 496 && UI_CONTENT_HEIGHT == 712",
        "isInsideUiContentRect",
        "static_assert(isInsideSafeContent(UI_CONTENT_X, UI_CONTENT_Y)",
    )
    required_canvas = (
        "enum class DrawingRegion",
        "DrawingRegion::UiContentRect",
        "eego::isInsideUiContentRect(x, y)",
        "void useUiContentRect()",
        "void useFullPanel()",
    )
    required_source = (
        "void DiagnosticApp::renderSafeAreaTest()",
        "inset < eego::SAFE_CONTENT_INSET",
        "eego::DISPLAY_OUTER_RADIUS - inset",
        "ROUNDED SAFE = R48",
        "UI RECT = 496 X 712",
        "canvas_.setCursor(eego::UI_CONTENT_X + 4, eego::UI_CONTENT_Y + 4)",
        "canvas_.useFullPanel();",
        "canvas_.useUiContentRect();",
        'lower == "run safe"',
        'lower == "screen safe"',
    )
    required_package = (
        '"outer_radius_px": 60',
        '"inset_px": 12',
        '"width": 528',
        '"height": 744',
        '"inner_radius_px": 48',
        '"foreground_required": True',
        '"ui_content_rect": {',
        '"inset_px": 28',
        '"width": 496',
        '"height": 712',
        '"radius_px": 0',
        '"recommended_title_origin": [32, 32]',
    )
    required_doc = (
        "x=12, y=12, w=528, h=744, r=48",
        "inner radius = outer radius - inset = 60 - 12 = 48 px",
        "ceil(48 × (1 - 1/√2)) = 15 px",
        "x=28, y=28, w=496, h=712, r=0",
        "canvas.useUiContentRect();",
        "canvas.useFullPanel();",
    )
    for marker in required_hardware:
        if marker not in hardware:
            fail(f"safe-area regression: EegoA4Hardware.h is missing {marker!r}")
    for marker in required_canvas:
        if marker not in canvas:
            fail(f"safe-area regression: PortraitCanvas.h is missing {marker!r}")
    for marker in required_source:
        if marker not in source:
            fail(f"safe-area regression: DiagnosticApp.cpp is missing {marker!r}")
    for marker in required_package:
        if marker not in package:
            fail(f"safe-area regression: package.py is missing {marker!r}")
    for marker in required_doc:
        if marker not in safe_area_doc:
            fail(f"safe-area regression: SAFE_AREA.md is missing {marker!r}")


def check_markdown_links(project: Path) -> int:
    files = [
        path
        for path in sorted(project.rglob("*.md"))
        if not any(part in {".git", ".pio", "release"} for part in path.parts)
    ]
    pattern = re.compile(r"!?\[[^\]]*\]\(([^)]+)\)")
    missing: list[str] = []
    for document in files:
        text = document.read_text(encoding="utf-8")
        for raw_target in pattern.findall(text):
            target = raw_target.strip().strip("<>")
            if not target or target.startswith(
                ("#", "http://", "https://", "mailto:")
            ):
                continue
            path_part = target.split("#", 1)[0]
            candidate = (document.parent / path_part).resolve()
            if not candidate.exists():
                missing.append(f"{document.relative_to(project)} -> {target}")
    if missing:
        fail("broken Markdown links:\n  " + "\n  ".join(missing))
    return len(files)


def find_pio() -> str:
    if executable := shutil.which("pio"):
        return executable
    candidate = Path.home() / ".platformio/penv/bin/pio"
    if candidate.is_file():
        return str(candidate)
    fail("PlatformIO Core not found; install it or add `pio` to PATH")
    raise AssertionError("unreachable")


def build(project: Path, environments: tuple[str, ...]) -> None:
    command = [find_pio(), "run"]
    for environment in environments:
        command.extend(["-e", environment])
    print("Running:", " ".join(command), flush=True)
    subprocess.run(command, cwd=project, check=True)


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Check version synchronization, examples, Markdown links, and "
            "optionally compile every supported PlatformIO environment."
        )
    )
    parser.add_argument(
        "--quick",
        action="store_true",
        help="run static checks only; skip PlatformIO builds",
    )
    parser.add_argument(
        "-e",
        "--environment",
        action="append",
        choices=EXPECTED_ENVIRONMENTS,
        help="build only this environment (repeatable); default builds all",
    )
    args = parser.parse_args()

    project = Path(__file__).resolve().parents[1]
    version = read_version(project)
    check_repository_shape(project)
    check_text_hygiene(project)
    check_python_scripts(project)
    check_platformio(project)
    check_examples(project)
    check_safe_area_test(project)
    markdown_count = check_markdown_links(project)

    print(f"Static validation PASS: version={version}")
    print(f"Examples: {len(EXPECTED_ENVIRONMENTS) - 1}")
    print(f"Markdown files checked: {markdown_count}")

    if not args.quick:
        selected = tuple(args.environment or EXPECTED_ENVIRONMENTS)
        build(project, selected)
        print(f"Build validation PASS: {len(selected)} environment(s)")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"validation failed: {error}", file=sys.stderr)
        raise SystemExit(2)
