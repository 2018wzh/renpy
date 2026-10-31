"""Generate the host-owned display module, matching Ren'Py's static naming."""
import pathlib
import re
import subprocess
import sys

source = pathlib.Path(sys.argv[1])
output = pathlib.Path(sys.argv[2])
subprocess.run([
    sys.executable, "-m", "cython", "-3", "-X", "profile=False",
    "-X", "embedsignature=True", "-X", "embedsignature.format=python",
    "-I", str(source), "-I", str(source / "src"),
    "-I", str(source / "src/pygame/include"),
    str(source / "renpy/pygame/display.pyx"), "-o", str(output),
], check=True)
if sys.argv[3] == "ON":
    # Same module-definition and init-symbol rewrites as scripts/setuplib.py.
    code = output.read_text(encoding="utf-8")
    code, definitions = re.subn(
        r'(__pyx_moduledef.*?")display"', r'\1renpy.pygame.display"',
        code, count=1, flags=re.DOTALL,
    )
    code, initializers = re.subn(
        r"^__Pyx_PyMODINIT_FUNC PyInit_display\b",
        "__Pyx_PyMODINIT_FUNC PyInit_renpy_pygame_display",
        code, flags=re.MULTILINE,
    )
    if definitions != 1 or initializers == 0:
        raise RuntimeError("Cython display output does not match Ren'Py's builtin layout")
    output.write_text(code, encoding="utf-8")
