"""Record the cast's assets before the m4v look changes anything (step 0): the sha256 of every asset file under
Content/Characters/ and Content/Pokemon/ (meshes, skeletons, clips, materials, textures): sha1 in "files" (what the
test re-hashes with the engine's FSHA1) and sha256 beside it.

    python Tools/look/cast_hashes.py [--out Content/Look/Kit/cast-hashes.json] [--copy OTHER.json]

The M4vCastUnchanged test (Tests/M4VLookTests.cpp) reads the game's copy: every mesh, material and texture Red, Pikachu,
the people and the Pokemon use must be one of these files, and where the project's Content folder can be found each must
still have its sha256. Standard library only.
"""
import argparse
import datetime
import hashlib
import json
import subprocess
import sys
from pathlib import Path

GAME = Path(__file__).resolve().parents[2]


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', default=str(GAME / 'Content/Look/Kit/cast-hashes.json'))
    ap.add_argument('--copy', action='append', default=[])
    args = ap.parse_args(argv)
    content = GAME / 'Content'
    files, sha256 = {}, {}
    for folder in ('Characters', 'Pokemon'):
        for path in sorted((content / folder).rglob('*.uasset')):
            data = path.read_bytes()
            files[path.relative_to(content).as_posix()] = hashlib.sha1(data).hexdigest()   # the engine checks sha1 (FSHA1)
            sha256[path.relative_to(content).as_posix()] = hashlib.sha256(data).hexdigest()
    try:
        revision = subprocess.run(['git', '-C', str(GAME), 'rev-parse', 'HEAD'], capture_output=True, text=True).stdout.strip()
    except OSError:
        revision = ''
    doc = {'about': 'm4v step 0: the cast before the anime look (Tools/look/cast_hashes.py); M4vCastUnchanged checks against it',
           'revision': revision, 'recorded': datetime.datetime.now(datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ'),
           'folders': ['Characters', 'Pokemon'], 'count': len(files), 'files': files, 'sha256': sha256}
    text = json.dumps(doc, indent=1) + '\n'
    for out in [args.out] + args.copy:
        Path(out).parent.mkdir(parents=True, exist_ok=True)
        Path(out).write_text(text, encoding='utf-8')
        print(f'{len(files)} cast files -> {out}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
