"""Find a Python with numpy and the pokeyellow checkout; shared by render.py and verify.py."""
import glob
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
GAME = os.path.dirname(os.path.dirname(HERE))
ROOT = GAME  # the project folder
REPO_URL = 'https://github.com/pret/pokeyellow'
PINNED_COMMIT = 'e89ead154b9968aa50eed9328ff2b38b6c194382'


def ensure_numpy():
    """Re-run this script under a Python that has numpy (Blender's bundled one works)."""
    try:
        import numpy  # noqa: F401
        return
    except ImportError:
        pass
    if os.environ.get('GBMUSIC_REEXEC'):
        sys.exit('gbmusic: numpy is missing and no Python with numpy was found')
    candidates = []
    if os.environ.get('PYR_PYTHON'):
        candidates.append(os.environ['PYR_PYTHON'])
    candidates += sorted(glob.glob(r'C:\Program Files\Blender Foundation\*\*\python\bin\python.exe'))
    for exe in candidates:
        if not os.path.exists(exe):
            continue
        ok = subprocess.run([exe, '-c', 'import numpy'], capture_output=True).returncode == 0
        if ok:
            env = dict(os.environ, GBMUSIC_REEXEC='1')
            sys.exit(subprocess.run([exe, '-B'] + sys.argv, env=env).returncode)
    sys.exit('gbmusic: numpy is missing; set PYR_PYTHON to a Python 3 with numpy')


def _git(*args, cwd=None):
    return subprocess.run(['git'] + list(args), cwd=cwd, capture_output=True, text=True)


def pokeyellow(path=None, clone=True):
    """Return (path, commit) of a pokeyellow checkout, cloning it into .cache/pokeyellow if needed."""
    candidates = [path] if path else []
    candidates.append(os.path.join(ROOT, '.cache', 'pokeyellow'))
    for c in candidates:
        if c and os.path.exists(os.path.join(c, 'audio', 'music', 'pallettown.asm')):
            commit = _git('rev-parse', 'HEAD', cwd=c).stdout.strip()
            return c, commit
    if not clone:
        sys.exit('gbmusic: no pokeyellow checkout found')
    dest = path or os.path.join(ROOT, '.cache', 'pokeyellow')
    print('cloning %s into %s' % (REPO_URL, dest))
    r = subprocess.run(['git', 'clone', REPO_URL, dest])
    if r.returncode:
        sys.exit('gbmusic: git clone failed (%d)' % r.returncode)
    _git('checkout', PINNED_COMMIT, cwd=dest)
    return dest, _git('rev-parse', 'HEAD', cwd=dest).stdout.strip()
