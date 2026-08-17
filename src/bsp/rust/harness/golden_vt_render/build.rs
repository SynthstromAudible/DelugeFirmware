//! Bindgen the libdeluge host ABI + link the pre-built host_app C++ object
//! closure. A trimmed copy of `../build.rs`'s `run_host_app`/`run_bindgen`/
//! `collect_objs`/`hash_objs_content` (device-path code and the `DELUGE_BUILD_DIR`
//! ARM path removed — this package only ever builds for the host). Kept as a
//! separate copy rather than a shared helper crate: `../build.rs` is itself a
//! `[[bin]]`-only build script with no library surface to import, and duplicating
//! ~150 lines of straight-line archiving/linking logic is far lower risk here than
//! inventing a new shared build-script crate for a single (harness-only) consumer.
//!
//! The object-archiving half now mirrors **`../lens1_vt_sim/build.rs`**, not `../build.rs`:
//! same `DELUGE_HOSTAPP_BUILD_DIR` archiving and `-Wl,-u,deluge_app_init` GC root, plus its
//! orphan filter, build-dir provenance guard and surviving-object floor. Only the log-message
//! crate label differs, deliberately, so the two copies stay diffable.
//!
//! Why the filter matters *here*: this harness is the before/after byte-identity gate for the
//! storage work, and `ninja` never removes a `.o` whose `.cpp` was deleted or relocated. Without
//! the filter a stale object is archived silently and can win the link — so the gate would be
//! measuring a tree that no longer exists. Confirmed empirically before porting it: a planted
//! zero-byte orphan was archived with no build-script warning at all, surfacing only as
//! `rust-lld: … is neither ET_REL nor LLVM bitcode`.
use std::env;
use std::fs;
use std::path::PathBuf;
use std::process::Command;

fn main() {
    let manifest_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    // DelugeFirmware repo root (crate is at <root>/src/bsp/rust/harness/golden_vt_render).
    let repo_root = manifest_dir.join("../../../../..").canonicalize().unwrap();
    let out_dir = PathBuf::from(env::var("OUT_DIR").unwrap());

    run_bindgen(&repo_root, &manifest_dir, &out_dir);

    // See ../build.rs's identical comment: lift lld's error cap and force
    // `deluge_app_init` as a link root so --gc-sections keeps its whole
    // transitively-reachable graph instead of stripping to the C++ global-ctor
    // subset.
    //
    // `--error-limit=0` is an lld-only flag (raises lld's default 20-diagnostic
    // cap so a link failure prints everything). The x86-64 host oracle links
    // with lld and accepts it; the arm-linux/qemu oracle links with GNU bfd `ld`
    // (driven by arm-linux-gnueabihf-gcc), which has no such cap and rejects the
    // option — so emit it only off the arm-linux path.
    let target = env::var("TARGET").unwrap_or_default();
    if target != "armv7-unknown-linux-gnueabihf" {
        println!("cargo:rustc-link-arg=-Wl,--error-limit=0");
    }
    println!("cargo:rustc-link-arg=-Wl,-u,deluge_app_init");
    // No C++ caller of `deluge_sample_stream_*` exists yet — unlike `deluge_app_init`
    // above, nothing has an unresolved reference into `deluge_sample_stream`'s rlib,
    // so ordinary lazy `.a` extraction would never pull its object in, and rustc's
    // default `--gc-sections` would then prune each unreached `#[no_mangle]`
    // function's own section even after the object is pulled. Force EACH of the six
    // ABI entry points as a link root — proves the ABI compiles+links end-to-end
    // ahead of a real caller.
    for sym in [
        "deluge_sample_stream_open",
        "deluge_sample_stream_close",
        "deluge_sample_stream_set_geometry",
        "deluge_sample_stream_get_asset_id",
        "deluge_sample_stream_set_asset_id",
        "deluge_sample_stream_read_at",
    ] {
        println!("cargo:rustc-link-arg=-Wl,-u,{sym}");
    }

    println!("cargo:rerun-if-env-changed=DELUGE_HOSTAPP_BUILD_DIR");
    let build_dir = env::var("DELUGE_HOSTAPP_BUILD_DIR")
        .map(PathBuf::from)
        .unwrap_or_else(|_| repo_root.join("build-embassy-hostapp"));
    let app_objs_dir = build_dir.join("app/CMakeFiles/deluge_app.dir");
    if !app_objs_dir.is_dir() {
        panic!(
            "host C++ app objects not found at {}. Build them first:\n  \
             ninja -C {} deluge_app NE10 eyalroz_printf deluge_dsp \
             deluge_scheduler deluge_foundation deluge_midi",
            app_objs_dir.display(),
            build_dir.display()
        );
    }

    // `collect_objs`'s orphan filter resolves every object's source against
    // `<repo_root>/sim` (see `assert_build_dir_matches_repo_root`'s doc comment for why
    // `sim/`, not `<repo_root>` itself) — but ONLY if `build_dir` was actually configured
    // from THIS checkout. If `DELUGE_HOSTAPP_BUILD_DIR` instead points at a tree
    // CMake-configured from a different checkout (a stale absolute path, a different
    // worktree, a CI artifact copied over), every object's derived source path is simply
    // wrong, the filter treats the entire closure as orphaned, and the result is hundreds of
    // undefined symbols — i.e. the filter added to fix exactly that failure shape instead
    // silently causing it again. Catch the mismatch here, loudly.
    assert_build_dir_matches_repo_root(&build_dir, &repo_root);

    let mut objs = Vec::new();
    let mut skipped = Vec::new();
    collect_objs(
        &app_objs_dir,
        &app_objs_dir,
        &repo_root,
        &mut objs,
        &mut skipped,
    );
    objs.sort();
    for o in &objs {
        println!("cargo:rerun-if-changed={}", o.display());
    }
    // A `cargo:warning=` alone (emitted per-skip inside `collect_objs`) scrolls past in normal
    // build output and is not re-emitted on a build where `build.rs` doesn't rerun — too quiet
    // for what a mass-skip actually costs. A handful of skips from genuinely deleted sources
    // (this filter's purpose) is expected and fine; a skip count large enough to push survivors
    // below this floor means something is systemically wrong with path resolution rather than a
    // few stale objects, and must fail the build instead of silently archiving an incomplete
    // closure.
    //
    // Unconditional — NOT gated on `!skipped.is_empty()`. An interrupted/truncated `ninja` can
    // leave a tree with a handful of valid objects and ZERO orphans (the missing objects were
    // never written, so `collect_objs` never sees a path to reject), which would otherwise sail
    // through and produce the very undefined-symbol wall this guard exists to explain.
    const MIN_SURVIVING_OBJECTS: usize = 300;
    if objs.len() < MIN_SURVIVING_OBJECTS {
        panic!(
            "golden_vt_render: only {} of {} objects under {} survived the orphan-object filter \
             — below the sanity floor of {MIN_SURVIVING_OBJECTS}. This means one of two things: \
             (1) {} objects were skipped as orphans, which almost certainly means \
             DELUGE_HOSTAPP_BUILD_DIR points at a build tree that doesn't match this checkout, or \
             the filter's src/deluge-relative path derivation is wrong for this tree's layout; or \
             (2) few/no objects were skipped but the build tree itself is truncated or incomplete \
             (e.g. an interrupted `ninja`), so most objects were never produced in the first \
             place. Either way this is NOT a case of that many sources being legitimately \
             deleted. Skipped objects: {:#?}",
            objs.len(),
            objs.len() + skipped.len(),
            app_objs_dir.display(),
            skipped.len(),
            skipped,
        );
    }
    let app_objs_archive = out_dir.join("libdeluge_app_objs.a");
    let _ = fs::remove_file(&app_objs_archive);
    let status = Command::new("ar")
        .arg("crs")
        .arg(&app_objs_archive)
        .args(&objs)
        .status()
        .expect("run host ar");
    assert!(status.success(), "archiving host deluge_app objects failed");

    // Same staleness-hash trick as ../build.rs (see its comment for the full
    // rationale): force a relink when the archived objects' CONTENT changes even
    // if Cargo's mtime-based rerun-if-changed didn't notice (e.g. a reconfigured
    // CMake tree).
    let content_hash = hash_objs_content(&objs);
    let hash_str = format!("{content_hash:016x}");
    let hash_stamp = out_dir.join("host_app_objs_hash.txt");
    let prev_hash = fs::read_to_string(&hash_stamp).ok();
    if prev_hash.as_deref() != Some(hash_str.as_str()) {
        println!(
            "cargo:warning=golden_vt_render: deluge_app object closure at {} changed ({} -> {}); forcing a relink",
            app_objs_dir.display(),
            prev_hash.as_deref().unwrap_or("<none>"),
            hash_str
        );
    }
    fs::write(&hash_stamp, &hash_str).expect("write host_app_objs_hash.txt");
    println!("cargo:rustc-env=DELUGE_APP_OBJS_HASH={hash_str}");

    let deps: [(&str, &str); 6] = [
        (".", "libNE10.a"),
        ("printf", "libeyalroz_printf.a"),
        ("app/dsp", "libdeluge_dsp.a"),
        ("scheduler", "libdeluge_scheduler.a"),
        ("foundation", "libdeluge_foundation.a"),
        ("midi", "libdeluge_midi.a"),
    ];

    println!("cargo:rustc-link-arg=-Wl,--start-group");
    println!("cargo:rustc-link-arg={}", app_objs_archive.display());
    for (dir, lib) in deps {
        let p = build_dir.join(dir).join(lib);
        assert!(p.is_file(), "missing host dep archive {}", p.display());
        println!("cargo:rustc-link-arg={}", p.display());
    }
    println!("cargo:rustc-link-arg=-Wl,--end-group");

    println!("cargo:rustc-link-arg=-Wl,--start-group");
    for l in ["-lstdc++", "-lm", "-lc", "-lgcc"] {
        println!("cargo:rustc-link-arg={l}");
    }
    println!("cargo:rustc-link-arg=-Wl,--end-group");

    println!("cargo:rerun-if-changed={}", app_objs_dir.display());
}

/// Bindgen the libdeluge headers for whichever host target this crate is being
/// built for — see `../build.rs`'s `run_bindgen` for the full rationale. The
/// clang target is taken from cargo's `TARGET` triple rather than hardcoded, so
/// the same build script serves both the x86-64 host oracle
/// (`x86_64-unknown-linux-gnu`) and the arm-linux/qemu oracle
/// (`armv7-unknown-linux-gnueabihf`); a cargo target triple is a valid clang
/// triple, and the POD layouts differ between ILP32 arm and LP64 x86-64, so the
/// bindgen target must track the build target for the FFI structs to match.
fn run_bindgen(
    repo_root: &std::path::Path,
    manifest_dir: &std::path::Path,
    out_dir: &std::path::Path,
) {
    let include_dir = repo_root.join("include");
    let wrapper = manifest_dir.join("wrapper.h");
    let clang_target = env::var("TARGET").expect("cargo sets TARGET for build scripts");
    let bindings = bindgen::Builder::default()
        .header(wrapper.to_str().unwrap())
        .clang_arg(format!("-I{}", include_dir.display()))
        .allowlist_type("Deluge.*")
        .allowlist_type("RunCondition")
        .use_core()
        // See ../build.rs: NO `-fshort-enums` — every libdeluge enum pins its
        // underlying type explicitly in its header, so both sides already
        // agree on each enum's width regardless of the flag.
        .clang_arg(format!("--target={clang_target}"))
        .layout_tests(false)
        .generate()
        .expect("bindgen failed on libdeluge headers");
    bindings
        .write_to_file(out_dir.join("libdeluge_sys.rs"))
        .expect("write libdeluge_sys.rs");
    println!("cargo:rerun-if-changed={}", wrapper.display());
    println!("cargo:rerun-if-changed={}", include_dir.display());
}

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

/// Walks `dir` (recursively) collecting every `.o`/`.obj` under `base` (the `deluge_app.dir`
/// root, constant across the recursion — needed to compute each object's path relative to it).
/// Skips orphans: `ninja` never removes a `.o` left behind by a `.cpp` that was since deleted
/// or relocated, and archiving a stale object drags in references to symbols that no longer
/// exist anywhere in the tree. Each object's source lives under `<repo_root>/src/deluge/` at the
/// same relative subpath with the trailing `.o`/`.obj` stripped; if that source file is gone,
/// the object is an orphan: it is skipped (with a `cargo:warning=` naming it, and pushed onto
/// `skipped` for the caller's sanity-floor check) instead of being archived.
///
/// Ported from `../lens1_vt_sim/build.rs`, where the filter was added after a stale object cost
/// a long detour. Kept signature-identical so the two copies stay diffable.
///
/// Known blind spot, not fixed here: if CMake ever compiles a source from OUTSIDE this target's
/// own directory tree, it mangles the object's relative path with a `__/` per parent-escape,
/// which then resolves against `src/deluge` to a path that never exists — the object reads as an
/// orphan and is skipped even though its source is alive. No such object exists in this tree
/// today, so the `src/deluge`-only derivation is sufficient; the caller's floor check is the
/// backstop if that ever changes.
fn collect_objs(
    base: &std::path::Path,
    dir: &std::path::Path,
    repo_root: &std::path::Path,
    out: &mut Vec<PathBuf>,
    skipped: &mut Vec<PathBuf>,
) {
    for entry in fs::read_dir(dir).unwrap() {
        let p = entry.unwrap().path();
        if p.is_dir() {
            collect_objs(base, &p, repo_root, out, skipped);
        } else if p.extension().is_some_and(|e| e == "obj" || e == "o") {
            let rel = p.strip_prefix(base).unwrap();
            let src_path = repo_root.join("src/deluge").join(rel.with_extension(""));
            if !src_path.is_file() {
                println!(
                    "cargo:warning=golden_vt_render: skipping orphan object {} (source {} no longer exists)",
                    p.display(),
                    src_path.display()
                );
                skipped.push(p);
                continue;
            }
            out.push(p);
        }
    }
}

/// Verifies `build_dir` was actually CMake-configured from `repo_root`, not from some other
/// checkout (a stale `DELUGE_HOSTAPP_BUILD_DIR`, a different worktree, a copied CI artifact).
/// Without this check, [`collect_objs`]'s orphan filter would resolve every object's derived
/// source path against the WRONG tree, treat the entire object closure as orphaned, and silently
/// archive nothing — surfacing later as a wall of undefined symbols with no obvious cause.
///
/// Reads `CMAKE_HOME_DIRECTORY:INTERNAL=<path>` out of `build_dir`'s `CMakeCache.txt` — the
/// CMake source root that configured this build tree — and compares it (canonicalized) against
/// `<repo_root>/sim`: this repo's host C++ app is CMake-rooted at `sim/`, not at the repo root.
fn assert_build_dir_matches_repo_root(build_dir: &std::path::Path, repo_root: &std::path::Path) {
    let cache_path = build_dir.join("CMakeCache.txt");
    let cache = fs::read_to_string(&cache_path)
        .unwrap_or_else(|e| panic!("failed to read {}: {e}", cache_path.display()));
    let home_dir = cache
        .lines()
        .find_map(|l| l.strip_prefix("CMAKE_HOME_DIRECTORY:INTERNAL="))
        .unwrap_or_else(|| {
            panic!(
                "no CMAKE_HOME_DIRECTORY:INTERNAL= line in {} — not a CMake build tree?",
                cache_path.display()
            )
        });
    let home_dir = PathBuf::from(home_dir).canonicalize().unwrap_or_else(|e| {
        panic!(
            "{}'s CMAKE_HOME_DIRECTORY ({home_dir}) does not exist: {e}",
            cache_path.display()
        )
    });
    let expected = repo_root.join("sim").canonicalize().unwrap_or_else(|e| {
        panic!(
            "expected CMake source root {}/sim missing: {e}",
            repo_root.display()
        )
    });
    if home_dir != expected {
        panic!(
            "golden_vt_render: {} was configured from {} (CMAKE_HOME_DIRECTORY), not this \
             checkout's {} — DELUGE_HOSTAPP_BUILD_DIR points at the wrong build tree. Using it \
             would resolve every object's source path against the wrong repo and (via the orphan \
             filter) drop the entire object closure as orphaned.",
            build_dir.display(),
            home_dir.display(),
            expected.display(),
        );
    }
}
