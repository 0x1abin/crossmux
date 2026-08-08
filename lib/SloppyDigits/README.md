# SloppyDigits

`SloppyDigits` is CrossMux's allocation-free procedural digit renderer. The
sloppy clock chooses a random `Style`; the Chinese calendar and Electronic
Woodfish use fixed styles and deterministic seeds.

Include `SloppyDigits.h`, configure an `AlphabetId` and `Style`, call
`prepareSeeds()` once for stable output, then pass digit text and a `Bounds` to
`draw()`. `rollStyle()` provides the clock's seeded random configuration.

The renderer accepts up to four newline-separated rows and ten digits per row;
other characters are ignored. Geometry tables and artwork are compile-time
constants. Drawing uses the caller's `GfxRenderer`, performs no file access,
and allocates no framebuffer, container, or heap block.
