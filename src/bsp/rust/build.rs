//! Links the CMake-built portable C++ `deluge_app` (an OBJECT lib, plus its
//! static-lib closure) against this crate for three targets: the clang
//! device target (`armv7a-deluge-eabihf`), the GCC device target
//! (`armv7a-none-eabihf`, `cargo device`), and a host target (`--features
//! host_app`). This script does not invoke CMake itself — the relevant tree
//! must already be built (see the panic messages below for the exact
//! commands).
//!
//! The two device CMake trees are **ABI-incompatible**: GCC mangles
//! `int32_t` as `long`, clang as `int`. Debug objects are plain ELF, so
//! mixing them across trees *links successfully with a corrupt ABI* instead
//! of failing loudly — see [`resolve_build_dir`] for how the default tree
//! is chosen so this can't happen silently.

use std::env;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

/// `deluge_sample_stream_*`: no C++ caller exists yet, so nothing in this
/// crate or the archived C++ closure leaves an unresolved reference into the
/// rlib. Lazy `.a` extraction would never pull the object in at all, and these
/// `#[no_mangle]` symbols would be absent from the final ELF even though the
/// crate compiled clean.
const SAMPLE_STREAM_ROOTS: [&str; 6] = [
    "deluge_sample_stream_open",
    "deluge_sample_stream_close",
    "deluge_sample_stream_set_geometry",
    "deluge_sample_stream_get_asset_id",
    "deluge_sample_stream_set_asset_id",
    "deluge_sample_stream_read_at",
];

/// The efatfs mount C-ABI. All four have real C++ callers today (via
/// `storage_manager.cpp` / `audio_file_manager.cpp`), so these roots are
/// belt-and-suspenders: harmless, and they keep the link robust against a
/// refactor that drops the last caller of any one of them.
const EFATFS_ROOTS: [&str; 4] = [
    "deluge_efatfs_mount",
    "deluge_efatfs_remount",
    "deluge_efatfs_cluster_size",
    "deluge_efatfs_is_mounted",
];

/// Emits `-Wl,-u,SYM` for each symbol, forcing it as a link root.
fn force_link_roots(syms: &[&str]) {
    for sym in syms {
        println!("cargo:rustc-link-arg=-Wl,-u,{sym}");
    }
}

fn main() {
    let manifest_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    // DelugeFirmware repo root (crate is at <root>/src/bsp/rust).
    let repo_root = manifest_dir.join("../../..").canonicalize().unwrap();
    let out_dir = PathBuf::from(env::var("OUT_DIR").unwrap());

    // Host (platform-std) build: everything below is device-only (the rza1l
    // linker script, the arm-eabi archived C++ deluge_app closure). Under
    // `--features host_app` we instead bindgen the host ABI and link the
    // host-built `deluge_app` object closure, letting the host binary reach
    // the C++ app boundary without a device build. Without that feature the
    // host path stays a no-op.
    if env::var("CARGO_CFG_TARGET_OS").as_deref() != Ok("none") {
        if env::var("CARGO_FEATURE_HOST_APP").is_ok() {
            run_host_app(&repo_root, &manifest_dir, &out_dir);
        }
        return;
    }

    run_bindgen(&repo_root, &manifest_dir, &out_dir, "armv7a-none-eabihf");
    setup_linker_script(&manifest_dir, &out_dir);
    emit_forced_link_roots();

    let (build_dir, cfg, ar) = resolve_build_dir(&repo_root);
    let app_objs_archive = archive_app_objects(&build_dir, &cfg, &ar, &out_dir);
    link_app_closure(&build_dir, &cfg, &app_objs_archive);
    emit_runtime_link_args();

    warn_if_heap_hack_active(&manifest_dir);
}

/// Generates the libdeluge POD types (`include/libdeluge/*.h`) via bindgen
/// for `clang_target`, writing `libdeluge_sys.rs` into `out_dir`. Types
/// only — we DEFINE the service functions ourselves in src/ffi.rs
/// (`#[no_mangle]`); the C contract drives the types so a layout/type
/// change is a compile error. Shared by the device path
/// (`--target=armv7a-none-eabihf`) and the `host_app` path
/// (`--target=x86_64-unknown-linux-gnu`) — same allowlist/flags otherwise,
/// so the two `mod sys`es stay structurally identical modulo target.
///
/// # Why no `-fshort-enums`
///
/// It is deliberately absent from both bindgen paths, and irrelevant to enum
/// sizing here: all 11 libdeluge FFI enums pin their underlying type
/// explicitly in the header (`enum DelugeStatus : int8_t`, and so on). An
/// explicit underlying type is authoritative in both C and C++ — no flag or
/// ABI default overrides it — so bindgen sizes them identically on every
/// target.
///
/// That explicitness is load-bearing, not incidental. Without it the arm
/// device (which defaults to short enums) and an un-flagged bindgen target
/// would silently disagree on enum width, mislaying every enum-bearing POD
/// across the FFI boundary. `--target` is passed purely for pointer width,
/// alignment and calling convention.
fn run_bindgen(repo_root: &Path, manifest_dir: &Path, out_dir: &Path, clang_target: &str) {
    let include_dir = repo_root.join("include");
    let wrapper = manifest_dir.join("wrapper.h");
    let bindings = bindgen::Builder::default()
        .header(wrapper.to_str().unwrap())
        .clang_arg(format!("-I{}", include_dir.display()))
        .allowlist_type("Deluge.*")
        .allowlist_type("RunCondition")
        .use_core()
        .clang_arg(format!("--target={clang_target}"))
        // Layouts match the app being linked on every target (explicit
        // fixed-width enums throughout), and the asserts would run host-side
        // anyway.
        .layout_tests(false)
        .generate()
        .expect("bindgen failed on libdeluge headers");
    bindings
        .write_to_file(out_dir.join("libdeluge_sys.rs"))
        .expect("write libdeluge_sys.rs");
    println!("cargo:rerun-if-changed={}", wrapper.display());
    println!("cargo:rerun-if-changed={}", include_dir.display());
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
fn archive_app_objects(build_dir: &Path, cfg: &str, ar: &Path, out_dir: &Path) -> PathBuf {
    let app_objs_dir = build_dir.join(format!("src/deluge/CMakeFiles/deluge_app.dir/{cfg}"));
    if !app_objs_dir.is_dir() {
        panic!(
            "C++ app objects not found at {}. Build them first:\n  \
             cmake --build {} --target deluge_app NE10 eyalroz_printf \
             deluge_dsp deluge_scheduler deluge_foundation deluge_midi deluge_gfx_oled deluge_gfx_grid \
             deluge_ui_core deluge_ui_oled deluge_ui_grid deluge_notify",
            app_objs_dir.display(),
            build_dir.display()
        );
    }

    let mut objs = Vec::new();
    collect_objs(&app_objs_dir, &mut objs);
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
    let deps: [(&str, &str); 12] = [
        ("src/NE10", "libNE10.a"),
        ("src/lib", "libeyalroz_printf.a"),
        ("src/deluge/dsp", "libdeluge_dsp.a"),
        ("src/OSLikeStuff", "libdeluge_scheduler.a"),
        ("src/foundation", "libdeluge_foundation.a"),
        ("src/midi", "libdeluge_midi.a"),
        ("src/gfx", "libdeluge_gfx_oled.a"),
        ("src/gfx", "libdeluge_gfx_grid.a"),
        ("src/ui", "libdeluge_ui_core.a"),
        ("src/ui", "libdeluge_ui_oled.a"),
        ("src/ui", "libdeluge_ui_grid.a"),
        ("src/notify", "libdeluge_notify.a"),
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

/// `host_app` feature: bindgen the host ABI (x86-64; every libdeluge enum is
/// pinned to an explicit fixed-width underlying type in its header, so this
/// matches build-embassy-hostapp's CMake config byte-for-byte regardless of
/// `-fshort-enums`, which neither build passes) into the real `mod sys`, then
/// archive the host-built C++ `deluge_app` object closure and emit link
/// directives so the crate reaches the linker against real provider-symbol
/// references.
fn run_host_app(repo_root: &Path, manifest_dir: &Path, out_dir: &Path) {
    let build_dir = resolve_host_build_dir(repo_root);

    run_bindgen(repo_root, manifest_dir, out_dir, "x86_64-unknown-linux-gnu");
    emit_host_forced_link_roots();

    let app_objs_dir = host_app_objs_dir(&build_dir);
    let objs = archive_host_app_objects(&app_objs_dir, out_dir);
    emit_host_staleness_hash(&app_objs_dir, &objs, out_dir);
    link_host_app_closure(&build_dir, &out_dir.join("libdeluge_app_objs.a"));
    emit_host_runtime_link_args();

    println!("cargo:rerun-if-changed={}", app_objs_dir.display());
}

/// Resolves the CMake host tree, defaulting to `build-embassy-hostapp`
/// (`cmake -S sim -B build-embassy-hostapp -DDELUGE_HOST_EMBASSY=…`).
///
/// Overridable via `DELUGE_HOSTAPP_BUILD_DIR` so CI and the sanitizer
/// harnesses can point at a differently-named tree (e.g. a clang+TSan
/// `build-embassy-hostapp-tsan`, see HOST_HARNESS.md). The
/// `rerun-if-env-changed` is what makes repointing safe: without it Cargo
/// holds no directive from a prior run mentioning the new dir, so the link
/// would silently keep using whatever was last archived.
fn resolve_host_build_dir(repo_root: &Path) -> PathBuf {
    println!("cargo:rerun-if-env-changed=DELUGE_HOSTAPP_BUILD_DIR");
    env::var("DELUGE_HOSTAPP_BUILD_DIR")
        .map(PathBuf::from)
        .unwrap_or_else(|_| repo_root.join("build-embassy-hostapp"))
}

/// The host tree's object directory. Its generator is single-config Ninja,
/// unlike the device tree's multi-config `…/{cfg}` layout, so objects land
/// directly under `deluge_app.dir` with no Debug/Release subdir.
fn host_app_objs_dir(build_dir: &Path) -> PathBuf {
    build_dir.join("app/CMakeFiles/deluge_app.dir")
}

/// Forces the host link roots, and lifts lld's error cap.
///
/// The cap is lifted because this link is expected to fail on undefined
/// provider symbols while the host-side callers are still being wired up —
/// one `cargo build` should surface the complete set rather than truncating
/// after the first batch.
///
/// `deluge_app_init` needs `-u` for the same reason the device path's roots
/// do: no host Rust code calls it, so without a root lld never extracts
/// `deluge.cpp.o`, and the default `--gc-sections` strips everything down to
/// the C++ global-constructor subset. Forcing it keeps its whole
/// transitively-reachable graph.
fn emit_host_forced_link_roots() {
    println!("cargo:rustc-link-arg=-Wl,--error-limit=0");
    force_link_roots(&["deluge_app_init"]);
    force_link_roots(&SAMPLE_STREAM_ROOTS);
    force_link_roots(&EFATFS_ROOTS);
}

/// Archives the host-built `deluge_app` object closure into
/// `libdeluge_app_objs.a`, returning the sorted object list for the staleness
/// hash. Panics with the exact ninja command if the tree hasn't been built.
///
/// Registers `rerun-if-changed` per object path for the same reason the device
/// path does: a directory-level watch only fires on add/remove, so editing a
/// `.cpp` and rebuilding in place would otherwise leave a stale link.
fn archive_host_app_objects(app_objs_dir: &Path, out_dir: &Path) -> Vec<PathBuf> {
    if !app_objs_dir.is_dir() {
        panic!(
            "host C++ app objects not found at {}. Build them first:\n  \
             ninja -C <host tree> deluge_app NE10 eyalroz_printf deluge_dsp \
             deluge_scheduler deluge_foundation deluge_midi deluge_gfx_oled deluge_gfx_grid \
             deluge_ui_core deluge_ui_oled deluge_ui_grid deluge_notify",
            app_objs_dir.display(),
        );
    }

    let mut objs = Vec::new();
    collect_objs(app_objs_dir, &mut objs);
    objs.sort();
    for o in &objs {
        println!("cargo:rerun-if-changed={}", o.display());
    }
    let app_objs_archive = out_dir.join("libdeluge_app_objs.a");
    let _ = fs::remove_file(&app_objs_archive);
    // Host `ar` (not arm-none-eabi-ar): these are x86-64 ELF objects.
    let status = Command::new("ar")
        .arg("crs")
        .arg(&app_objs_archive)
        .args(&objs)
        .status()
        .expect("run host ar");
    assert!(status.success(), "archiving host deluge_app objects failed");
    objs
}

/// Threads a content hash of the object closure through `cargo:rustc-env`, to
/// force a relink when the objects change underneath an unchanged path.
///
/// This is load-bearing, not a defensive extra. If a reconfigured tree (say,
/// flipping on `-fsanitize=thread`) produces objects whose mtimes Cargo's
/// `rerun-if-changed` fails to notice, the archive above is rebuilt from fresh
/// objects — but every `rustc-link-arg` below is byte-identical to last run,
/// because the archive's *path* didn't change. Cargo compares a build script's
/// emitted metadata run over run, sees no difference, and can skip relinking
/// the final binary even though the archive's contents just changed. Emitting
/// the hash makes that change visible in the metadata.
///
/// The failure this prevents: an instrumented `.o` on disk, an uninstrumented
/// archive in the link, and no way to tell short of wiping `target/`.
fn emit_host_staleness_hash(app_objs_dir: &Path, objs: &[PathBuf], out_dir: &Path) {
    let content_hash = hash_objs_content(objs);
    let hash_str = format!("{content_hash:016x}");
    let hash_stamp = out_dir.join("host_app_objs_hash.txt");
    let prev_hash = fs::read_to_string(&hash_stamp).ok();
    if prev_hash.as_deref() != Some(hash_str.as_str()) {
        println!(
            "cargo:warning=host_app: deluge_app object closure at {} changed ({} -> {}); forcing a relink",
            app_objs_dir.display(),
            prev_hash.as_deref().unwrap_or("<none>"),
            hash_str
        );
    }
    fs::write(&hash_stamp, &hash_str).expect("write host_app_objs_hash.txt");
    println!("cargo:rustc-env=DELUGE_APP_OBJS_HASH={hash_str}");
}

/// Emits the link args for the host app archive plus the portable static-lib
/// closure, as one `--start-group` for the mutual C++/Rust references.
///
/// The dep paths mirror the host tree's own layout, which differs from the
/// device tree's: NE10 sits at the build root and dsp under `app/`, not under
/// `src/deluge/`.
fn link_host_app_closure(build_dir: &Path, app_objs_archive: &Path) {
    let deps: [(&str, &str); 12] = [
        (".", "libNE10.a"),
        ("printf", "libeyalroz_printf.a"),
        ("app/dsp", "libdeluge_dsp.a"),
        ("scheduler", "libdeluge_scheduler.a"),
        ("foundation", "libdeluge_foundation.a"),
        ("midi", "libdeluge_midi.a"),
        ("gfx", "libdeluge_gfx_oled.a"),
        ("gfx", "libdeluge_gfx_grid.a"),
        ("ui", "libdeluge_ui_core.a"),
        ("ui", "libdeluge_ui_oled.a"),
        ("ui", "libdeluge_ui_grid.a"),
        ("notify", "libdeluge_notify.a"),
    ];

    println!("cargo:rustc-link-arg=-Wl,--start-group");
    println!("cargo:rustc-link-arg={}", app_objs_archive.display());
    for (dir, lib) in deps {
        let p = build_dir.join(dir).join(lib);
        assert!(p.is_file(), "missing host dep archive {}", p.display());
        println!("cargo:rustc-link-arg={}", p.display());
    }
    println!("cargo:rustc-link-arg=-Wl,--end-group");
}

/// Re-adds the C/C++ runtime the host app needs, since rustc passes
/// `-nodefaultlibs`: libstdc++ (`std::`, vtables), libgcc (compiler helpers),
/// libc and libm. Grouped for the libstdc++ ↔ libc ↔ libgcc circular refs.
///
/// Unlike the device's arm-eabi/newlib group, glibc pulls libsupc++ in via
/// libstdc++ and needs no unhosted syscall stubs — hence no `-lsupc++` or
/// `-lnosys` here.
fn emit_host_runtime_link_args() {
    println!("cargo:rustc-link-arg=-Wl,--start-group");
    for l in ["-lstdc++", "-lm", "-lc", "-lgcc"] {
        println!("cargo:rustc-link-arg={l}");
    }
    println!("cargo:rustc-link-arg=-Wl,--end-group");
}

/// Content hash over a sorted object closure (path + bytes of each `.o`), used
/// by the `host_app` path to force a relink whenever the archived objects'
/// CONTENT changes even if Cargo's own `rerun-if-changed` mtime tracking of
/// the individual files does not (see the staleness comment at the call
/// site). Not cryptographic — `DefaultHasher` (SipHash) is fine for a
/// same-machine, same-run change/no-change signal; the whole closure is
/// tens of MB and hashes in well under a second.
fn hash_objs_content(objs: &[PathBuf]) -> u64 {
    use std::hash::Hasher;
    let mut hasher = std::collections::hash_map::DefaultHasher::new();
    for o in objs {
        hasher.write(o.to_string_lossy().as_bytes());
        let bytes = fs::read(o)
            .unwrap_or_else(|e| panic!("failed to read {} for staleness hash: {e}", o.display()));
        hasher.write(&bytes);
    }
    hasher.finish()
}

fn collect_objs(dir: &Path, out: &mut Vec<PathBuf>) {
    for entry in fs::read_dir(dir).unwrap() {
        let p = entry.unwrap().path();
        if p.is_dir() {
            collect_objs(&p, out);
        } else if p.extension().is_some_and(|e| e == "obj" || e == "o") {
            out.push(p);
        }
    }
}
