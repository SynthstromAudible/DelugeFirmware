//! Links the host-built C++ `deluge_app` and its integration scenarios into a host binary: this
//! crate's own under `host_app`, and `harness/golden_vt_render`, whose build script includes this
//! file and [`link`](super::link) through `#[path]` (link arguments do not reach dependents).
//!
//! The objects come from the CMake host tree `./dbt test` builds; this script never invokes CMake.

use std::env;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

use super::link::{
    EFATFS_ROOTS, SAMPLE_STREAM_ROOTS, collect_objs, force_link_roots, hash_objs_content,
};

/// A closure with fewer objects than this is incomplete: an interrupted `ninja`, or a build tree
/// whose sources the orphan filter could not find. Linking it would fail with a wall of undefined
/// symbols that says nothing about why.
const MIN_SURVIVING_OBJECTS: usize = 300;

/// Archives the host app's objects and emits every link argument the binary needs.
pub fn link(repo_root: &Path, out_dir: &Path) {
    let build_dir = resolve_build_dir(repo_root);
    assert_build_dir_matches_repo_root(&build_dir, repo_root);

    // lld stops after 20 errors by default; show the whole list. The arm-linux (qemu) link uses GNU
    // ld, which has no cap and rejects the flag.
    if env::var("TARGET").as_deref() != Ok("armv7-unknown-linux-gnueabihf") {
        println!("cargo:rustc-link-arg=-Wl,--error-limit=0");
    }
    // No Rust code calls `deluge_app_init` by name, so without a root lld never extracts
    // `deluge.cpp.o`, and `--gc-sections` strips the app down to its global constructors.
    force_link_roots(&["deluge_app_init"]);
    force_link_roots(&SAMPLE_STREAM_ROOTS);
    force_link_roots(&EFATFS_ROOTS);

    let app_objs_dir = build_dir.join("app/CMakeFiles/deluge_app.dir");
    let scenario_objs_dir = build_dir.join("scenarios/CMakeFiles/deluge_scenarios.dir");
    let objs = collect_closure(repo_root, &app_objs_dir, &scenario_objs_dir);
    let archive = out_dir.join("libdeluge_app_objs.a");
    let _ = fs::remove_file(&archive);
    // Host `ar`: these are host objects.
    let status = Command::new("ar")
        .arg("crs")
        .arg(&archive)
        .args(&objs)
        .status()
        .expect("run host ar");
    assert!(
        status.success(),
        "archiving the host deluge_app objects failed"
    );

    emit_staleness_hash(&app_objs_dir, &objs, out_dir);
    link_closure(&build_dir, &archive);
    link_runtime();

    println!("cargo:rerun-if-changed={}", app_objs_dir.display());
    println!("cargo:rerun-if-changed={}", scenario_objs_dir.display());
}

/// The CMake host tree: `DELUGE_HOSTAPP_BUILD_DIR`, else `build/host-app` (the tree `./dbt test`
/// builds), else the older `build-embassy-hostapp` where only that one exists.
///
/// The `rerun-if-env-changed` is what makes repointing the variable safe: without it Cargo has no
/// record of the old value and would keep linking the last archive.
fn resolve_build_dir(repo_root: &Path) -> PathBuf {
    println!("cargo:rerun-if-env-changed=DELUGE_HOSTAPP_BUILD_DIR");
    env::var("DELUGE_HOSTAPP_BUILD_DIR")
        .map(PathBuf::from)
        .unwrap_or_else(|_| {
            let current = repo_root.join("build/host-app");
            let older = repo_root.join("build-embassy-hostapp");
            if !current.is_dir() && older.is_dir() {
                older
            } else {
                current
            }
        })
}

/// Panics unless `build_dir` was configured from this checkout.
///
/// The orphan filter resolves every object's source against this checkout, so a tree configured
/// from another one (a stale `DELUGE_HOSTAPP_BUILD_DIR`, another worktree) would have its whole
/// closure dropped as orphans. The host app's CMake root is `sim/`, not the repository root.
fn assert_build_dir_matches_repo_root(build_dir: &Path, repo_root: &Path) {
    let cache_path = build_dir.join("CMakeCache.txt");
    let Ok(cache) = fs::read_to_string(&cache_path) else {
        panic!(
            "no CMake host tree at {}. Build it first: ./dbt test --build-only golden-vt-render",
            build_dir.display()
        );
    };
    let home_dir = cache
        .lines()
        .find_map(|l| l.strip_prefix("CMAKE_HOME_DIRECTORY:INTERNAL="))
        .unwrap_or_else(|| {
            panic!(
                "no CMAKE_HOME_DIRECTORY in {} — not a CMake build tree?",
                cache_path.display()
            )
        });
    let home_dir = PathBuf::from(home_dir).canonicalize().unwrap_or_else(|e| {
        panic!(
            "{}'s CMAKE_HOME_DIRECTORY ({home_dir}) does not exist: {e}",
            cache_path.display()
        )
    });
    let expected = repo_root.join("sim").canonicalize().unwrap();
    assert!(
        home_dir == expected,
        "{} was configured from {}, not this checkout's {}: DELUGE_HOSTAPP_BUILD_DIR names the \
         wrong tree",
        build_dir.display(),
        home_dir.display(),
        expected.display(),
    );
}

/// The app's and the scenarios' objects whose sources still exist, sorted, each registered with
/// `rerun-if-changed` (a directory watch only fires when an object is added or removed).
fn collect_closure(
    repo_root: &Path,
    app_objs_dir: &Path,
    scenario_objs_dir: &Path,
) -> Vec<PathBuf> {
    for dir in [app_objs_dir, scenario_objs_dir] {
        assert!(
            dir.is_dir(),
            "host C++ objects not found at {}. Build them first: \
             ./dbt test --build-only golden-vt-render",
            dir.display()
        );
    }
    let mut objs = Vec::new();
    let mut skipped = Vec::new();
    let app_src = repo_root.join("src/deluge");
    collect_objs(
        app_objs_dir,
        app_objs_dir,
        &app_src,
        &mut objs,
        &mut skipped,
    );
    let scenario_src = repo_root.join("tests/integration/scenarios");
    collect_objs(
        scenario_objs_dir,
        scenario_objs_dir,
        &scenario_src,
        &mut objs,
        &mut skipped,
    );
    // Checked even with nothing skipped: a truncated tree has too few objects and no orphans.
    assert!(
        objs.len() >= MIN_SURVIVING_OBJECTS,
        "only {} host objects survived the orphan filter ({} skipped): the build tree is \
         incomplete, or was not built from this checkout. Skipped: {skipped:#?}",
        objs.len(),
        skipped.len(),
    );
    objs.sort();
    for o in &objs {
        println!("cargo:rerun-if-changed={}", o.display());
    }
    objs
}

/// Threads [`hash_objs_content`] through `rustc-env`, so rebuilt objects force a relink.
fn emit_staleness_hash(app_objs_dir: &Path, objs: &[PathBuf], out_dir: &Path) {
    let hash = format!("{:016x}", hash_objs_content(objs));
    let stamp = out_dir.join("host_app_objs_hash.txt");
    let prev = fs::read_to_string(&stamp).ok();
    if prev.as_deref() != Some(hash.as_str()) {
        println!(
            "cargo:warning=host app objects at {} changed ({} -> {hash}); relinking",
            app_objs_dir.display(),
            prev.as_deref().unwrap_or("<none>"),
        );
    }
    fs::write(&stamp, &hash).expect("write host_app_objs_hash.txt");
    println!("cargo:rustc-env=DELUGE_APP_OBJS_HASH={hash}");
}

/// Links the app archive and its static libraries as one group, because the C++ and Rust sides
/// call each other. The host tree's layout differs from the device tree's: the DSP libraries sit
/// under `app/`.
fn link_closure(build_dir: &Path, archive: &Path) {
    const DEPS: [(&str, &str); 12] = [
        ("printf", "libeyalroz_printf.a"),
        ("app/dsp", "libdeluge_dsp.a"),
        ("app/dsp-next", "libdeluge-dsp-fixed.a"),
        ("app/dsp-next", "libdeluge-dsp-float.a"),
        ("synth", "libdeluge_synth.a"),
        ("foundation", "libdeluge_foundation.a"),
        ("serialization", "libdeluge_serialization.a"),
        ("music-theory", "libdeluge_music_theory.a"),
        ("deluge-project", "libdeluge_project.a"),
        ("instruments-deluge", "libdeluge_instrument.a"),
        ("effects", "libdeluge_effects.a"),
        ("midi", "libdeluge_midi.a"),
    ];
    println!("cargo:rustc-link-arg=-Wl,--start-group");
    println!("cargo:rustc-link-arg={}", archive.display());
    for (dir, lib) in DEPS {
        let p = build_dir.join(dir).join(lib);
        assert!(p.is_file(), "missing host library {}", p.display());
        println!("cargo:rustc-link-arg={}", p.display());
        // A change confined to one of these libraries must relink: cargo tracks only what it is told to.
        println!("cargo:rerun-if-changed={}", p.display());
    }
    println!("cargo:rustc-link-arg=-Wl,--end-group");
}

/// Re-adds the C and C++ runtime, which rustc's `-nodefaultlibs` leaves out, grouped for the
/// libstdc++ ↔ libc ↔ libgcc cycle. glibc's libstdc++ brings libsupc++ with it.
fn link_runtime() {
    println!("cargo:rustc-link-arg=-Wl,--start-group");
    for l in ["-lstdc++", "-lm", "-lc", "-lgcc"] {
        println!("cargo:rustc-link-arg={l}");
    }
    println!("cargo:rustc-link-arg=-Wl,--end-group");
}
