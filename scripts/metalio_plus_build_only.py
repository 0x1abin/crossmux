"""Keep the experimental S31 integration build-only until runtime review."""
from SCons.Errors import UserError
from SCons.Script import COMMAND_LINE_TARGETS

safe = {"buildprog", "size", "clean", "compiledb", "idedata", "__idedata", "envdump", "checkprogsize", "unit-tests"}
blocked = [target for target in COMMAND_LINE_TARGETS if target not in safe]
if blocked:
    raise UserError("Metalio Plus integration is BUILD ONLY; rejected targets: " + ", ".join(blocked) + ". Runtime requires validated input/storage, the checked 200MHz core, and a reviewed startup write/restore plan.")

# Build helpers throughout this repository expect the older prebuilt core graph.
# Do not run its cache-rebuild patch on the independent IDF6.1 platform.
# Vendor include paths remain private to the new backend library.
