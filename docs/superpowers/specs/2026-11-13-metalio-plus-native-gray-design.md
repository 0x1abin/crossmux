# Metalio Plus native sixteen-level grayscale

Second subsystem in the user's explicit unattended-development order. Continue
after IMU software development; no physical writes or acceptance until return.
Engineering choices are delegated. No dependency/commit/push is implicit.

## Evidence and selected approach

MetalioPlusEpd already owns a native 415,872-byte front plane and a committed
back plane. Its checked commit uses GC16 for Full/unknown history and GL16 for
ordinary gray. GfxRenderer.cpp:1804 already supports low-nibble-first 4bpp native
targets, full-frame B/W proxy mirroring and 4-bit glyphs. The Plus driver currently
exposes only two overlay masks (0/5/10/15), while PanelDriver's native entrypoint
returns nullptr. ImageViewer and custom sleep images already have native callers.

Borrow the existing front plane, clear it white at begin, and commit exactly once.
No second driver framebuffer, copied vendor waveform, new library or task. Native
16 and overlay masks are mutually exclusive transactions; the completed back
plane is never replaced on cancellation. The four-gray overlay API stays valid
for two-bit fonts and older callers. Raise only the Plus profile to 16 levels.

## Refresh and compatibility contract

Default native commits request Full for existing image callers. Add a compatible
mode-aware virtual extension with fallback to the existing method, keeping every
other driver unchanged. A separate native-refresh-cycle capability defaults false
and is true only on Plus; existing Combined metadata is not enough because the
legacy Epdiy driver is Combined too. HAL and renderer expose the capability and
mode overload without application SDK access.

On Plus, mode Full/unknown uses clear+GC16; Half/Fast native requests use GL16,
never DU. Every request carries its own mode. Success alone commits the new
physical baseline. Failure keeps unknown history and existing power-fault
inhibition; no cleanup or fallback immediately retries failed hardware work.
After native gray, first B/W Fast uses GL16, then ordinary B/W can use DU.
Sleep/resync cancels pending data and requires physical clean on next update.

EPUB's existing four-bit-font path uses a transactional reader cadence helper:
prepare a local counter, request the matching native mode, publish the counter
only after success. Manual refresh and periodic Full remain honored. Legacy
drivers keep their old default native commit and counter=1 behavior. Begin failure
before panel work may fall back to B/W; a failed Plus hardware commit returns a
failed render with next-page Full debt instead of immediately submitting B/W.

## Scope boundary and acceptance

This subsystem supplies the backend, refresh intent and four-bit glyph path.
Existing image-viewer/custom-sleep native callers become eligible, but their
failure handling and other cover/EPUB image integrations are reviewed in the
next requested subsystem. Two-bit `.cpfont` files still provide four-level text;
sixteen-level text requires an existing four-bit/vector face, not invented font
precision. No user SD font or setting is changed while away.

Tests compile the actual driver/backend and facade: sixteen distinct levels,
packing, one activation, native/overlay/BW transitions, cancel/restart/resync,
Full/GL16 cadence, power/draw/off failure, preserved back plane and legacy virtual
fallback. Renderer host checks cover mirroring, native fonts/images and mode
dispatch. Record host/build/physical status separately. Native memory mode,
optical separation, ghosting, 30-page cadence and rotated text/images remain
pending physical acceptance, regardless of successful host tests.
