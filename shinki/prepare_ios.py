"""Prepare official Renios static archives and matching target CPython headers.

The pinned renpy-build tree supplies CPython/SDL sources and its Darwin patch.
No target interpreter or extension is replaced with a desktop binary.
"""
import hashlib
import json
import os
import pathlib
import plistlib
import re
import shlex
import shutil
import subprocess
import sys
import tarfile
import tempfile

recipe, renios, sdk, host_python, output, metalangle = map(pathlib.Path, sys.argv[1:7])
target = sys.argv[7]
variants = {
    "aarch64-apple-ios": ("arm64", "iphoneos", "release", "ios-arm64_armv7", "arm64-apple-ios16.0"),
    "aarch64-apple-ios-sim": ("arm64", "iphonesimulator", "debug", "ios-arm64_i386_x86_64-simulator", "arm64-apple-ios16.0-simulator"),
    "x86_64-apple-ios": ("x86_64", "iphonesimulator", "debug", "ios-arm64_i386_x86_64-simulator", "x86_64-apple-ios16.0-simulator"),
}
arch, sdk_name, variant, framework_slice, clang_target = variants[target]

def capture(*args):
    return subprocess.check_output(args, text=True).strip()

def run(*args, **kwargs):
    subprocess.run(args, check=True, **kwargs)

sdk_path = capture("xcrun", "--sdk", sdk_name, "--show-sdk-path")
identity = {
    "target": target,
    "renios_sha256": "c4fae153e8276ed0faed5e84ea3e0b7c4bf337f0e3208e9130c6a41748a83b2b",
    "recipe": "7bfab40c1174f622f644b24669afd5fb167fbb79",
    "script": hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest(),
    "xcode": capture("xcodebuild", "-version"),
    "sdk": sdk_path,
}
marker = output / "runtime.json"
if marker.is_file() and json.loads(marker.read_text()) == identity:
    sys.exit(0)

output.parent.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix="renpy-ios-", dir=output.parent) as temporary:
    work = pathlib.Path(temporary)
    stage = work / "runtime"
    include = stage / "include/python3.12"
    libraries = stage / "lib"
    include.mkdir(parents=True)
    libraries.mkdir()
    resources = stage / "Resources"
    framework = resources / "Frameworks/ShinkiRenpyMetalANGLE.framework"
    shutil.copytree(renios / "prototype/Frameworks/MetalANGLE.xcframework" / framework_slice / "MetalANGLE.framework", framework)
    run("xcrun", "lipo", str(framework / "MetalANGLE"), "-thin", arch, "-output", str(work / "MetalANGLE"))
    (framework / "MetalANGLE").unlink()
    shutil.copy2(work / "MetalANGLE", framework / "ShinkiRenpyMetalANGLE")
    with (framework / "Info.plist").open("rb") as stream:
        metadata = plistlib.load(stream)
    metadata["CFBundleExecutable"] = "ShinkiRenpyMetalANGLE"
    metadata["CFBundleIdentifier"] = "org.shinkirou.renpy.MetalANGLE"
    with (framework / "Info.plist").open("wb") as stream:
        plistlib.dump(metadata, stream)
    python_tar = recipe / "source/Python-3.12.8.tar.xz"
    with tarfile.open(python_tar) as archive:
        archive.extractall(work / "python", filter="data")
    python = work / "python/Python-3.12.8"
    run("patch", "-p1", "-i", str(recipe / "patches/Python-3.12.8/cross-darwin.diff"), cwd=python)
    run("autoreconf", "-vfi", cwd=python)
    environment = os.environ.copy()
    for key in ("MACOSX_DEPLOYMENT_TARGET", "IPHONEOS_DEPLOYMENT_TARGET", "CFLAGS", "CPPFLAGS", "LDFLAGS"):
        environment.pop(key, None)
    compiler = capture("xcrun", "--sdk", sdk_name, "--find", "clang")
    environment.update({
        "CC": f"{shlex.quote(compiler)} -target {clang_target} -isysroot {shlex.quote(sdk_path)}",
        "ac_cv_file__dev_ptmx": "no", "ac_cv_file__dev_ptc": "no",
        "ac_cv_little_endian_double": "yes", "ac_cv_func_getentropy": "no",
        "ac_cv_have_long_long_format": "yes", "ac_cv_func_clock_settime": "no",
    })
    # A distinct Darwin host forces configure's cross path even on Intel simulators.
    run(str(python / "configure"), f"--host={arch}-apple-darwin", "--build=" + capture(str(python / "config.guess")),
        "--disable-toolbox-glue", "--enable-ipv6", "--with-ensurepip=no",
        "--with-build-python=" + str(host_python), cwd=python, env=environment)
    shutil.copytree(python / "Include", include, dirs_exist_ok=True)
    shutil.copy2(python / "pyconfig.h", include / "pyconfig.h")
    shutil.copy2(python / "LICENSE", stage / "LICENSE-CPython.txt")
    shutil.copy2(metalangle / "LICENSE", stage / "LICENSE-MetalANGLE.txt")
    # Match the runtime's SDL headers, not the desktop SDK's development headers.
    sdl_tar = recipe / "source/SDL2-2.0.20.tar.gz"
    with tarfile.open(sdl_tar) as archive:
        archive.extractall(work / "sdl", filter="data")
    sdl = work / "sdl/SDL2-2.0.20"
    patches = recipe / "patches/SDL2-2.0.20"
    for line in (patches / "series").read_text().splitlines():
        patch = line.partition("#")[0].strip()
        if patch:
            run("patch", "-p1", "-i", str(patches / patch), cwd=sdl)
    # SDL2 and SDL3 both define UIKit classes. Rebuild SDL2 with private
    # Objective-C class names; renaming Mach-O symbols alone cannot isolate them.
    classes = set()
    for source in (sdl / "src").rglob("*"):
        if source.suffix in {".m", ".h"}:
            classes.update(name.decode("ascii") for name in re.findall(
                rb"@interface\s+([A-Za-z_][A-Za-z_0-9]*)\s*:", source.read_bytes()))
    class_header = work / "sdl-classes.h"
    class_header.write_text("".join(f"#define {name} ShinkiRenpy{name}\n" for name in sorted(classes)))
    run("sh", "./autogen.sh", cwd=sdl)
    sdl_environment = environment.copy()
    sdl_environment["CFLAGS"] = f"-fobjc-arc -DSDL_MAIN_HANDLED -DMETALANGLE -include {shlex.quote(str(class_header))} -F{shlex.quote(str(resources / 'Frameworks'))}"
    # The source patch includes <MetalANGLE/...>; make only the build header
    # search path retain that name. The installed runtime has a private identity.
    headers = work / "framework-headers/MetalANGLE.framework"
    headers.parent.mkdir()
    shutil.copytree(framework / "Headers", headers / "Headers")
    sdl_environment["CFLAGS"] += " -F" + shlex.quote(str(headers.parent))
    sdl_environment["LDFLAGS"] = "-F" + shlex.quote(str(resources / "Frameworks")) + " -framework ShinkiRenpyMetalANGLE"
    sdl_environment["CXX"] = capture("xcrun", "--sdk", sdk_name, "--find", "clang++") + f" -target {clang_target} -isysroot {shlex.quote(sdk_path)}"
    sdl_environment["OBJC"] = sdl_environment["CC"]
    run(str(sdl / "configure"), f"--host={arch}-apple-darwin",
        "--build=" + capture(str(sdl / "build-scripts/config.guess")),
        "--prefix=" + str(work / "sdl-install"), "--disable-shared", "--enable-static",
        "--disable-render-metal", "--disable-jack", "--disable-pipewire",
        "--disable-video-kmsdrm", "--disable-video-x11", "--disable-video-wayland",
        cwd=sdl, env=sdl_environment)
    run("make", "-j3", cwd=sdl, env=sdl_environment)
    run("make", "install", cwd=sdl, env=sdl_environment)
    shutil.copytree(work / "sdl-install/include/SDL2", stage / "include/SDL2")
    shutil.copy2(work / "sdl-install/lib/libSDL2.a", libraries / "libSDL2.a")
    for archive in sorted((renios / "prototype/prebuilt" / variant).glob("*.a")):
        # Exclude app entrypoints/tests and the always-true OS-version shim.
        if archive.name in {"libSDL2.a", "libSDL2_test.a", "libSDL2main.a", "librenpython.a", "libmockrt.a"}:
            continue
        run("xcrun", "lipo", str(archive), "-thin", arch, "-output", str(libraries / archive.name))
    for name in ("libpython3.12.a", "librenpy.a"):
        if not (libraries / name).is_file():
            raise RuntimeError("Official Renios static runtime is missing " + name)
    shutil.copytree(sdk / "lib/python3.12", resources / "lib/python3.12", ignore=shutil.ignore_patterns("__pycache__"))
    rust_host = next(line.removeprefix("host: ") for line in capture("rustc", "-vV").splitlines() if line.startswith("host: "))
    llvm = pathlib.Path(capture("rustc", "--print", "sysroot")) / "lib/rustlib" / rust_host / "bin"
    nm, objcopy = llvm / "llvm-nm", llvm / "llvm-objcopy"
    if not nm.is_file() or not objcopy.is_file():
        raise RuntimeError("Ren'Py iOS isolation requires rustup component add llvm-tools-preview")
    native_inputs = sorted(libraries.glob("*.a")) + [framework / "ShinkiRenpyMetalANGLE"]
    symbols = set()
    for native in native_inputs:
        for line in capture(str(nm), "-g", "--defined-only", "--format=posix", str(native)).splitlines():
            fields = line.split()
            if len(fields) >= 2 and len(fields[1]) == 1 and fields[1].isalpha():
                symbols.add(fields[0])
    renames = {name: "_shinki_renpy_" + name[1:] for name in symbols if name.startswith("_")}
    symbol_file = work / "native-symbols.txt"
    symbol_file.write_text("".join(f"{old} {new}\n" for old, new in sorted(renames.items())))
    symbol_header = stage / "include/shinki-renpy-symbols.h"
    symbol_header.write_text("".join(
        f"#define {old[1:]} {new[1:]}\n" for old, new in sorted(renames.items())
        if re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", old[1:])
    ))
    for native in native_inputs:
        rewritten = work / ("private-" + native.name)
        run(str(objcopy), "--redefine-syms=" + str(symbol_file), str(native), str(rewritten))
        shutil.copy2(rewritten, native)
    run("install_name_tool", "-id", "@rpath/ShinkiRenpyMetalANGLE.framework/ShinkiRenpyMetalANGLE",
        str(framework / "ShinkiRenpyMetalANGLE"))
    pc = libraries / "pkgconfig/shinki-renpy-runtime.pc"
    pc.parent.mkdir()
    frameworks = "UIKit Foundation CoreGraphics AudioToolbox QuartzCore ImageIO CoreServices GameController CoreMotion Security SystemConfiguration AVFoundation CoreMedia CoreVideo VideoToolbox"
    pc.write_text("Name: shinki-renpy-runtime\nDescription: Official Renios system closure\nVersion: 8.5.3\nLibs: -lc++ -lz -liconv "
        + " ".join("-framework " + name for name in frameworks.split())
        + " -F" + shlex.quote(str(output / "Resources/Frameworks")) + " -framework ShinkiRenpyMetalANGLE\n")
    (stage / "runtime.json").write_text(json.dumps(identity, indent=2) + "\n")
    if output.exists():
        shutil.rmtree(output)
    shutil.move(stage, output)
