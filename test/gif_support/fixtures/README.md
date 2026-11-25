# Metalio GIF crash reproduction

`metalio-gif-crash-sample.epub` is a user-requested reproduction attachment for
the GIF cache-only crash fix. It retains only the cover and author introduction
from the user-provided book: their XHTML/CSS, two JPEGs and the original
170x104 `qm.gif` signature. All retained resources are byte-identical to the
original EPUB. The remaining book chapters are omitted.

On Metalio E-Ink 4 running CrossMux 2.0.0, the user twice encountered a PANIC and
automatic reboot when turning to the original book's author introduction page.
Converting the original book's two GIFs to PNG allowed the user to pass the same
page. That is physical validation of a workaround, not of the firmware patch.

To reproduce the affected path, copy this sample under a fresh path so it has no
existing image cache, enable normal image display, and turn from the cover to
the author introduction. The original GIF converter writes to the lent-out
framebuffer during cache-only predecode. The fixed converter should finish the
pixel cache without touching the framebuffer, then render the page normally.

The trimmed sample has passed ZIP/XML and resource checks; physical-reader
verification of this exact trimmed file and the firmware patch remains pending.
The synthetic host fixture in `GifFramebufferTest.cpp` independently exercises
the same cache-only contract without depending on book contents.

The original crash report is included in the PR description. The full book is
not committed or published.
