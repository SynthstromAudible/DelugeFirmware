//! Links the device image: the linker scripts, and the CMake-built C++ app for the device target.
//!
//! The two device CMake trees are ABI-incompatible: GCC mangles `int32_t` as `long`, clang as
//! `int`. Their Debug objects are plain ELF, so mixing them links successfully with a corrupt ABI
//! instead of failing — see [`resolve_build_dir`] for how the tree is chosen.

use std::env;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

use super::link::{EFATFS_ROOTS, SAMPLE_STREAM_ROOTS, collect_objs, force_link_roots};

/// Emits everything the device link needs.
pub fn link(repo_root: &Path, manifest_dir: &Path, out_dir: &Path) {
    setup_linker_script(manifest_dir, out_dir);
    emit_forced_link_roots();
    let (build_dir, cfg, ar) = resolve_build_dir(repo_root);
    let app_objs_archive = archive_app_objects(repo_root, &build_dir, &cfg, &ar, out_dir);
    link_app_closure(&build_dir, &cfg, &app_objs_archive);
    emit_runtime_link_args();
}

/// Copies the memory layout for the active `rtt` feature and the SDRAM fragment into `out_dir`,
/// and points the linker at them. rza1l-hal's build script puts `rza1l.x` on the search path.
fn setup_linker_script(manifest_dir: &Path, out_dir: &Path) {
    let rtt = env::var("CARGO_FEATURE_RTT").is_ok();
    let (memory_src, linker_script) = if rtt {
        ("memory_rtt.x", "rza1l_rtt.x")
    } else {
        ("memory.x", "rza1l.x")
    };
    // Not in the crate root: the linker resolves `INCLUDE memory.x` from its working directory
    // before the -L path, so a memory.x there would shadow this one.
    let linker_src = manifest_dir.join("linker");
    fs::copy(linker_src.join(memory_src), out_dir.join("memory.x")).unwrap();
    // Places the app's SDRAM sections and symbols.
    fs::copy(
        linker_src.join("sdram_sections.x"),
        out_dir.join("sdram_sections.x"),
    )
    .unwrap();
    println!("cargo:rustc-link-search={}", out_dir.display());
    println!("cargo:rustc-link-arg=-Wl,-T,{linker_script}");
    println!("cargo:rustc-link-arg=-Wl,-T,sdram_sections.x");
    println!("cargo:rerun-if-changed=linker/memory.x");
    println!("cargo:rerun-if-changed=linker/memory_rtt.x");
    println!("cargo:rerun-if-changed=linker/sdram_sections.x");
}

/// Forces the device entry points that no strong reference would pull into the link.
fn emit_forced_link_roots() {
    force_link_roots(&SAMPLE_STREAM_ROOTS);
    force_link_roots(&EFATFS_ROOTS);

    // The pad-grid crash reporter (src/deluge/io/debug/fault_pattern.c). Its only reference is
    // the HAL UNDEF vector's weak one, which pulls nothing from an archive; without this root
    // every fault would fall back to a silent spin.
    force_link_roots(&["handle_cpu_fault"]);
}

/// Resolves the CMake build dir, build config and archiver for the device link, each overridable
/// by its `DELUGE_*` environment variable (each with a `rerun-if-env-changed`, or repointing one
/// would relink the last archive).
///
/// The build dir defaults by target: `build/` (clang) for `armv7a-deluge-eabihf`, `build-gcc/`
/// otherwise, so `cargo device` never archives clang objects into a GCC link. (Cargo sets `TARGET`
/// to a custom target's JSON file stem.)
///
/// The archiver defaults to `arm-none-eabi-ar`, right for Debug's plain ELF objects. LTO builds
/// must pass an LTO-aware one (`gcc-ar`, `llvm-ar`): GNU `ar` cannot index bitcode, and the linker
/// then silently pulls in nothing.
fn resolve_build_dir(repo_root: &Path) -> (PathBuf, String, PathBuf) {
    println!("cargo:rerun-if-env-changed=DELUGE_BUILD_DIR");
    println!("cargo:rerun-if-env-changed=DELUGE_BUILD_CONFIG");
    let build_dir = env::var("DELUGE_BUILD_DIR")
        .map(PathBuf::from)
        .unwrap_or_else(|_| {
            let default_tree = if env::var("TARGET").as_deref() == Ok("armv7a-deluge-eabihf") {
                "build"
            } else {
                "build-gcc"
            };
            repo_root.join(default_tree)
        });
    let cfg = env::var("DELUGE_BUILD_CONFIG").unwrap_or_else(|_| "Debug".into());
    // `toolchain/current` links to the active toolchain version.
    println!("cargo:rerun-if-env-changed=DELUGE_DEVICE_AR");
    let ar = env::var("DELUGE_DEVICE_AR")
        .map(PathBuf::from)
        .unwrap_or_else(|_| {
            repo_root.join("toolchain/current/arm-none-eabi-gcc/bin/arm-none-eabi-ar")
        });
    (build_dir, cfg, ar)
}

/// Archives `deluge_app`'s objects (an OBJECT library, so CMake makes no `.a`) into
/// `libdeluge_app_objs.a`.
///
/// # Panics
///
/// If the app has not been built at `build_dir`/`cfg`; the message gives the CMake command.
///
/// Each object gets its own `rerun-if-changed`, since the directory's fires only on add or remove.
fn archive_app_objects(
    repo_root: &Path,
    build_dir: &Path,
    cfg: &str,
    ar: &Path,
    out_dir: &Path,
) -> PathBuf {
    let app_objs_dir = build_dir.join(format!("src/deluge/CMakeFiles/deluge_app.dir/{cfg}"));
    if !app_objs_dir.is_dir() {
        panic!(
            "C++ app objects not found at {}. Build them first:\n  \
             cmake --build {} --target deluge_app NE10 eyalroz_printf \
             deluge_dsp deluge_dsp_fixed deluge_dsp_float deluge_foundation deluge_midi",
            app_objs_dir.display(),
            build_dir.display()
        );
    }

    let mut objs = Vec::new();
    collect_objs(
        &app_objs_dir,
        &app_objs_dir,
        &repo_root.join("src/deluge"),
        &mut objs,
        &mut Vec::new(),
    );
    objs.sort();
    for o in &objs {
        println!("cargo:rerun-if-changed={}", o.display());
    }
    println!("cargo:rerun-if-changed={}", app_objs_dir.display());

    let app_objs_archive = out_dir.join("libdeluge_app_objs.a");
    let _ = fs::remove_file(&app_objs_archive);
    let status = Command::new(ar)
        .arg("crs")
        .arg(&app_objs_archive)
        .args(&objs)
        .status()
        .expect("run arm-none-eabi-ar");
    assert!(status.success(), "archiving deluge_app objects failed");
    app_objs_archive
}

/// Emits the app archive and its static libraries as one `--start-group`, because the references
/// are mutual: the C++ app calls the `deluge_*` services, and this crate calls `deluge_app_init`.
fn link_app_closure(build_dir: &Path, cfg: &str, app_objs_archive: &Path) {
    let deps: [(&str, &str); 7] = [
        ("src/NE10", "libNE10.a"),
        ("src/lib", "libeyalroz_printf.a"),
        ("src/deluge/dsp", "libdeluge_dsp.a"),
        ("src/deluge/dsp-next", "libdeluge-dsp-fixed.a"),
        ("src/deluge/dsp-next", "libdeluge-dsp-float.a"),
        ("src/foundation", "libdeluge_foundation.a"),
        ("src/midi", "libdeluge_midi.a"),
    ];

    println!("cargo:rustc-link-arg=-Wl,--start-group");
    println!("cargo:rustc-link-arg={}", app_objs_archive.display());
    for (dir, lib) in deps {
        let p = build_dir.join(dir).join(cfg).join(lib);
        assert!(p.is_file(), "missing dep archive {}", p.display());
        println!("cargo:rustc-link-arg={}", p.display());
    }
    println!("cargo:rustc-link-arg=-Wl,--end-group");
}

/// Re-adds the C and C++ runtime the app needs, which rustc's `-nodefaultlibs` drops, grouped
/// for their circular references. rza1l.x keeps `.ARM.exidx`, so C++ unwinding works.
fn emit_runtime_link_args() {
    println!("cargo:rustc-link-arg=-Wl,--start-group");
    for l in ["-lstdc++", "-lsupc++", "-lc", "-lm", "-lgcc", "-lnosys"] {
        println!("cargo:rustc-link-arg={l}");
    }
    println!("cargo:rustc-link-arg=-Wl,--end-group");
}
