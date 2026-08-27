//! Bindgen the libdeluge host ABI + link the pre-built host_app C++ object
//! closure. A trimmed copy of `../build.rs`'s `run_host_app`/`run_bindgen`/
//! `collect_objs`/`hash_objs_content` (device-path code and the `DELUGE_BUILD_DIR`
//! ARM path removed — this package only ever builds for the host). Kept as a
//! separate copy rather than a shared helper crate: `../build.rs` is itself a
//! `[[bin]]`-only build script with no library surface to import, and duplicating
//! ~150 lines of straight-line archiving/linking logic is far lower risk here than
//! inventing a new shared build-script crate for a single (harness-only) consumer.
//! Mirrors `../lens1_vt_sim/build.rs` (same `DELUGE_HOSTAPP_BUILD_DIR`
//! object archiving + `-Wl,-u,deluge_app_init` GC root) — only the log-message
//! crate label below differs.
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
             deluge_scheduler deluge_foundation deluge_midi deluge_gfx_oled deluge_gfx_grid \
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

fn collect_objs(dir: &std::path::Path, out: &mut Vec<PathBuf>) {
    for entry in fs::read_dir(dir).unwrap() {
        let p = entry.unwrap().path();
        if p.is_dir() {
            collect_objs(&p, out);
        } else if p.extension().is_some_and(|e| e == "obj" || e == "o") {
            out.push(p);
        }
    }
}
