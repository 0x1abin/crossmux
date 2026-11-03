# Upload only the application image, at the ota_0 offset taken from the
# partition table. For boards that keep their vendor bootloader, partition
# table, otadata and recovery partition (Onyx Picco): pio's default upload
# would also write the Arduino bootloader, partition table and boot_app0.bin.
Import("env")

from SCons.Script import COMMAND_LINE_TARGETS

if set(COMMAND_LINE_TARGETS) & {"erase", "erase_upload", "uploadfs", "uploadfsota"}:
    print("Picco preserves its stock flash layout; only application upload is supported.")
    env.Exit(1)

# The platform builder already captured FLASH_EXTRA_IMAGES in its upload flags
# before this post script runs. Remove those operands as well as the source list.
flags = list(env.get("UPLOADERFLAGS", []))
for address, image in env.get("FLASH_EXTRA_IMAGES", []):
    image = env.subst(image)
    for index in range(len(flags) - 2, -1, -1):
        if flags[index:index + 2] == [address, image]:
            del flags[index:index + 2]
env.Replace(FLASH_EXTRA_IMAGES=[], UPLOADERFLAGS=flags)

# Reject protocols/custom commands whose action captured other flash operands
# (including automatic espota selection for an IP upload port).
if "upload" in COMMAND_LINE_TARGETS and env.get("UPLOADCMD") != \
        '$UPLOADER $UPLOADERFLAGS $ESP32_APP_OFFSET $SOURCE':
    print("Picco application upload requires the standard USB esptool command.")
    env.Exit(1)
