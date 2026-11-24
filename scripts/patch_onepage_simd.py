"""Disable S3-only SIMD assembly in PNGdec/JPEGDEC for the OnePage C61 build."""

Import("env")  # noqa: F821

from pathlib import Path

if env.subst("$PIOENV") == "onepage":  # noqa: F821
    libdeps = Path(env.subst("$PROJECT_DIR")) / ".pio" / "libdeps" / "onepage"  # noqa: F821
    for name in ("PNGdec", "JPEGDEC"):
        src = libdeps / name / "src"
        if not src.is_dir():
            continue
        for asm in src.glob("s3_simd_*.S"):
            disabled = asm.with_name(asm.name + ".disabled")
            if disabled.exists():
                asm.unlink()
            else:
                asm.rename(disabled)
            print(f"Disabled S3 SIMD source for C61: {asm.name}")