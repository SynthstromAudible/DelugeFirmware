//! Links the device image: the linker scripts, and the C++ app from the clang CMake tree.

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
    emit_bench_cfg(&build_dir);
    let app_objs_archive = archive_app_objects(repo_root, &build_dir, &cfg, &ar, out_dir);
    link_app_closure(&build_dir, &cfg, &app_objs_archive);
    emit_runtime_link_args();
    emit_size_link_args();
}

/// Copies the memory layout for the active `rtt` feature and the SDRAM fragment into `out_dir`,
/// and points the linker at them. rza1-rt's build script puts `link.x` (its RTT form with `rtt`) on
/// the search path; deluge-bsp's own `memory.x` is off (`default-features = false`), so this
/// image's layout is the one `link.x` INCLUDEs.
fn setup_linker_script(manifest_dir: &Path, out_dir: &Path) {
    let rtt = env::var("CARGO_FEATURE_RTT").is_ok();
    let memory_src = if rtt { "memory_rtt.x" } else { "memory.x" };
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
    // The fragment goes first. lld assigns each input section to the first matching output
    // section in command-line script order, before applying the fragment's INSERTs, and the
    // fragment's patterns that move statics to SDRAM must win over `link.x`'s `*(.bss.*)`.
    println!("cargo:rustc-link-arg=-Wl,-T,sdram_sections.x");
    println!("cargo:rustc-link-arg=-Wl,-T,link.x");
    println!("cargo:rerun-if-changed=linker/memory.x");
    println!("cargo:rerun-if-changed=linker/memory_rtt.x");
    println!("cargo:rerun-if-changed=linker/sdram_sections.x");
}

/// Forces the device entry points that no strong reference would pull into the link.
fn emit_forced_link_roots() {
    force_link_roots(&SAMPLE_STREAM_ROOTS);
    force_link_roots(&EFATFS_ROOTS);
}

/// Resolves the CMake build dir, build config and archiver for the device link, each overridable
/// by its `DELUGE_*` environment variable (each with a `rerun-if-env-changed`, or repointing one
/// would relink the last archive).
///
/// The build dir defaults to `build/`, the tree `dbt build` configures.
///
/// The archiver defaults to `arm-none-eabi-ar`, right for Debug's plain ELF objects. LTO builds
/// must pass an LTO-aware one (`llvm-ar`, which `dbt build` sets): GNU `ar` cannot index bitcode,
/// and the linker then silently pulls in nothing.
fn resolve_build_dir(repo_root: &Path) -> (PathBuf, String, PathBuf) {
    println!("cargo:rerun-if-env-changed=DELUGE_BUILD_DIR");
    println!("cargo:rerun-if-env-changed=DELUGE_BUILD_CONFIG");
    let build_dir = env::var("DELUGE_BUILD_DIR")
        .map(PathBuf::from)
        .unwrap_or_else(|_| repo_root.join("build"));
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

/// Sets `cfg(dsp_bench)` when the C++ tree at `build_dir` was configured with `DELUGE_DSP_BENCH`,
/// so a normal image carries none of the bench's boot path.
fn emit_bench_cfg(build_dir: &Path) {
    let cache = build_dir.join("CMakeCache.txt");
    println!("cargo:rerun-if-changed={}", cache.display());
    let on = fs::read_to_string(&cache).is_ok_and(|text| {
        text.lines()
            .filter_map(|line| line.strip_prefix("DELUGE_DSP_BENCH:"))
            .filter_map(|rest| rest.split_once('=').map(|(_, value)| value))
            .any(cmake_true)
    });
    if on {
        println!("cargo::rustc-cfg=dsp_bench");
    }
}

/// CMake's true constants: `1`, `ON`, `YES`, `TRUE`, `Y`, case-insensitive.
fn cmake_true(value: &str) -> bool {
    ["1", "ON", "YES", "TRUE", "Y"]
        .iter()
        .any(|t| value.trim().eq_ignore_ascii_case(t))
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
             deluge_dsp deluge_dsp_fixed deluge_dsp_float deluge_foundation deluge_serialization deluge_music_theory deluge_project deluge_instrument deluge_effects deluge_midi \
             deluge_gfx_oled deluge_gfx_grid deluge_ui_core deluge_ui_oled deluge_ui_grid deluge_ui_shell \
             deluge_notify deluge_scenes deluge_scenes_bind",
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
    let deps: [(&str, &str); 21] = [
        ("src/NE10", "libNE10.a"),
        ("src/lib", "libeyalroz_printf.a"),
        ("src/deluge/dsp", "libdeluge_dsp.a"),
        ("src/deluge/dsp-next", "libdeluge-dsp-fixed.a"),
        ("src/deluge/dsp-next", "libdeluge-dsp-float.a"),
        ("src/foundation", "libdeluge_foundation.a"),
        ("src/serialization", "libdeluge_serialization.a"),
        ("src/music-theory", "libdeluge_music_theory.a"),
        ("src/deluge-project", "libdeluge_project.a"),
        ("src/engines/deluge", "libdeluge_instrument.a"),
        ("src/engines/effects", "libdeluge_effects.a"),
        ("src/midi", "libdeluge_midi.a"),
        ("src/gfx", "libdeluge_gfx_oled.a"),
        ("src/gfx", "libdeluge_gfx_grid.a"),
        ("src/ui", "libdeluge_ui_core.a"),
        ("src/ui", "libdeluge_ui_oled.a"),
        ("src/ui", "libdeluge_ui_grid.a"),
        ("src/shell", "libdeluge_ui_shell.a"),
        ("src/notify", "libdeluge_notify.a"),
        ("src/scenes", "libdeluge_scenes.a"),
        ("src/scenes/bind", "libdeluge_scenes_bind.a"),
    ];

    println!("cargo:rustc-link-arg=-Wl,--start-group");
    println!("cargo:rustc-link-arg={}", app_objs_archive.display());
    for (dir, lib) in deps {
        let p = build_dir.join(dir).join(cfg).join(lib);
        assert!(p.is_file(), "missing dep archive {}", p.display());
        println!("cargo:rustc-link-arg={}", p.display());
        // A change confined to one of these libraries must relink: cargo tracks only what it is told to.
        println!("cargo:rerun-if-changed={}", p.display());
    }
    println!("cargo:rustc-link-arg=-Wl,--end-group");
}

/// Re-adds the C and C++ runtime the app needs, which rustc's `-nodefaultlibs` drops, grouped
/// for their circular references. rza1-rt's `link.x` keeps `.ARM.exidx`, so C++ unwinding works.
fn emit_runtime_link_args() {
    println!("cargo:rustc-link-arg=-Wl,--start-group");
    for l in ["-lstdc++", "-lsupc++", "-lc", "-lm", "-lgcc", "-lnosys"] {
        println!("cargo:rustc-link-arg={l}");
    }
    println!("cargo:rustc-link-arg=-Wl,--end-group");
}

/// Link options that shrink the image, which must fit in on-chip SRAM (docs/dev/image_size.md).
///
/// - `--icf=safe` folds functions whose code is identical, such as one template instantiated for
///   types of the same layout. `safe` folds only functions whose address is never taken, so
///   function-pointer comparisons keep their meaning. Folded functions share one address, so a
///   backtrace may name a sibling of the function that really ran.
/// - `snprintf` resolves to `eyalroz_printf`'s `snprintf_`, which the app already links, instead of
///   newlib's, whose floating-point formatter is far larger. It supports every format the app uses.
fn emit_size_link_args() {
    println!("cargo:rustc-link-arg=-Wl,--icf=safe");
    println!("cargo:rustc-link-arg=-Wl,--defsym=snprintf=snprintf_");
}
