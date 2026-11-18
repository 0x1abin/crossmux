# Metalio Plus isolated build recovery

Fourth requested subsystem, after IMU/native16/images. User delegates details and previously authorized the same fixed project/tool dependencies inside the isolated dev directory. Diagnose from real logs and preserve existing tools/caches before reversible repairs. Do not upgrade versions, install new dependency categories, modify global packages, relax critical flags, change SDK vendor content or connect/flash a device.

Evidence: external plus-precommit-build-20261112.log begins with tool-esp_install5.3.5 and idf_tools quiet failure; ultimately attempts to install the already-present framework-espidf into an existing directory. Installed platform.py:480–507 captures only a quiet child's final empty tail; inspect the actual tool invocation and package identity before choosing a repair.

Recover reproducible cached S31/IDF6.1/200MHz PSRAM builds; run existing static checks and appropriate shared-target checks. Preserve exact binaries/maps/ELFs/source hashes and pending physical acceptance. Record unavoidable baseline failures without inventing full CI success. One fresh whole-increment code review after compilation.
