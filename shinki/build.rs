use anyhow::{Context, Result, bail};
use shinki_emu_build::{
    BuildAsset, CoreBuildManifest, configure_cmake, generate_import_library,
    write_core_build_manifest,
};
use std::{env, path::PathBuf};

fn input(name: &str) -> Result<PathBuf> {
    println!("cargo:rerun-if-env-changed={name}");
    let path = PathBuf::from(
        env::var_os(name)
            .with_context(|| format!("{name} is required; run cargo xtask deps --core renpy"))?,
    );
    if !path.exists() {
        bail!("missing {name} input: {}", path.display());
    }
    Ok(std::path::absolute(path)?)
}
fn asset(source: PathBuf, dest: &str) -> BuildAsset {
    BuildAsset {
        source,
        dest: dest.into(),
    }
}
fn run() -> Result<()> {
    let target = env::var("TARGET")?;
    let source = PathBuf::from(env::var_os("CARGO_MANIFEST_DIR").context("CARGO_MANIFEST_DIR")?);
    let sdk = input("RENPY_SDK_ROOT")?;
    let python = input("RENPY_PYTHON_DEV")?;
    let sdl = input("RENPY_SDL_INCLUDE")?;
    let cython_license = input("RENPY_CYTHON_LICENSE")?;
    println!(
        "cargo:rerun-if-changed={}",
        source.join("core.cpp").display()
    );
    println!(
        "cargo:rerun-if-changed={}",
        source.join("CMakeLists.txt").display()
    );
    println!(
        "cargo:rerun-if-changed={}",
        source.join("shinki_renpy_bridge.py").display()
    );
    println!(
        "cargo:rerun-if-changed={}",
        source.join("shinki_save_migration.py").display()
    );
    println!(
        "cargo:rerun-if-changed={}",
        source.join("../renpy").display()
    );
    let native_dir = if target.contains("windows") {
        sdk.join("lib/py3-windows-x86_64")
    } else if target.contains("apple") {
        sdk.join("lib/py3-mac-x86_64")
    } else {
        sdk.join("lib/py3-linux-x86_64")
    };
    let python_lib = if target.contains("windows") {
        native_dir.join("libpython3.12.dll")
    } else {
        native_dir.join("libpython3.12.so")
    };
    let renpython_lib = if target.contains("windows") {
        native_dir.join("librenpython.dll")
    } else {
        native_dir.join("librenpython.so")
    };
    for path in [&python_lib, &renpython_lib] {
        if !path.exists() {
            bail!("missing Ren'Py runtime library: {}", path.display());
        }
    }
    let mut definitions = vec![
        ("RENPY_SDK_ROOT".into(), sdk.to_string_lossy().into_owned()),
        (
            "RENPY_PYTHON_DEV".into(),
            python.to_string_lossy().into_owned(),
        ),
        (
            "RENPY_SDL_INCLUDE".into(),
            sdl.to_string_lossy().into_owned(),
        ),
        (
            "RENPY_CYTHON_LICENSE".into(),
            cython_license.to_string_lossy().into_owned(),
        ),
    ];
    if target.ends_with("windows-msvc") {
        let imports = PathBuf::from(env::var_os("OUT_DIR").context("OUT_DIR")?).join("imports");
        for library in [&python_lib, &renpython_lib] {
            generate_import_library(library, &imports, &target)?;
        }
        definitions.push((
            "RENPY_IMPORT_LIBRARY_DIR".into(),
            imports.to_string_lossy().into_owned(),
        ));
    }
    if let Some(ffmpeg) = env::var_os("SHINKI_EMU_FFMPEG_INSTALLED") {
        definitions.push((
            "RENPY_FFMPEG_ROOT".into(),
            PathBuf::from(ffmpeg).to_string_lossy().into_owned(),
        ));
    }
    if let Some(interpreter) = env::var_os("Python3_EXECUTABLE") {
        definitions.push((
            "Python3_EXECUTABLE".into(),
            interpreter.to_string_lossy().into_owned(),
        ));
    }
    let native_build = configure_cmake(&source, &definitions, &["install"])?;
    let install = native_build.join("install");
    let library = if target.contains("windows") {
        install.join("shinki_emu_renpy.dll")
    } else if target.contains("apple") {
        install.join("libshinki_emu_renpy.dylib")
    } else {
        install.join("libshinki_emu_renpy.so")
    };
    let mut manifest = CoreBuildManifest::new("renpy", "native-shared")?;
    manifest.native_library = Some(library);
    manifest.resources = vec![asset(install.join("Resources"), "Resources")];
    manifest.licenses = vec![
        asset(source.join("LICENSE"), "licenses/renpy/LICENSE-SHINKI.txt"),
        asset(sdk.join("LICENSE.txt"), "licenses/renpy/LICENSE-RenPy.txt"),
        asset(
            python.join("LICENSE.txt"),
            "licenses/renpy/LICENSE-CPython.txt",
        ),
        asset(
            cython_license.with_file_name("COPYING.txt"),
            "licenses/renpy/COPYING-Cython.txt",
        ),
        asset(cython_license, "licenses/renpy/LICENSE-Cython.txt"),
    ];
    if target.ends_with("windows-msvc") {
        manifest.licenses.push(asset(
            native_dir.join("libwinpthread-1.txt"),
            "licenses/renpy/libwinpthread-1.txt",
        ));
    }
    write_core_build_manifest(&manifest)?;
    if target.ends_with("windows-msvc") {
        println!(
            "cargo:rustc-link-search=native={}",
            install.join("lib").display()
        );
        println!("cargo:rustc-link-lib=dylib=shinki_emu_renpy");
    }
    Ok(())
}
fn main() {
    if let Err(error) = run() {
        panic!("Ren'Py native build failed: {error:#}");
    }
}
