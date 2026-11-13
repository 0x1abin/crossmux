#pragma once

// IDF 6.1's TF-PSA-Crypto moved the legacy SHA-256 declarations. The pinned
// Arduino RC1 still exports the same implementation/symbols. Keep existing
// callers and older IDF builds on their original API while the port is tested.
#if !__has_include(<mbedtls/sha256.h>) && __has_include(<mbedtls/private/sha256.h>)
// TF-PSA-Crypto gates these declarations even though the pinned archive still
// exports the five exact legacy symbols. Limit the opt-in to this include.
#ifndef MBEDTLS_DECLARE_PRIVATE_IDENTIFIERS
#define MBEDTLS_DECLARE_PRIVATE_IDENTIFIERS
#define CROSSMUX_SHA256_RESTORE_PRIVATE_DECLARATIONS
#endif
#include <mbedtls/private/sha256.h>
#ifdef CROSSMUX_SHA256_RESTORE_PRIVATE_DECLARATIONS
#undef MBEDTLS_DECLARE_PRIVATE_IDENTIFIERS
#undef CROSSMUX_SHA256_RESTORE_PRIVATE_DECLARATIONS
#endif
#else
// Keep the public API as the default, including host analyzers that deliberately
// omit framework headers. A real build without either header fails this include.
#include <mbedtls/sha256.h>
#endif
