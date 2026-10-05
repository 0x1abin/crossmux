"""Pin where the vector (TTF) glyph cache may and may not be dropped.

TTF faces only exist on PSRAM boards, so the host unit tests stub them out and
cannot reach this behaviour. These are source-level assertions, in the same
spirit as the other hardware-only guards in this directory.

The contract: a TTF glyph cache converges on the book's working set and is
hard-bounded by its own byte/glyph cap, so a per-render clearCache() must leave
it alone -- dropping it per render forces the whole page back through
FreeType's streamed SD reads. Memory pressure goes through
releaseResidentCaches() instead, which actually frees the arenas.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
FCM = ROOT / 'lib/GfxRenderer/FontCacheManager.cpp'
RENDERER = ROOT / 'lib/GfxRenderer/GfxRenderer.cpp'
TTF = ROOT / 'lib/EpdFont/TtfEpdFont.cpp'


def body(source: str, signature: str) -> str:
    """Return the text of one function definition, from signature to its close."""
    start = source.index(signature)
    depth = 0
    for i in range(start, len(source)):
        if source[i] == '{':
            depth += 1
        elif source[i] == '}':
            depth -= 1
            if depth == 0:
                return source[start:i + 1]
    raise AssertionError(f'unbalanced braces after {signature}')


class TtfGlyphCachePersistenceTest(unittest.TestCase):
    def test_per_render_clear_leaves_vector_fonts_alone(self):
        clear = body(FCM.read_text(), 'void FontCacheManager::clearCache()')
        # The per-render reset is for the caches recycled by design.
        self.assertIn('fontDecompressor_->clearCache()', clear)
        self.assertIn('sdCardFonts_', clear)
        # ...and must not touch the vector faces.
        self.assertNotIn('ttfFonts_', clear,
                         'clearCache() must not drop TTF glyphs; they are not a per-render cache')

    def test_memory_pressure_still_releases_vector_fonts(self):
        release = body(FCM.read_text(), 'void FontCacheManager::releaseSdFontCaches()')
        self.assertIn('ttfFonts_', release)
        self.assertIn('releaseResidentCaches()', release)

    def test_renderer_registers_ttf_eviction_on_the_release_path(self):
        source = RENDERER.read_text()
        sink = source[source.index('gfx.ttfGlyphArenas'):]
        sink = sink[:sink.index('}}')]
        self.assertIn('releaseResidentCaches()', sink,
                      'the TTF eviction sink must free arenas, not just drop glyphs')

    def test_reload_and_family_change_invalidate_the_cache(self):
        # Point-size change: load() flushes every face itself.
        load = body(TTF.read_text(), 'bool TtfEpdFont::load(')
        self.assertIn('flushFace(f)', load)
        # Family change: SdCardFontSystem unloads the object, taking the cache with it.
        system = (ROOT / 'src/SdCardFontSystem.cpp').read_text()
        load_family = body(system, 'void SdCardFontSystem::loadTtfFamily(')
        self.assertRegex(load_family, re.compile(r'ttf_\.reset\(\)|unloadTtf\('),
                         'a new family must replace the TtfEpdFont, not reuse its glyphs')


if __name__ == '__main__':
    unittest.main()
