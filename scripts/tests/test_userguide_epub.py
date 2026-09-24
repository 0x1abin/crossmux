#!/usr/bin/env python3
"""Validate the actual bundled archives, including links and reproducible bytes."""
import io
from pathlib import Path
import posixpath
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
import zipfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import generate_userguide_epub as generator


class UserGuideEpubTest(unittest.TestCase):
    def test_bundled_epubs(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            header = output / 'guide.generated.h'
            generator.build_bundled(ROOT / 'docs/user-guide', output, header)
            before = header.read_bytes()
            total = 0
            for language, filename, title in (
                ('zh-CN', 'CrossMux用户手册.epub', 'CrossMux用户手册'),
                ('en', 'CrossMux User Guide.epub', 'CrossMux User Guide'),
            ):
                data = (output / filename).read_bytes()
                total += len(data)
                with zipfile.ZipFile(io.BytesIO(data)) as archive:
                    self.assertIsNone(archive.testzip())
                    first = archive.infolist()[0]
                    self.assertEqual(first.filename, 'mimetype')
                    self.assertEqual(first.compress_type, zipfile.ZIP_STORED)
                    self.assertEqual(archive.read(first), b'application/epub+zip')
                    ns = {'o': 'http://www.idpf.org/2007/opf',
                          'dc': 'http://purl.org/dc/elements/1.1/',
                          'c': 'urn:oasis:names:tc:opendocument:xmlns:container',
                          'x': 'http://www.w3.org/1999/xhtml'}
                    container = ET.fromstring(archive.read('META-INF/container.xml'))
                    opf_path = container.find('c:rootfiles/c:rootfile', ns).get('full-path')
                    opf = ET.fromstring(archive.read(opf_path))
                    self.assertEqual(opf.find('o:metadata/dc:title', ns).text, title)
                    self.assertEqual(opf.find('o:metadata/dc:language', ns).text, language)
                    items = {item.get('id'): item for item in opf.findall('o:manifest/o:item', ns)}
                    for item in items.values():
                        self.assertRegex(item.get('id'), r'^[A-Za-z_][\w.-]*$')
                        self.assertIn('OEBPS/' + item.get('href'), archive.namelist())
                        self.assertNotIn('font', item.get('media-type'))
                    spine = [item.get('idref') for item in opf.findall('o:spine/o:itemref', ns)]
                    self.assertEqual(len(spine), 6)
                    self.assertNotIn('nav', spine)
                    self.assertTrue(all(item in items for item in spine))
                    self.assertIsNone(opf.find('o:metadata/o:meta[@name="cover"]', ns))
                    self.assertNotIn('OEBPS/cover.png', archive.namelist())
                    for name in archive.namelist():
                        if name.endswith(('.xml', '.opf', '.xhtml')):
                            document = ET.fromstring(archive.read(name))
                            if name.endswith('.xhtml'):
                                self.assertEqual(document.get('{http://www.w3.org/XML/1998/namespace}lang'), language)
                            for element in document.iter():
                                for attr in ('href', 'src'):
                                    link = element.get(attr)
                                    if link and not link.startswith(('http:', 'https:', '#')):
                                        target = posixpath.normpath(posixpath.join(posixpath.dirname(name), link))
                                        self.assertIn(target.split('#')[0], archive.namelist())
                    nav = ET.fromstring(archive.read('OEBPS/nav.xhtml'))
                    self.assertEqual(len(nav.findall('.//x:nav/x:ol/x:li', ns)), len(spine))
            self.assertLessEqual(total, generator.BUNDLED_BUDGET)
            generator.build_bundled(ROOT / 'docs/user-guide', output, header)
            self.assertEqual(before, header.read_bytes())


if __name__ == '__main__':
    unittest.main()
