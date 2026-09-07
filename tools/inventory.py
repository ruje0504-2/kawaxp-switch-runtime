#!/usr/bin/env python3
"""Validate KAWAXP raw ARC/AWF indexes without changing game files."""
import argparse, hashlib, json, struct
from pathlib import Path

def inspect(path):
    with path.open('rb') as f:
        n, = struct.unpack('<I', f.read(4))
        stride = 52 if path.suffix.lower() == '.awf' else 40
        size = path.stat().st_size
        if not 0 < n <= 100000 or 4 + n * stride > size:
            raise ValueError(f'{path.name}: invalid index')
        index = f.read(n * stride)
        end = 4 + n * stride
        entries = []
        for i in range(n):
            row = index[i * stride:(i + 1) * stride]
            name = row[:32].split(b'\0', 1)[0].decode('cp932')
            offset, length = struct.unpack_from('<II', row, 32)
            if offset < end or offset + length > size:
                raise ValueError(f'{path.name}:{name}: overlapping/out-of-range entry')
            end = offset + length
            f.seek(offset)
            entry = dict(name=name, offset=offset, size=length, signature=f.read(min(12,length)).hex())
            if stride == 52:
                entry['loop_start'], entry['loop_end'] = struct.unpack_from('<II', row, 40)
            entries.append(entry)
        f.seek(0)
        digest = hashlib.file_digest(f, 'sha256').hexdigest()
    return dict(name=path.name, bytes=size, sha256=digest, count=n, entries=entries)

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('game_dir', type=Path)
    p.add_argument('output', type=Path)
    args = p.parse_args()
    names=['mes.ARC','gcc.ARC','sequence.ARC','voice.ARC','bgm.AWF','effect.AWF','effect2.AWF','effect3.AWF']
    result = [inspect(args.game_dir / name) for name in names]
    args.output.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
    for r in result: print(f"{r['name']}: {r['count']} entries, index valid")
if __name__ == '__main__': main()
