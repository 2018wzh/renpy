use anyhow::{Context, Result, bail};
use shinki_emu_build::{
    BuildAsset, CoreBuildManifest, configure_cmake, generate_import_library,
    link_android_static_core, link_apple_static_core, write_core_build_manifest,
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
    let static_build = env::var_os("CARGO_FEATURE_STATIC").is_some();
    let static_root = if static_build {
        if !(target.contains("apple-ios") || target.ends_with("-linux-android")) {
            bail!("Ren'Py static embedding requires an iOS or Android target");
        }
        let root = input("RENPY_STATIC_ROOT")?;
        for relative in [
            "include/python3.12/Python.h",
            "include/python3.12/pyconfig.h",
            "lib/librenpy.a",
            "lib/libpython3.12.a",
            "lib/pkgconfig/shinki-renpy-runtime.pc",
        ] {
            if !root.join(relative).is_file() {
                bail!(
                    "missing target Ren'Py runtime input: {}",
                    root.join(relative).display()
                );
            }
        }
        Some(root)
    } else {
        if target.contains("apple-ios") || target.ends_with("-linux-android") {
            bail!("Ren'Py mobile targets require the static adapter feature");
        }
        None
    };
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
    } else if target.ends_with("apple-darwin") {
        sdk.join("lib/py3-mac-universal")
    } else {
        sdk.join("lib/py3-linux-x86_64")
    };
    let python_lib = if target.contains("windows") {
        native_dir.join("libpython3.12.dll")
    } else if target.ends_with("apple-darwin") {
        native_dir.join("librenpython.dylib")
    } else {
        native_dir.join("librenpython.so")
    };
    let renpython_lib = if target.contains("windows") {
        native_dir.join("librenpython.dll")
    } else if target.ends_with("apple-darwin") {
        native_dir.join("librenpython.dylib")
    } else {
        native_dir.join("librenpython.so")
    };
    for path in [&python_lib, &renpython_lib] {
        if !static_build && !path.exists() {
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
    if let Some(root) = &static_root {
        definitions.push(("RENPY_STATIC".into(), "ON".into()));
        definitions.push((
            "RENPY_STATIC_ROOT".into(),
            root.to_string_lossy().into_owned(),
        ));
    }
    if target.contains("apple") {
        let ios = target.contains("apple-ios");
        let architecture = if !ios {
            "x86_64;arm64"
        } else if target.starts_with("aarch64-") {
            "arm64"
        } else {
            "x86_64"
        };
        let system = if ios { "iOS" } else { "Darwin" };
        definitions.push(("CMAKE_SYSTEM_NAME".into(), system.into()));
        definitions.push(("CMAKE_OSX_ARCHITECTURES".into(), architecture.into()));
        if ios {
            let sdk = if target.ends_with("-sim") || target.starts_with("x86_64-") {
                "iphonesimulator"
            } else {
                "iphoneos"
            };
            definitions.push(("CMAKE_OSX_SYSROOT".into(), sdk.into()));
            definitions.push(("CMAKE_OSX_DEPLOYMENT_TARGET".into(), "16.0".into()));
            definitions.push((
                "CMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED".into(),
                "NO".into(),
            ));
        }
        let toolchain =
            PathBuf::from(env::var_os("OUT_DIR").context("OUT_DIR")?).join("apple.cmake");
        std::fs::write(&toolchain, format!("set(CMAKE_SYSTEM_NAME {system})\n"))?;
        definitions.push((
            "CMAKE_TOOLCHAIN_FILE".into(),
            toolchain.to_string_lossy().into_owned(),
        ));
    }
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
    let library = if let Some(root) = &static_root {
        if target.contains("apple") {
            link_apple_static_core("renpy", &native_build, std::slice::from_ref(root))?
        } else {
            link_android_static_core(&native_build, std::slice::from_ref(root))?;
            install.join("lib/libshinki_emu_renpy.a")
        }
    } else if target.contains("windows") {
        install.join("shinki_emu_renpy.dll")
    } else if target.contains("apple") {
        install.join("shinki_emu_renpy.dylib")
    } else {
        install.join("shinki_emu_renpy.so")
    };
    let mut manifest = CoreBuildManifest::new(
        "renpy",
        if static_build {
            "native-static"
        } else {
            "native-shared"
        },
    )?;
    manifest.native_library = Some(library);
    manifest.resources = vec![asset(install.join("Resources"), "Resources")];
    manifest.licenses = vec![
        asset(source.join("LICENSE"), "licenses/renpy/LICENSE-SHINKI.txt"),
        asset(sdk.join("LICENSE.txt"), "licenses/renpy/LICENSE-RenPy.txt"),
        asset(
            if let Some(root) = &static_root {
                root.join("LICENSE-CPython.txt")
            } else {
                python.join(if target.contains("windows") {
                    "LICENSE.txt"
                } else {
                    "LICENSE"
                })
            },
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
