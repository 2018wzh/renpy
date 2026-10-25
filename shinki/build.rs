use anyhow::{bail, Context, Result};
use sha2::{Digest, Sha256};
use shinki_emu_build::{
    configure_cmake, generate_import_library, write_core_build_manifest, BuildAsset, CoreBuildManifest,
};
use std::{env, fs, io::Read, path::PathBuf};

fn input(name: &str) -> Result<PathBuf> {
    println!("cargo:rerun-if-env-changed={name}");
    let path = PathBuf::from(env::var_os(name).with_context(|| {
        format!("{name} is required; run cargo xtask deps --target x86_64-pc-windows-msvc --core renpy")
    })?);
    if !path.exists() {
        bail!("missing {name} input: {}", path.display());
    }
    Ok(std::path::absolute(path)?)
}

fn asset(source: PathBuf, dest: &str) -> BuildAsset {
    BuildAsset { source, dest: dest.into() }
}

fn run() -> Result<()> {
    let target = env::var("TARGET")?;
    let host = env::var("HOST")?;
    if target != "x86_64-pc-windows-msvc" || host != target {
        bail!("Ren'Py 8.5.3 requires a native Windows x64/MSVC host and target; HOST={host}, TARGET={target}");
    }
    // Keep CMake bounded even when the parent Cargo has a larger job limit.
    let jobs = env::var("NUM_JOBS").ok().and_then(|n| n.parse::<usize>().ok()).unwrap_or(4);
    // This build script is single-threaded; no other thread can read the environment.
    unsafe { env::set_var("NUM_JOBS", jobs.clamp(1, 4).to_string()) };
    let source = PathBuf::from(env::var_os("CARGO_MANIFEST_DIR").context("CARGO_MANIFEST_DIR")?);
    let out = PathBuf::from(env::var_os("OUT_DIR").context("OUT_DIR")?);
    let sdk = input("RENPY_SDK_ROOT")?;
    let python = input("RENPY_PYTHON_DEV")?;
    let sdl = input("RENPY_SDL_INCLUDE")?;
    let cython_license = input("RENPY_CYTHON_LICENSE")?;
    println!("cargo:rerun-if-env-changed=Python3_EXECUTABLE");
    println!("cargo:rerun-if-changed={}", source.join("sdk.json").display());
    println!("cargo:rerun-if-changed={}", source.join("core.cpp").display());
    println!("cargo:rerun-if-changed={}", source.join("vendor").display());
    println!("cargo:rerun-if-changed={}", source.join("shinki_renpy_bridge.py").display());
    println!("cargo:rerun-if-changed={}", source.join("../renpy").display());
    println!("cargo:rerun-if-changed={}", source.join("../renpy.py").display());
    println!("cargo:rerun-if-changed={}", source.join("../src").display());
    println!("cargo:rerun-if-changed={}", python.join("include").display());
    println!("cargo:rerun-if-changed={}", sdl.display());
    let lock: serde_json::Value = serde_json::from_slice(&fs::read(source.join("sdk.json"))?)?;
    let native = sdk.join("lib/py3-windows-x86_64");
    for (name, expected) in lock["windows_x86_64"].as_object().context("sdk.json windows_x86_64")? {
        let path = native.join(name);
        println!("cargo:rerun-if-changed={}", path.display());
        let mut file = fs::File::open(&path).with_context(|| format!("missing official SDK DLL: {}", path.display()))?;
        let mut digest = Sha256::new();
        let mut buffer = [0; 65536];
        loop {
            let n = file.read(&mut buffer)?;
            if n == 0 { break; }
            digest.update(&buffer[..n]);
        }
        if format!("{:x}", digest.finalize()) != expected.as_str().context("SDK DLL SHA256")? {
            bail!("{} is not the pinned official Ren'Py {} SDK DLL", path.display(), lock["version"]);
        }
    }
    let patchlevel = python.join("include/patchlevel.h");
    println!("cargo:rerun-if-changed={}", patchlevel.display());
    let header = fs::read_to_string(&patchlevel).context("CPython 3.12.7 development headers are required")?;
    if !header.lines().any(|l| {
        let mut fields = l.split_whitespace();
        fields.next() == Some("#define") && fields.next() == Some("PY_VERSION") && fields.next() == Some("\"3.12.7\"")
    }) {
        bail!("RENPY_PYTHON_DEV must contain CPython 3.12.7 headers");
    }
    for stem in ["libpython3.12", "librenpython"] {
        generate_import_library(
            &native.join(format!("{stem}.dll")), &out.join("imports"), &target,
        )?;
    }
    let mut definitions = vec![
        ("RENPY_SDK_ROOT".into(), sdk.to_string_lossy().into_owned()),
        ("RENPY_PYTHON_DEV".into(), python.to_string_lossy().into_owned()),
        ("RENPY_SDL_INCLUDE".into(), sdl.to_string_lossy().into_owned()),
        ("RENPY_IMPORT_LIBRARY_DIR".into(), out.join("imports").to_string_lossy().into_owned()),
        ("RENPY_CYTHON_LICENSE".into(), cython_license.to_string_lossy().into_owned()),
    ];
    if let Some(interpreter) = env::var_os("Python3_EXECUTABLE") {
        definitions.push(("Python3_EXECUTABLE".into(), interpreter.to_string_lossy().into_owned()));
    }
    let native_build = configure_cmake(&source, &definitions, &["install"])?;
    let install = native_build.join("install");
    let mut manifest = CoreBuildManifest::new("renpy", "native-shared")?;
    manifest.native_library = Some(install.join("shinki_emu_renpy.dll"));
    manifest.resources = vec![asset(install.join("Resources"), "Resources")];
    manifest.licenses = vec![
        asset(source.join("LICENSE"), "licenses/renpy/LICENSE-SHINKI.txt"),
        asset(sdk.join("LICENSE.txt"), "licenses/renpy/LICENSE-RenPy.txt"),
        asset(native.join("libwinpthread-1.txt"), "licenses/renpy/libwinpthread-1.txt"),
        asset(python.join("LICENSE.txt"), "licenses/renpy/LICENSE-CPython.txt"),
        asset(cython_license.with_file_name("COPYING.txt"), "licenses/renpy/COPYING-Cython.txt"),
        asset(cython_license, "licenses/renpy/LICENSE-Cython.txt"),
    ];
    write_core_build_manifest(&manifest)?;
    println!("cargo:rustc-link-search=native={}", install.join("lib").display());
    println!("cargo:rustc-link-lib=dylib=shinki_emu_renpy");
    Ok(())
}

fn main() {
    if let Err(error) = run() {
        panic!("Ren'Py native build failed: {error:#}");
    }
}
