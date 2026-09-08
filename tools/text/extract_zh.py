#!/usr/bin/env python3
# Extract per-script dialogue text pairs (original Japanese vs han-patch text)
# from both mes.ARC files, aligned by byte offset (the patch is an equal-length
# SJIS replacement, so text at the same offset pairs 1:1).  Writes:
#   work/zh-work/<SCRIPT>.tsv   (offset<TAB>japanese<TAB>patch)
import struct, subprocess, sys, os

# Original and han-patch mes.ARC paths come from argv / env (do NOT hard-code
# an author-local path here).  Output goes to work/zh-work under cwd by default.
def _opt(name, idx, default):
    if idx < len(sys.argv) and not sys.argv[idx].startswith('-'):
        return os.path.abspath(sys.argv[idx])
    return os.environ.get(name, default)

# legacy: <originalArc> <patchArc> [workDir] (positional), else env with defaults
_dw = os.path.join('work', 'zh-work')
ARC_O = _opt('KAWA_ARC_O', 1, os.path.join('mes.ARC'))
ARC_H = _opt('KAWA_ARC_H', 2, '')
WORK  = _opt('KAWA_ZH_WORK', 3, _dw)
os.makedirs(WORK, exist_ok=True)

def sjis_texts(b):
    out = []
    i = 0
    while i < len(b):
        if b[i] == 0x00:
            i += 1; continue
        j = i; toks = []
        while j < len(b):
            c = b[j]
            if c == 0x00:
                break
            if 0x81 <= c <= 0x9f or 0xe0 <= c <= 0xfc:
                if j + 1 < len(b) and 0x40 <= b[j + 1] <= 0xfc:
                    toks.append(bytes([c, b[j + 1]])); j += 2; continue
                else:
                    break
            if 0x20 <= c <= 0x7e:
                toks.append(bytes([c])); j += 1; continue
            break
        if len(toks) >= 2:
            try:
                out.append((i, b''.join(toks).decode('shift_jis')))
            except Exception:
                pass
        i = j + 1 if j > i else i + 1
    return out

def get_script(arc, name, path):
    subprocess.run(['./get_script', arc, name, path], capture_output=True)

def main():
    lst = subprocess.run(['./list_arc', ARC_O], capture_output=True, text=True).stdout
    names = [l.split()[1] for l in lst.strip().split('\n') if l]
    total = 0
    for nm in names:
        po = f'/tmp/zk-o'; ph = f'/tmp/zk-h'
        get_script(ARC_O, nm, po); get_script(ARC_H, nm, ph)
        to = sjis_texts(open(po, 'rb').read())
        th = sjis_texts(open(ph, 'rb').read())
        with open(os.path.join(WORK, nm + '.tsv'), 'w', encoding='utf-8') as f:
            # align by offset: both lists sorted by offset already
            do = {off: t for off, t in to}
            dh = {off: t for off, t in th}
            for off, t in to:
                patch = dh.get(off, '')
                f.write(f'{off}\t{t}\t{patch}\n')
                total += 1
    print(f'wrote {total} lines for {len(names)} scripts to {WORK}')

if __name__ == '__main__':
    main()
