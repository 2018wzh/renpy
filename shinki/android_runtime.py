#!/usr/bin/env python3
"""Build the pinned official Ren'Py Android archives for static Manager embedding."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tarfile
import threading
import urllib.request
import zipfile

SDK_FILE = Path(__file__).resolve().with_name("sdk.json")
SDK = json.loads(SDK_FILE.read_text(encoding="utf-8"))
PYTHON_SOURCES = SDK_FILE.parent.parent / "licenses/renpy/python-sources.json"
BUILD_REVISION = SDK["android"]["renpy_build"]
SOURCE_REVISION = SDK["source_revision"]
NDK_SHA256 = SDK["android"]["ndk_sha256"]
PYTHON_VERSION = SDK["android"]["python"]
ARCHITECTURES = {"aarch64-linux-android": "arm64_v8a", "x86_64-linux-android": "x86_64"}


def capture(*args, **kwargs):
    return subprocess.check_output(args, text=True, **kwargs).strip()


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def stage(root, destination, target):
    arch = ARCHITECTURES[target]
    install = root / "tmp" / f"install.android-{arch}"
    for name in ("lib/libpython3.12.a", "lib/librenpy.a", "lib/libSDL2.a", "include/python3.12/Python.h"):
        if not (install / name).is_file():
            raise RuntimeError(f"Official Android build omitted {install / name}")
    destination.mkdir(parents=True, exist_ok=False)
    shutil.copytree(install / "include", destination / "include")
    with tarfile.open(root / f"source/Python-{PYTHON_VERSION}.tar.xz") as source:
        license_file = source.extractfile(f"Python-{PYTHON_VERSION}/LICENSE")
        if license_file is None:
            raise RuntimeError("Pinned CPython source omitted its license")
        with license_file:
            (destination / "LICENSE-CPython.txt").write_bytes(license_file.read())
    with zipfile.ZipFile(root / "tars/CubismSdkForNative-4-r.6.2.zip") as source:
        (destination / "LICENSE-Cubism-Headers.md").write_bytes(
            source.read("CubismSdkForNative-4-r.6.2/Core/LICENSE.md"))
    libraries = destination / "lib"
    libraries.mkdir()
    for archive in sorted((install / "lib").glob("*.a")):
        shutil.copy2(archive, libraries / archive.name)
    tools = root / "tmp/cross.android/android-ndk-r29/toolchains/llvm/prebuilt/linux-x86_64/bin"
    archives = sorted(libraries.glob("*.a"))
    # One process contains SDL3/FFmpeg from other cores. Keep this independently
    # versioned runtime private, including every reference within its archives.
    symbols = set()
    for archive in archives:
        output = capture(str(tools / "llvm-nm"), "--defined-only", "--extern-only", "--format=posix", str(archive))
        for line in output.splitlines():
            fields = line.split()
            if len(fields) < 2:
                continue
            name, kind = fields[:2]
            if kind.upper() not in {"T", "D", "B", "R", "S", "G", "C", "W", "V"}:
                continue
            if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name):
                continue
            if name.startswith(("__", "Py", "_Py")) or name == "init_librenpy":
                continue
            symbols.add(name)
    mapping = destination / "symbols.map"
    mapping.write_text("".join(f"{name} shinki_renpy_{name}\n" for name in sorted(symbols)), encoding="utf-8")
    for archive in archives:
        subprocess.run([str(tools / "llvm-objcopy"), f"--redefine-syms={mapping}", str(archive)], check=True)
        subprocess.run([str(tools / "llvm-ranlib"), str(archive)], check=True)
    (destination / "include/shinki-renpy-symbols.h").write_text(
        "/* Private symbols of the pinned Android runtime. */\n#pragma once\n"
        + "".join(f"#define {name} shinki_renpy_{name}\n" for name in sorted(symbols)
                  if not name.startswith("_Z")), encoding="utf-8")
    pkgconfig = libraries / "pkgconfig"
    pkgconfig.mkdir()
    (pkgconfig / "shinki-renpy-runtime.pc").write_text(
        "Name: shinki-renpy-runtime\nDescription: Pinned private Ren'Py Android runtime\nVersion: 8.5.3\n"
        "Libs: -lGLESv1_CM -lGLESv2 -lEGL -lOpenSLES -llog -landroid -ldl -lz -lm\n", encoding="utf-8")
    resources = destination / "Resources/lib"
    resources.mkdir(parents=True)
    shutil.copytree(install / "lib/python3.12", resources / "python3.12",
                    ignore=shutil.ignore_patterns("__pycache__", "*.pyc", "site-packages", "config-*"))
    target_metadata = destination / "Resources/lib" / target / "python3.12"
    target_metadata.mkdir(parents=True)
    shutil.move(resources / "python3.12/_sysconfigdata__linux_.py", target_metadata)
    # Upstream's hostpython pip task may install host-native acceleration wheels
    # into the target tree. Use the SDK's inventoried pure Python artifacts,
    # including their exact corresponding sources, instead.
    inventory = json.loads(PYTHON_SOURCES.read_text(encoding="utf-8"))
    cache = root / "tmp/shinki-python-cache"
    cache.mkdir(parents=True, exist_ok=True)
    packages = []
    for record in inventory["tools"] + inventory["packages"]:
        artifacts = [record]
        if "wheel" in record:
            artifacts.append(record["wheel"])
        for artifact in artifacts:
            path = cache / artifact["file_name"]
            if not path.exists():
                with urllib.request.urlopen(artifact["url"], timeout=600) as response, path.open("xb") as output:
                    shutil.copyfileobj(response, output)
            if digest(path) != artifact["sha256"]:
                raise RuntimeError(f"Pinned Python artifact checksum mismatch: {path}")
        packages.append(cache / record.get("wheel", record)["file_name"])
    # Use one host interpreter path for both ABIs, including generated script
    # shebangs/RECORD entries. Build tools remain the inventoried pure wheels.
    tool_root = root / "tmp/shinki-python-tools"
    tool_root.mkdir(parents=True, exist_ok=True)
    for tool in inventory["tools"]:
        with zipfile.ZipFile(cache / tool["file_name"]) as archive:
            archive.extractall(tool_root)
    python_env = os.environ.copy()
    python_env["PYTHONPATH"] = str(tool_root)
    python_env["PYTHONDONTWRITEBYTECODE"] = "1"
    subprocess.run([str(root / "tmp/host/bin/python3"), "-m", "pip", "install", "--no-index", "--no-deps",
                    "--no-build-isolation", "--no-compile", "--target",
                    str(resources / "python3.12/site-packages"), *map(str, packages)], env=python_env, check=True)
    for path in resources.rglob("*"):
        if path.suffix in {".so", ".pyd", ".dll", ".dylib"}:
            raise RuntimeError(f"Static target Python resources contain a dynamic module: {path}")
    receipt = {
        "target": target, "renpy_build": BUILD_REVISION, "renpy_source": SOURCE_REVISION,
        "ndk_sha256": NDK_SHA256, "private_symbols": len(symbols),
        "builder_sha256": digest(Path(__file__)),
        "sdl_patch_sha256": digest(Path(__file__).parent / "android/SDL2-offscreen-default-display.patch"),
        "sdk_sha256": digest(SDK_FILE),
        "python_sources_sha256": digest(PYTHON_SOURCES),
        "archives": {p.name: digest(p) for p in archives},
    }
    (destination / "runtime.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-root", required=True, type=Path)
    parser.add_argument("--target", required=True, choices=ARCHITECTURES)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--host-python", type=Path, default=Path(sys.executable))
    args = parser.parse_args()
    root = args.build_root.resolve()
    output_root = args.output.resolve()
    recipe = Path(__file__).resolve().parent
    host_python = args.host_python.absolute()
    if sys.platform != "linux":
        parser.error("the official Android source recipes require a provisioned Linux host")
    for checkout, revision in ((root, BUILD_REVISION), (root / "renpy", SOURCE_REVISION)):
        if capture("git", "-C", str(checkout), "rev-parse", "HEAD") != revision:
            parser.error(f"source revision mismatch: {checkout}, expected {revision}")
    if digest(root / "tars/android-ndk-r29-linux.zip") != NDK_SHA256:
        parser.error("official Android NDK r29 archive checksum mismatch")
    cubism = root / "tars/CubismSdkForNative-4-r.6.2.zip"
    if digest(cubism) != SDK["android"]["cubism_sha256"]:
        parser.error("official Cubism header SDK archive checksum mismatch")
    cubism_header = root / f"tmp/install.android-{ARCHITECTURES[args.target]}/cubism/Core/include/Live2DCubismCore.h"
    if not cubism_header.is_file():
        (root / f"tmp/complete/build-live2d.android-{ARCHITECTURES[args.target]}").unlink(missing_ok=True)
    if output_root.exists():
        parser.error("output must be new; preserve previous verified runtime outputs")
    os.chdir(root)
    sys.path.insert(0, str(root))
    os.environ["MAKEFLAGS"] = "-j3"
    os.environ["CMAKE_BUILD_PARALLEL_LEVEL"] = "3"
    host_tools = root / "tmp/shinki-host-tools"
    host_tools.mkdir(parents=True, exist_ok=True)
    cython = host_tools / "cython"
    cython.write_text(f"#!/bin/sh\nexec {shlex.quote(str(host_python))} -m cython \"$@\"\n", encoding="utf-8")
    cython.chmod(0o755)
    os.environ["PATH"] = str(host_tools) + os.pathsep + os.environ["PATH"]
    os.cpu_count = lambda: 3
    import renpybuild.run
    from renpybuild.task import annotator
    # Only the real offscreen EGL backend owns a hidden backing context. The
    # Manager's NativeActivity continues to own Android presentation and input.
    @annotator
    def offscreen(context):
        if context.name == "sdl2" and context.platform == "android":
            context.var("configure", "./configure --enable-video-offscreen")
        if context.name == "librenpy":
            context.include("{{ renpy }}/tmp/gen3-static")
    import tasks
    from renpybuild.task import tasks as native_tasks
    # Manager supplies NativeActivity and the APK. RAPT's standalone Java/APK
    # packaging rows are not prerequisites of the native archive closure.
    standalone_packaging = {("rapt", "copy"), ("sdl2", "rapt"), ("pyjnius", "rapt")}
    native_tasks[:] = [task for task in native_tasks
                       if (task.name, task.task) not in standalone_packaging]
    unpack = next(task for task in native_tasks if task.name == "sdl2" and task.task == "unpack")
    original_unpack = unpack.function
    def unpack_offscreen(context):
        original_unpack(context)
        if context.platform == "android":
            subprocess.run(["patch", "-p1", "-i", str(recipe / "android/SDL2-offscreen-default-display.patch")],
                           cwd=context.cwd, check=True)
    unpack.function = unpack_offscreen
    slots = threading.Semaphore(3)
    original_init = renpybuild.run.RunCommand.__init__
    original_run = renpybuild.run.RunCommand.run
    def bounded_init(self, command, context):
        slots.acquire()
        try:
            original_init(self, command, context)
        except BaseException:
            slots.release()
            raise
    def bounded_run(self):
        try:
            original_run(self)
        finally:
            slots.release()
    renpybuild.run.RunCommand.__init__ = bounded_init
    renpybuild.run.RunCommand.run = bounded_run
    # Include the compile option in the source-build cache identity.
    marker = root / "tmp/shinki-android-offscreen"
    identity = "--enable-video-offscreen\n" + digest(recipe / "android/SDL2-offscreen-default-display.patch")
    if not marker.exists() or marker.read_text(encoding="utf-8") != identity:
        complete = root / "tmp/complete"
        if complete.exists():
            for entry in complete.glob("*-sdl2.android-*"):
                entry.unlink()
        marker.parent.mkdir(parents=True, exist_ok=True)
        marker.write_text(identity, encoding="utf-8")
    sys.argv = [sys.argv[0], "--platform", "android", "--arch", ARCHITECTURES[args.target], "--python", "3", "--stop", "librenpy"]
    from renpybuild.__main__ import main as build
    build()
    stage(root, output_root, args.target)


if __name__ == "__main__":
    main()
