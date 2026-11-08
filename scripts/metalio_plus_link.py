"""Apply reader panic/validation hooks only after IDF cloned the bootloader env."""

Import("env")

env.Append(LINKFLAGS=[env["METALIO_PLUS_READER_WRAPS"], "-Wl,--wrap=nvs_flash_init"])
