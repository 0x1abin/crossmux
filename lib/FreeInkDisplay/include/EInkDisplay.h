#pragma once

// Compatibility shim.
//
// The display driver was rebuilt as freeink::FreeInkDisplay with a per-controller
// driver architecture. This alias preserves the original `#include <EInkDisplay.h>`
// include path and the `EInkDisplay` type name (plus every nested enum, constant,
// and method, which a type alias surfaces automatically) so existing firmware —
// e.g. CrossPoint's HalDisplay referencing EInkDisplay::DISPLAY_WIDTH,
// EInkDisplay::RefreshMode, EInkDisplay::GRAY_PLANE_LSB — builds unchanged.

#include "FreeInkDisplay.h"

using EInkDisplay = freeink::FreeInkDisplay;
