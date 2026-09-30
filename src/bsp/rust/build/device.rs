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
    warn_if_heap_hack_active(manifest_dir);
}

/// Copies the linker script + memory-layout fragment for the active `rtt`
/// feature into `out_dir` and points the linker at them. rza1l-hal's own
/// build.rs puts `rza1l.x` on the link search path; this supplies the
/// matching `memory.x` (mirrors deluge-sdk's `firmwares/controller-firmware`).
fn setup_linker_script(manifest_dir: &Path, out_dir: &Path) {
    let rtt = env::var("CARGO_FEATURE_RTT").is_ok();
    let (memory_src, linker_script) = if rtt {
        ("memory_rtt.x", "rza1l_rtt.x")
    } else {
        ("memory.x", "rza1l.x")
    };
    // Linker sources live under linker/ (NOT the crate root): the cross linker
    // resolves `INCLUDE memory.x` from its CWD (the crate root) before the -L
    // search path, so a memory.x in the root would shadow this generated one.
    let linker_src = manifest_dir.join("linker");
    fs::copy(linker_src.join(memory_src), out_dir.join("memory.x")).unwrap();
    // Supplementary fragment that places the app's SDRAM sections + symbols.
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

/// Forces (`-Wl,-u,SYM`) every device-side ABI entry point whose only
/// reference wouldn't otherwise pull its archive member out of the link, so
/// `--gc-sections` can't silently drop it. Each symbol is listed
/// individually (not just one root per translation unit) because
/// `--gc-sections` prunes unreached function sections one at a time even
/// within an already-extracted object.
fn emit_forced_link_roots() {
    force_link_roots(&SAMPLE_STREAM_ROOTS);
    force_link_roots(&EFATFS_ROOTS);

    // The pad-grid crash reporter (src/deluge/io/debug/fault_pattern.c). Its ONLY
    // reference is the deliberately-weak one from the HAL's UNDEF vector, and a weak
    // undefined reference does not pull a member out of an archive — so without this
    // root the reporter is silently left out and every fault falls back to the bare
    // spin, which is precisely the invisible-crash behaviour it exists to end.
    force_link_roots(&["handle_cpu_fault"]);
}

/// Resolves the CMake build dir, build config, and archiver for the device
/// link, each overridable by the matching `DELUGE_*` env var.
///
/// Every override gets a `rerun-if-env-changed`: without one Cargo holds no
/// directive mentioning the new value, so repointing any of them would
/// silently relink whatever was last archived.
///
/// **Build dir** defaults by target, not to the clang tree: this script also
/// serves `cargo device` (`armv7a-none-eabihf`, GCC), and defaulting that to
/// `build/` would archive clang objects into a GCC link — see the module docs
/// for why that is worse than a build failure. Cargo sets `TARGET` to a custom
/// target's JSON file stem, so the two device trees are distinguishable here.
///
/// **Archiver** defaults to `arm-none-eabi-ar`, which is correct for Debug:
/// those objects are plain ELF. Release enables LTO (GCC slim-LTO, or ThinLTO
/// bitcode on the clang tree), and GNU `ar` cannot read bitcode symbols — it
/// writes an archive whose index is empty for every member, so the linker
/// silently pulls in none of them. LTO builds must override this with an
/// LTO-aware archiver (`gcc-ar` / `llvm-ar`).
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
    // `toolchain/current` symlinks to the active toolchain version's host dir,
    // so this survives toolchain version bumps.
    println!("cargo:rerun-if-env-changed=DELUGE_DEVICE_AR");
    let ar = env::var("DELUGE_DEVICE_AR")
        .map(PathBuf::from)
        .unwrap_or_else(|_| {
            repo_root.join("toolchain/current/arm-none-eabi-gcc/bin/arm-none-eabi-ar")
        });
    (build_dir, cfg, ar)
}

/// Archives `deluge_app`'s object closure (an OBJECT lib, so no `.a` exists
/// yet) into `libdeluge_app_objs.a`, panicking with the exact CMake command
/// if the app hasn't been built at `build_dir`/`cfg`. Registers
/// `rerun-if-changed` on each object's own path (content, not just
/// presence): a directory-level `rerun-if-changed` only fires on add/remove,
/// so editing a `.cpp` and rebuilding `deluge_app` in place wouldn't
/// otherwise trigger a re-archive, leaving a stale link. The directory
/// itself is also watched, to catch objects being added or removed.
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

/// Emits the link args for the app archive plus the portable static-lib
/// closure (argon/etl are header-only; no fatfs entry — C-FatFS is retired
/// and no longer part of the CMake build graph, deluge_app's storage calls
/// go through the efatfs C-ABI implemented natively in this crate), as one
/// `--start-group` because the references are mutual: C++ calls the
/// `deluge_*` services, this crate calls `deluge_main()`.
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

/// Re-adds the C++/C runtime that `deluge_app` needs: rustc passes
/// `-nodefaultlibs`, so libstdc++/libsupc++ (std::, `__cxa_*`, vtables),
/// libgcc (helpers like `__popcountsi2`), newlib libc/libm, and libnosys
/// (unhosted syscall stubs) all need re-adding explicitly. g++'s own search
/// paths resolve these. Grouped for the libstdc++<->libc<->libgcc circular
/// refs.
///
/// `__exidx_start`/`__exidx_end` are defined by rza1l.x's `.ARM.exidx`
/// section (kept, not discarded) so C++ exception unwinding works.
fn emit_runtime_link_args() {
    println!("cargo:rustc-link-arg=-Wl,--start-group");
    for l in ["-lstdc++", "-lsupc++", "-lc", "-lm", "-lgcc", "-lnosys"] {
        println!("cargo:rustc-link-arg={l}");
    }
    println!("cargo:rustc-link-arg=-Wl,--end-group");
}

/// Self-clearing nag for the temporary rza1l.x stack/heap-overlap workaround
/// (`program_stack_start` retargeted to `__sram_heap_end` so the C++ app's
/// GeneralMemoryAllocator can't overrun the mode stacks). Warns on every
/// build while the HACK tag is present in the sibling deluge-sdk linker
/// scripts; goes silent automatically once the proper fix (app sources heap
/// bounds via libdeluge/memory.h) removes it.
fn warn_if_heap_hack_active(manifest_dir: &Path) {
    let sdk = manifest_dir.join("../../../../deluge-sdk/crates/rza1l-hal");
    for s in ["rza1l.x", "rza1l_rtt.x"] {
        let p = sdk.join(s);
        println!("cargo:rerun-if-changed={}", p.display());
        if fs::read_to_string(&p).is_ok_and(|c| c.contains("DELUGE_APP_HEAP_HACK")) {
            println!(
                "cargo:warning=TEMP HACK active in deluge-sdk {s}: program_stack_start \
                 retargeted to __sram_heap_end for the C++ app heap. Remove once the app \
                 sources heap bounds via libdeluge/memory.h (deluge_memory_*)."
            );
        }
    }
}
