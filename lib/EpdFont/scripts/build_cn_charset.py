#!/usr/bin/env python3
"""
Trim a character pool down to the top-N most frequent characters used in
modern Chinese, ranked via wordfreq's Zipf scale.

Default pool:  lib/EpdFont/scripts/chars_3500_common.txt
               (教育部《现代汉语常用字表》3500 chars, vendored from
               https://github.com/elephantnose/characters/blob/master/
               3500%E5%B8%B8%E7%94%A8%E6%B1%89%E5%AD%97.txt)
Output:        lib/EpdFont/scripts/cn_common_chars.txt
               (top N, single-line UTF-8)

The output file feeds pyftsubset's --text-file= in build-cn-builtin-fonts.sh.

When --top equals (or exceeds) the pool size, all pool chars are kept and
the script effectively only adds --require-from chars to the output. This
is the recommended mode: 3500 common chars covers all of modern Chinese
that the device reasonably needs to render, and the wordfreq cutoff is
only useful when intentionally shrinking below 3500 for tight flash
budgets.

Re-run whenever you want to change the target N or the input pool. The
result is committed so the firmware build is fully reproducible.

Pass --require-from <file> (repeatable) to force-include every CJK Unified
Ideograph found in that file regardless of Zipf rank. Use this with the i18n
YAML files so UI strings always render — the 3500 char list omits some
common modern chars like 浏 (U+6D4F, used in "浏览器") that would otherwise
be silently dropped by the renderer (see EpdFont::getTextBounds when
getGlyph returns nullptr).

Requires:  pip install wordfreq jieba
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

# CJK Unified Ideographs. Punctuation (U+3000–U+303F, U+FF00–U+FFEF) is
# already covered by pyftsubset's --unicodes= argument and does not need to
# be force-included here.
CJK_IDEOGRAPH_RE = re.compile(r"[一-鿿]")

try:
    import wordfreq
except ImportError:
    print("error: wordfreq not installed. Run: pip install wordfreq jieba", file=sys.stderr)
    sys.exit(1)


SCRIPT_DIR = Path(__file__).resolve().parent
SOURCE_FILE = SCRIPT_DIR / "chars_3500_common.txt"
OUTPUT_FILE = SCRIPT_DIR / "cn_common_chars.txt"
# Bonus output: just the --require-from CJK chars, single-line UTF-8. Used by
# build-cn-builtin-fonts.sh to build the tiny 16pt/18pt i18n-only subset OTF.
# Only written when at least one --require-from file is provided.
I18N_OUTPUT_FILE = SCRIPT_DIR / "cn_i18n_chars.txt"


def rank_chars(chars) -> list[tuple[str, float]]:
    """Return [(char, zipf_score)] sorted by descending Zipf frequency."""
    ranked = [(ch, wordfreq.zipf_frequency(ch, "zh")) for ch in chars]
    ranked.sort(key=lambda pair: pair[1], reverse=True)
    return ranked


def load_required(paths: list[Path]) -> set[str]:
    """Extract every CJK Unified Ideograph found in the given files."""
    required: set[str] = set()
    for path in paths:
        if not path.is_file():
            print(f"error: --require-from path not found: {path}", file=sys.stderr)
            sys.exit(1)
        text = path.read_text(encoding="utf-8")
        required.update(CJK_IDEOGRAPH_RE.findall(text))
    return required


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--top",
        type=int,
        default=3500,
        help="Number of characters to keep (default: 3500, the size of the "
        "现代汉语常用字表 pool). Lower this to shrink flash; higher only useful "
        "if --require-from adds chars beyond the pool.",
    )
    parser.add_argument(
        "--require-from",
        action="append",
        default=[],
        metavar="FILE",
        help="Force-include every CJK Unified Ideograph found in FILE "
        "regardless of Zipf rank. Repeatable. Intended for i18n YAML files "
        "so UI strings never silently drop glyphs.",
    )
    parser.add_argument(
        "--review",
        type=int,
        default=50,
        help="Print this many characters just above and just below the cutoff "
        "for hand-review (default: 50)",
    )
    args = parser.parse_args()

    if not SOURCE_FILE.is_file():
        print(f"error: source file not found: {SOURCE_FILE}", file=sys.stderr)
        sys.exit(1)

    raw = SOURCE_FILE.read_text(encoding="utf-8").strip()
    pool_chars = {c for c in raw if not c.isspace()}

    required = load_required([Path(p) for p in args.require_from])
    required_beyond_pool = sorted(required - pool_chars)

    pool = pool_chars | required
    total = len(pool)

    if args.top >= total:
        print(
            f"error: --top {args.top} is >= pool size {total}; nothing to trim",
            file=sys.stderr,
        )
        sys.exit(1)

    if len(required) > args.top:
        print(
            f"error: required set has {len(required)} chars but --top is {args.top}; "
            "raise --top or trim the require-from input",
            file=sys.stderr,
        )
        sys.exit(1)

    ranked = rank_chars(pool)
    required_ranked = [(c, z) for c, z in ranked if c in required]
    non_required_ranked = [(c, z) for c, z in ranked if c not in required]

    remaining_slots = args.top - len(required_ranked)
    non_required_kept = non_required_ranked[:remaining_slots]
    non_required_dropped = non_required_ranked[remaining_slots:]

    kept = required_ranked + non_required_kept

    # Sort kept chars back into a stable order (by codepoint) so file diffs are
    # readable when --top changes by a small amount. The font generation
    # doesn't care about order.
    kept_chars = sorted(c for c, _ in kept)

    OUTPUT_FILE.write_text("".join(kept_chars), encoding="utf-8")

    print(
        f"Wrote {len(kept_chars)} characters to {OUTPUT_FILE.relative_to(SCRIPT_DIR.parent.parent)}",
        file=sys.stderr,
    )

    if required:
        i18n_chars = sorted(required)
        I18N_OUTPUT_FILE.write_text("".join(i18n_chars), encoding="utf-8")
        print(
            f"Wrote {len(i18n_chars)} i18n-only characters to "
            f"{I18N_OUTPUT_FILE.relative_to(SCRIPT_DIR.parent.parent)}",
            file=sys.stderr,
        )
    print(
        f"Pool: {total} chars ({SOURCE_FILE.name} {len(pool_chars)} ∪ required {len(required)})  "
        f"→  kept {len(kept)} ({len(required_ranked)} required + "
        f"{len(non_required_kept)} top-Zipf)  →  dropped {len(non_required_dropped)}",
        file=sys.stderr,
    )

    if required_beyond_pool:
        print("", file=sys.stderr)
        print(
            f"[required] +{len(required_beyond_pool)} chars added beyond {SOURCE_FILE.name}: "
            f"{''.join(required_beyond_pool)}",
            file=sys.stderr,
        )

    if args.review:
        review_n = min(args.review, len(non_required_kept), len(non_required_dropped))
        if review_n > 0:
            print("", file=sys.stderr)
            print(
                f"Last {review_n} kept (lowest-frequency non-required survivors, Zipf descending):",
                file=sys.stderr,
            )
            for ch, z in non_required_kept[-review_n:]:
                print(f"  {ch}  zipf={z:.2f}", file=sys.stderr)
            print("", file=sys.stderr)
            print(
                f"First {review_n} dropped (highest-frequency casualties, Zipf descending):",
                file=sys.stderr,
            )
            for ch, z in non_required_dropped[:review_n]:
                print(f"  {ch}  zipf={z:.2f}", file=sys.stderr)


if __name__ == "__main__":
    main()
