#!/usr/bin/env python3
"""Report CJK characters used by books/dictionaries that the built-in font lacks.

CrossMux's Chinese firmware ships a fixed 8/10/12pt CJK subset
(lib/EpdFont/scripts/cn_common_chars.txt, ~3.5k chars) for the UI, book titles,
chapter lists and dictionary headwords. Book *content* is drawn with the SD-card
reader font, so text can look fine there while a title shows blanks.

This tool finds exactly which characters your library needs beyond that subset,
so a firmware font patch can add them (roughly 210 B of flash per character
across the 8/10/12pt headers) instead of guessing.

Usage:
    python scan_cjk_coverage.py <path>... [--charset FILE] [--write-charset FILE]

<path> may be a book folder, a dictionary folder, or an SD-card root. It is
scanned recursively for:
  *.epub   - dc:title, dc:creator, TOC entries (nav/ncx) and h1-h3 headings
  *.idx    - StarDict headwords (uncompressed index)
  *.txt    - plain-text titles are skipped; TXT books have no title metadata

Nothing is modified unless --write-charset is given. No device access needed.
"""

from __future__ import annotations

import argparse
import re
import sys
import zipfile
from pathlib import Path

CJK_RE = re.compile(r"[\u3400-\u4dbf\u4e00-\u9fff\uf900-\ufaff]")
TAG_RE = re.compile(r"<[^>]+>")
H_RE = re.compile(r"<h[1-3][^>]*>(.*?)</h[1-3]>", re.I | re.S)
NCX_TEXT_RE = re.compile(r"<text[^>]*>(.*?)</text>", re.I | re.S)
NAV_A_RE = re.compile(r"<a[^>]*>(.*?)</a>", re.I | re.S)
DC_RE = {
    "title": re.compile(r"<dc:title[^>]*>(.*?)</dc:title>", re.I | re.S),
    "creator": re.compile(r"<dc:creator[^>]*>(.*?)</dc:creator>", re.I | re.S),
}

DEFAULT_CHARSET = Path(__file__).resolve().parents[1] / "lib" / "EpdFont" / "scripts" / "cn_common_chars.txt"

# Measured on the shipped headers: bytes per added character, per point size.
BYTES_PER_CHAR = {8: 52, 10: 71, 12: 88}


def clean(text: str) -> str:
    text = TAG_RE.sub("", text)
    return text.replace("&amp;", "&").replace("&lt;", "<").replace("&gt;", ">").replace("&quot;", '"')


def scan_epub(path: Path, hits: dict[str, int], sources: dict[str, set[str]]) -> str:
    """Collect CJK from title/author/TOC/headings. Returns a short status note."""
    try:
        with zipfile.ZipFile(path) as zf:
            names = zf.namelist()
            texts: list[tuple[str, str]] = []
            for name in names:
                low = name.lower()
                if low.endswith((".opf", ".ncx")) or "nav" in low and low.endswith((".xhtml", ".html")):
                    try:
                        texts.append((name, zf.read(name).decode("utf-8", "ignore")))
                    except Exception:
                        pass
            # Headings only: chapter titles rendered in the chapter list.
            html_count = 0
            for name in names:
                low = name.lower()
                if low.endswith((".xhtml", ".html")) and html_count < 400:
                    try:
                        texts.append((name, zf.read(name).decode("utf-8", "ignore")))
                        html_count += 1
                    except Exception:
                        pass
    except Exception as exc:
        return f"skip ({exc.__class__.__name__})"

    found = 0
    for name, body in texts:
        low = name.lower()
        strings: list[str] = []
        if low.endswith(".opf"):
            for rx in DC_RE.values():
                strings += [clean(m) for m in rx.findall(body)]
        elif low.endswith(".ncx"):
            strings += [clean(m) for m in NCX_TEXT_RE.findall(body)]
        elif "nav" in low:
            strings += [clean(m) for m in NAV_A_RE.findall(body)]
        else:
            strings += [clean(m) for m in H_RE.findall(body)]
        for s in strings:
            for ch in CJK_RE.findall(s):
                found += 1
                hits[ch] = hits.get(ch, 0) + 1
                sources.setdefault(ch, set()).add(path.name)
    return f"{found} CJK chars in titles/toc"


def scan_idx(path: Path, hits: dict[str, int], sources: dict[str, set[str]]) -> str:
    """StarDict word index: UTF-8 word, NUL, 4-byte offset, 4-byte size."""
    try:
        data = path.read_bytes()
    except Exception as exc:
        return f"skip ({exc.__class__.__name__})"
    total = 0
    for raw in data.split(b"\x00"):
        if not raw:
            continue
        try:
            word = raw.decode("utf-8")
        except UnicodeDecodeError:
            continue
        for ch in CJK_RE.findall(word):
            total += 1
            hits[ch] = hits.get(ch, 0) + 1
            sources.setdefault(ch, set()).add(path.name)
    return f"{total} CJK chars in headwords"


def main() -> int:
    ap = argparse.ArgumentParser(description="Find CJK chars missing from the built-in firmware font.")
    ap.add_argument("paths", nargs="+", help="folders to scan (book library, dictionary folder, SD root)")
    ap.add_argument("--charset", default=str(DEFAULT_CHARSET), help="current built-in charset file")
    ap.add_argument("--write-charset", metavar="FILE", help="write the missing chars here (one line, UTF-8)")
    ap.add_argument("--include-latin-punct", action="store_true", help="also list non-CJK symbols that may be missing")
    args = ap.parse_args()

    charset = Path(args.charset)
    if not charset.is_file():
        print(f"error: charset file not found: {charset}", file=sys.stderr)
        return 2
    known = set(charset.read_text(encoding="utf-8").strip())
    print(f"built-in font covers {len(known)} chars ({charset.name})")

    hits: dict[str, int] = {}
    sources: dict[str, set[str]] = {}
    files = {"epub": 0, "idx": 0}
    for root in args.paths:
        p = Path(root)
        if not p.exists():
            print(f"warning: not found: {p}")
            continue
        candidates = [p] if p.is_file() else list(p.rglob("*"))
        for f in candidates:
            if not f.is_file():
                continue
            suffix = f.suffix.lower()
            if suffix == ".epub":
                files["epub"] += 1
                note = scan_epub(f, hits, sources)
                print(f"  epub {f.name}: {note}")
            elif suffix == ".idx":
                files["idx"] += 1
                note = scan_idx(f, hits, sources)
                print(f"  dict {f.parent.name}/{f.name}: {note}")

    missing = {ch: n for ch, n in hits.items() if ch not in known}
    if not missing:
        print(f"\nOK: no missing glyphs across {files['epub']} epub / {files['idx']} dictionary files.")
        return 0

    ordered = sorted(missing.items(), key=lambda kv: (-kv[1], kv[0]))
    print(f"\nmissing: {len(ordered)} unique chars across {files['epub']} epub / {files['idx']} dictionary files")
    est = sum(sum(BYTES_PER_CHAR.values()) for _ in ordered)
    print(f"estimated flash cost for 8/10/12pt: {est} bytes ({est/1024:.1f} KiB)\n")
    for ch, n in ordered[:80]:
        where = ", ".join(sorted(sources[ch])[:2])
        print(f"  {ch}  x{n}  ({where})")
    if len(ordered) > 80:
        print(f"  ... {len(ordered) - 80} more")

    text = "".join(ch for ch, _ in ordered)
    if args.write_charset:
        Path(args.write_charset).write_text(text, encoding="utf-8")
        print(f"\nwrote {len(text)} chars to {args.write_charset}")
    else:
        print(f"\nall missing chars:\n{text}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
