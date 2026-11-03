#!/usr/bin/env python3
"""Publish the validated CJK output set, restoring all previous outputs on failure."""
from pathlib import Path
import argparse
import shutil
import tempfile


def publish(staging, destination, font_ids=None):
    pairs = [(staging / f'notosans_cjk_{size}.h', destination / f'notosans_cjk_{size}.h')
             for size in (8, 10, 12, 14, 16, 18)]
    pairs.append((staging / 'notosans_cjk_common_intervals.h', destination / 'notosans_cjk_common_intervals.h'))
    pairs += [(staging / name, destination.parent / 'scripts' / name)
              for name in ('cn_common_chars.txt', 'cn_i18n_chars.txt')]
    if font_ids is not None:
        pairs.append((staging / 'fontIds.h', font_ids))
    if any(not src.is_file() or src.stat().st_size == 0 for src, _ in pairs):
        raise ValueError('Incomplete CJK output set')
    backup = Path(tempfile.mkdtemp(prefix='.cjk-backup-', dir=destination.parent))
    previous = []
    changed = []
    try:
        for index, (_, target) in enumerate(pairs):
            saved = backup / str(index)
            present = target.exists()
            if present:
                shutil.copy2(target, saved)
            previous.append((target, saved, present))
        try:
            for src, target in pairs:
                src.replace(target)
                changed.append(target)
        except BaseException:
            recovered = True
            for target, saved, present in reversed(previous):
                if target not in changed:
                    continue
                try:
                    if present:
                        saved.replace(target)
                    else:
                        target.unlink()
                except OSError:
                    recovered = False
            if not recovered:
                raise RuntimeError(f'CJK publication rollback failed; originals retained at {backup}')
            raise
    finally:
        # Retain backups if any recovery rename failed.
        if all(not saved.exists() or target not in changed for target, saved, _ in previous):
            shutil.rmtree(backup)
        elif len(changed) == len(pairs):
            shutil.rmtree(backup)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('staging', type=Path)
    parser.add_argument('destination', type=Path)
    parser.add_argument('--font-ids', type=Path, help='Publish the staged font IDs with the font outputs')
    args = parser.parse_args()
    publish(args.staging, args.destination, args.font_ids)
