# Metalio Plus image and cover grayscale

User delegates design and unattended implementation; no device work or new dependency installation.
Evidence: ImageBlock.cpp:373,388,430,479 fixes visible rendering to four-level PXC; PNG decoder.cpp:513,668,692 has no Native16 writer; Epub.cpp:975–1063 writes monochrome thumbnails. UiListActivity.cpp:332–364 repaints the whole frame; HomeActivity.cpp:748–815 has BW-only cover caching. GfxRenderer.cpp:2178 clears only BW, so native frame rebuilds must clear both planes.

Use existing native frame loan for Plus image pages and selected cover UI. Decode PNG/JPEG native targets without PXC; never change the existing binary layout. Keep older panel behavior and image-BW preference. Native text with images supports both four-bit and two-bit fonts through existing pixel mirroring. Existing four-level GIF fallback is retained and documented.

Create independent Plus gray BMP cache names and explicit Gray8 converter outputs, preserving existing output defaults and cache formats. Standard BMP rows add bounded per-row storage, no screen-sized heap buffer. Native frame covers must fully redraw; prohibit BW-only saved-cover reuse. Scope UI frame ownership with a stack guard that cancels on early return. Failed hardware commit has no immediate fallback refresh. Begin/decode failure may fall back before hardware work.

Acceptance is host decoder/renderer/cache tests and compile checks first; actual gray tones, cover menus, native image/text combinations, sleep image and rotations wait for the user. No SD writes/uploads/flash, implicit commits or pushes.
