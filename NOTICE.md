# Notices and provenance

This is an independent community project. It is not produced, endorsed, or
supported by EEGO, Xteink, CrossLink, or their respective owners. Product and
project names may be trademarks of their owners.

## Source code

The project contains an MIT-licensed subset of the FreeInk SDK plus original
EEGO A4 board-support, diagnostics, examples, documentation, and host tooling.
The applicable MIT terms are in [`LICENSE`](LICENSE). Individual source files
also describe important upstream lineages where relevant.

## Binary-analysis inputs

Hardware facts were cross-checked against three user-supplied, unencrypted
ESP32-S3 images. The images themselves are intentionally not included:

| Research input | SHA-256 |
|---|---|
| CrossLink full image | `79c4cdbae8fbb66e8ec586b61ac1209561bee83068627b69fd1f657d970c1483` |
| CrossLink v1.0.10 app image | `43264c93c8e371db6f2e44574027e50d9444dcff40cc6a645828d2558d70cf24` |
| EEGO official v1.2.7 image | `37e121af158cf63ca4483d79d4e66a0537f153c7b0d1c0a02a7acc74753f059c` |

`lib/InputManager/src/gsl/EegoA4GslFirmware.h` and
`lib/FreeInkDisplay/src/lut/Uc8279cA4Luts.h` contain binary-derived
interoperability data. The GSL table can be reproduced from a lawful,
user-supplied image with `scripts/extract_eego_a4_gsl_firmware.py`.

The repository does not grant rights to third-party firmware, fonts, product
artwork, or trademarks. Before redistributing binary-derived tables, a
publisher should independently assess the applicable law and the source
material's terms. This notice documents provenance; it is not legal advice.
