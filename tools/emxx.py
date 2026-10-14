# em++ for the MSYS2 Emscripten package. Its emcc/em++/emar .exe launchers are refused by Windows on this
# machine, so tools that would be spawned through them are spawned through .bat shims instead.
import os
import sys

ROOT = r'C:\msys64\ucrt64\lib\emscripten'
PYTHON = sys.executable
SHIMS = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'shims')

sys.path.insert(0, ROOT)
from tools import utils

original_find_exe = utils.find_exe


def find_exe(*parts):
    path = os.path.join(*parts)
    script = path + '.py'
    if not os.path.abspath(path).startswith(ROOT) or not os.path.isfile(script):
        return original_find_exe(*parts)
    os.makedirs(SHIMS, exist_ok=True)
    shim = os.path.join(SHIMS, os.path.basename(path) + '.bat')
    if not os.path.isfile(shim):
        with open(shim, 'w') as f:
            f.write(f'@"{PYTHON}" -E "{script}" %*\n')
    return shim


utils.find_exe = find_exe

import emcc
from tools import shared

shared.run_via_emxx = True
sys.exit(emcc.main(sys.argv))
