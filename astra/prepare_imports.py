# Copyright 2026 AstraEngine contributors. SPDX-License-Identifier: MIT
"""Create MSVC import libraries for the unmodified official MinGW SDK DLLs."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--dumpbin", required=True)
    parser.add_argument("--lib", required=True)
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    manifest = json.loads(Path(__file__).with_name("sdk.json").read_text(encoding="utf-8"))
    runtime = args.sdk / "lib/py3-windows-x86_64"
    for name, expected in manifest["windows_x86_64"].items():
        with (runtime / name).open("rb") as stream:
            actual = hashlib.file_digest(stream, "sha256").hexdigest()
        if actual != expected:
            raise RuntimeError("Expected unmodified official " + manifest["version"] + " SDK: " + name)
    args.output.mkdir(parents=True, exist_ok=True)
    for stem in ("libpython3.12", "librenpython"):
        dll = runtime / (stem + ".dll")
        definition = args.output / (stem + ".def")
        library = args.output / (stem + ".lib")
        if library.exists() and library.stat().st_mtime_ns >= max(dll.stat().st_mtime_ns, Path(__file__).stat().st_mtime_ns):
            continue
        listing = subprocess.run([args.dumpbin, "/NOLOGO", "/EXPORTS", str(dll)],
                                 check=True, capture_output=True, text=True).stdout
        exports = re.findall(r"^\s*\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(\S+)\s*$", listing, re.MULTILINE)
        if not exports:
            raise RuntimeError("No named SDK exports: " + dll.name)
        # CPython exports data as well as functions. The same __imp_ import
        # slots serve both; no DLL internals or second SDL instance are copied.
        definition.write_text("LIBRARY " + dll.name + "\nEXPORTS\n" + "\n".join(exports) + "\n", encoding="ascii")
        subprocess.run([args.lib, "/NOLOGO", "/MACHINE:X64", "/DEF:" + str(definition),
                        "/OUT:" + str(library)], check=True)


if __name__ == "__main__":
    main()
