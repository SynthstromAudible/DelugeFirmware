//! Build script for `deluge_sample_convert`.
//!
//! Compiles the app's `convert.cpp`, `stitch.cpp` and `audio_format_helpers.cpp`, plus the
//! `extern "C"` shim in `cpp/shim.cpp`, in one of three modes chosen by the target:
//!
//! 1. **Host** (default): compiles all four TUs for x86 with `cc` and SIMDe, linked into this
//!    crate so its tests can round-trip them. Also compiles each TU for armv7a-NEON (`-c` only)
//!    and checks the objects are ARM ELF and that `convert`'s carries real NEON codegen
//!    (`vcvt.s32.f32`); the result is stamped to `OUT_DIR/arm_compile_result.txt` for a test.
//! 2. **Device** (`target_os = "none"`, as a BSP dependency): compiles only the shim, with the
//!    device flags and the toolchain's own `<arm_neon.h>`; see [`build_device`].
//! 3. **Host with `app_convert`**: compiles only the shim, like the device, for a final link that
//!    already supplies `convert_word`, `stitch_boundaries` and the helpers.
//!
//! argon and SIMDe are header-only. [`fetch_pinned`] shallow-fetches them into a gitignored
//! `third_party/` at the revisions `cmake/deps.cmake` pins, so a clean checkout builds without any
//! CMake build (it needs network the first time). The device mode needs only argon.
//!
//! Flags, defines and include dirs mirror `tests/unit/audio_stream/CMakeLists.txt`,
//! `sim/CMakeLists.txt` and `scripts/cmake/CMakeToolchainDeluge.cmake`.
use std::path::{Path, PathBuf};
use std::process::Command;

/// `CMakeToolchainDeluge.cmake`'s `ARCH_FLAGS`, plus `-funsafe-math-optimizations` (required for
/// NEON rather than VFPv3 floating point).
///
/// Shared by the host verify compile and the device compile so the two cannot drift apart.
const ARM_ARCH_FLAGS: [&str; 7] = [
    "-mcpu=cortex-a9",
    "-mfpu=neon",
    "-mfloat-abi=hard",
    "-mthumb",
    "-mthumb-interwork",
    "-mlittle-endian",
    "-funsafe-math-optimizations",
];

// Must match the revisions in `cmake/deps.cmake`.
const ARGON_URL: &str = "https://github.com/stellar-aria/argon";
const ARGON_SHA: &str = "cda2c66c56685190624579aff0c8c9888fba2aa9";
const SIMDE_URL: &str = "https://github.com/simd-everywhere/simde";
const SIMDE_SHA: &str = "71fd833d9666141edcd1d3c109a80e228303d8d7"; // == tag v0.8.2

fn main() {
    let manifest = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    // crates/deluge_sample_convert -> repo root is two levels up.
    let repo = manifest
        .join("../..")
        .canonicalize()
        .expect("resolve repo root");
    let out_dir = PathBuf::from(std::env::var("OUT_DIR").unwrap());

    let src = repo.join("src");
    let src_deluge = src.join("deluge");
    let include = repo.join("include");
    let sim_compat = repo.join("sim/compat");
    let stream = src_deluge.join("storage/audio/stream");

    let convert_cpp = stream.join("convert.cpp");
    let stitch_cpp = stream.join("stitch.cpp");
    let helpers_cpp = src_deluge.join("util/audio_format_helpers.cpp");
    let shim_cpp = manifest.join("cpp/shim.cpp");
    for f in [&convert_cpp, &stitch_cpp, &helpers_cpp, &shim_cpp] {
        println!("cargo:rerun-if-changed={}", f.display());
    }
    println!("cargo:rerun-if-changed=build.rs");

    // argon for every mode; SIMDe below, for the host modes only.
    let third_party = manifest.join("third_party");
    let argon_dir = third_party.join("argon");
    fetch_pinned(
        "argon",
        ARGON_URL,
        ARGON_SHA,
        &argon_dir,
        Path::new("include/argon.hpp"),
    );
    let argon_inc = argon_dir.join("include");

    // Device mode: `target_os = "none"` means a bare-metal (BSP device) build.
    let device = std::env::var("CARGO_CFG_TARGET_OS").as_deref() == Ok("none");
    if device {
        build_device(
            &repo,
            &argon_inc,
            &src,
            &src_deluge,
            &include,
            &out_dir,
            &shim_cpp,
        );
        return;
    }

    // Both host modes need SIMDe: `shim.cpp` includes `convert.h` and so argon, which falls back to
    // SIMDe's portable NEON on a non-ARM host.
    let simde_dir = third_party.join("simde");
    fetch_pinned(
        "simde",
        SIMDE_URL,
        SIMDE_SHA,
        &simde_dir,
        Path::new("simde/arm/neon.h"),
    );
    let simde_root = simde_dir;

    // `app_convert`: the final link already has `convert.cpp`, `stitch.cpp` and the helpers, so
    // compiling them here would double-define `convert_word` and `stitch_boundaries`.
    let app_convert = std::env::var("CARGO_FEATURE_APP_CONVERT").is_ok();
    if app_convert {
        let mut build = cc::Build::new();
        build
            .cpp(true)
            .std("c++26")
            .file(&shim_cpp)
            .include(&src) // definitions_cxx.hpp
            .include(&src_deluge) // storage/..., util/...
            .include(&include) // libdeluge/types.h
            .include(&argon_inc) // argon headers used by shim.cpp's included app headers
            // argon's bare <arm/neon.h> resolves to SIMDe on a non-ARM host, as in the full build.
            .include(&sim_compat) // <arm_neon.h> -> SIMDe shim
            .include(&simde_root) // compat shim's <simde/arm/neon.h>
            .include(simde_root.join("simde")) // argon's bare <arm/neon.h>
            .define("DELUGE_HOST", None)
            .define("SIMDE_NO_NATIVE", None) // portable NEON, bit-accurate to the device
            .flag_if_supported("-Wno-unused-parameter");
        build.compile("deluge_sample_convert_cc");
        return;
    }

    // Host mode: the x86 build, linked into this crate.
    let mut build = cc::Build::new();
    build
        .cpp(true)
        .std("c++26")
        .file(&convert_cpp)
        .file(&stitch_cpp)
        .file(&helpers_cpp)
        .file(&shim_cpp)
        // App include roots, as in tests/unit/audio_stream/CMakeLists.txt.
        .include(&src) // definitions_cxx.hpp (SHARED_INCLUDE)
        .include(&src_deluge) // storage/..., util/...
        .include(&include) // libdeluge/types.h
        // argon + SIMDe (non-native host): argon's bare <arm/neon.h> and the compat <arm_neon.h>
        // shim.
        .include(&argon_inc) // argon.hpp, argon/helpers/size.hpp
        .include(&sim_compat) // <arm_neon.h> -> SIMDe shim
        .include(&simde_root) // compat shim's <simde/arm/neon.h>
        .include(simde_root.join("simde")) // argon's bare <arm/neon.h>
        // Host markers, as in sim/CMakeLists.txt.
        .define("DELUGE_HOST", None)
        .define("SIMDE_NO_NATIVE", None) // portable NEON, bit-accurate to the device
        .flag_if_supported("-Wno-unused-parameter");
    build.compile("deluge_sample_convert_cc");

    // ARM verify: compile each TU with the repo's arm-none-eabi-g++ (`-c` only) and check the
    // objects.
    let arm_result = arm_compile_check(
        &repo,
        &argon_inc,
        &src,
        &src_deluge,
        &include,
        &out_dir,
        &convert_cpp,
        &[&convert_cpp, &stitch_cpp, &helpers_cpp, &shim_cpp],
    );
    let stamp = out_dir.join("arm_compile_result.txt");
    std::fs::write(&stamp, &arm_result).expect("write arm stamp");
    if !arm_result.starts_with("OK") {
        println!("cargo:warning=armv7a-NEON compile/verify FAILED (see test output / stamp)");
    }
}

/// Shallow-fetches `url` at `sha` into `dest`, unless `dest` already holds that revision (checked
/// by `marker_rel` and a `.pinned-sha` stamp).
///
/// # Panics
///
/// Panics if the fetch fails or the marker is missing afterwards.
fn fetch_pinned(name: &str, url: &str, sha: &str, dest: &Path, marker_rel: &Path) {
    let marker = dest.join(marker_rel);
    let sha_stamp = dest.join(".pinned-sha");
    let up_to_date = marker.is_file()
        && std::fs::read_to_string(&sha_stamp)
            .map(|s| s.trim() == sha)
            .unwrap_or(false);
    if up_to_date {
        return;
    }

    // Stale or partial checkout: start clean.
    let _ = std::fs::remove_dir_all(dest);
    std::fs::create_dir_all(dest).unwrap_or_else(|e| panic!("mkdir {}: {e}", dest.display()));

    let git = |args: &[&str]| -> bool {
        Command::new("git")
            .current_dir(dest)
            .args(args)
            .status()
            .map(|s| s.success())
            .unwrap_or(false)
    };
    let ok = git(&["init", "-q"])
        && git(&["remote", "add", "origin", url])
        && git(&["fetch", "-q", "--depth", "1", "origin", sha])
        && git(&[
            "-c",
            "advice.detachedHead=false",
            "checkout",
            "-q",
            "FETCH_HEAD",
        ]);
    if !ok || !marker.is_file() {
        panic!(
            "deluge_sample_convert: failed to fetch pinned {name} ({url} @ {sha}) into {}. \
             This crate owns its deps via a shallow git fetch (needs network on first build). \
             Marker {} missing after checkout.",
            dest.display(),
            marker.display()
        );
    }
    std::fs::write(&sha_stamp, sha).ok();
}

/// Compiles each TU for armv7a-NEON with the device flags (`-c` only, native `<arm_neon.h>`), then
/// checks the objects are ARM ELF and that `convert`'s carries real NEON codegen.
///
/// Returns `"OK\n<details>"` or `"FAIL\n<log>"`.
#[allow(clippy::too_many_arguments)]
fn arm_compile_check(
    repo: &Path,
    argon_inc: &Path,
    src: &Path,
    src_deluge: &Path,
    include: &Path,
    out_dir: &Path,
    convert_cpp: &Path,
    files: &[&PathBuf],
) -> String {
    // `toolchain/current` links to the active toolchain version, so this survives version bumps.
    let bin = repo.join("toolchain/current/arm-none-eabi-gcc/bin");
    let gxx = bin.join("arm-none-eabi-g++");
    let objdump = bin.join("arm-none-eabi-objdump");
    if !gxx.is_file() {
        return format!("FAIL\narm-none-eabi-g++ not found at {}", gxx.display());
    }

    let mut log = String::new();
    let mut convert_obj: Option<PathBuf> = None;
    for f in files {
        let obj = out_dir.join(format!(
            "{}.arm.o",
            f.file_name().unwrap().to_string_lossy()
        ));
        let out = Command::new(&gxx)
            .args(["-std=c++26", "-c"])
            .args(ARM_ARCH_FLAGS)
            // Native <arm_neon.h> — no SIMDe, no compat shim on the real target.
            .arg(format!("-I{}", argon_inc.display()))
            .arg(format!("-I{}", src.display()))
            .arg(format!("-I{}", src_deluge.display()))
            .arg(format!("-I{}", include.display()))
            .arg(f.as_path())
            .arg("-o")
            .arg(&obj)
            .output();
        match out {
            Ok(o) if o.status.success() => {
                if *f == convert_cpp {
                    convert_obj = Some(obj);
                }
            }
            Ok(o) => log.push_str(&format!(
                "--- {} ---\n{}\n",
                f.display(),
                String::from_utf8_lossy(&o.stderr)
            )),
            Err(e) => log.push_str(&format!("--- {} --- spawn error: {e}\n", f.display())),
        }
    }
    if !log.is_empty() {
        return format!("FAIL\n{log}");
    }

    // `vcvt.s32.f32` is convert's float-to-Q31 path.
    let convert_obj = convert_obj.expect("convert.cpp arm object produced");
    if !objdump.is_file() {
        return "OK\narm objects produced (objdump absent, NEON not disassembly-verified)\n"
            .to_string();
    }
    let arch = Command::new(&objdump)
        .args(["-f"])
        .arg(&convert_obj)
        .output();
    let arch_str = arch
        .map(|o| String::from_utf8_lossy(&o.stdout).into_owned())
        .unwrap_or_default();
    let is_arm_elf =
        arch_str.contains("elf32-littlearm") || arch_str.to_lowercase().contains("arm");
    if !is_arm_elf {
        return format!("FAIL\nconvert.cpp object is not ARM ELF:\n{arch_str}");
    }
    let dis = Command::new(&objdump)
        .args(["-d"])
        .arg(&convert_obj)
        .output();
    let dis_str = dis
        .map(|o| String::from_utf8_lossy(&o.stdout).into_owned())
        .unwrap_or_default();
    let has_neon = dis_str.contains("vcvt.s32.f32");
    if !has_neon {
        return "FAIL\nARM ELF produced but no vcvt.s32.f32 NEON codegen found in convert.cpp object"
            .to_string();
    }
    format!(
        "OK\nARM ELF + real NEON (vcvt.s32.f32) confirmed in convert.cpp object\n{}",
        arch_str
            .lines()
            .find(|l| l.contains("file format"))
            .unwrap_or("")
            .trim()
    )
}

/// Builds the shim for the device and emits the link directives for the BSP's final link.
///
/// Compiles only `cpp/shim.cpp` (device NEON flags, no SIMDe) and archives it. `convert.cpp`,
/// `stitch.cpp` and `audio_format_helpers.cpp` are already in the app's objects
/// (`libdeluge_app_objs.a`), which the BSP links; compiling them again would duplicate
/// `convert_word`, `stitch_boundaries` and the helpers. The shim leaves those undefined for the app
/// to resolve. The inline templates in `convert.h` are unaffected: the shim's no-op-yield
/// instantiation mangles differently from the app's.
#[allow(clippy::too_many_arguments)]
fn build_device(
    repo: &Path,
    argon_inc: &Path,
    src: &Path,
    src_deluge: &Path,
    include: &Path,
    out_dir: &Path,
    shim_cpp: &Path,
) {
    let bin = repo.join("toolchain/current/arm-none-eabi-gcc/bin");
    // `DELUGE_DEVICE_CXX` must name the compiler that builds the app: GCC mangles `int32_t` as
    // `long` on arm-none-eabi and clang as `int`, so a mismatched shim's references do not resolve.
    // `DELUGE_DEVICE_CXXFLAGS` replaces `ARM_ARCH_FLAGS` rather than extending it, because clang
    // rejects `-mthumb-interwork`.
    println!("cargo:rerun-if-env-changed=DELUGE_DEVICE_CXX");
    println!("cargo:rerun-if-env-changed=DELUGE_DEVICE_CXXFLAGS");
    let gxx = std::env::var("DELUGE_DEVICE_CXX")
        .map(PathBuf::from)
        .unwrap_or_else(|_| bin.join("arm-none-eabi-g++"));
    let arch_flags: Vec<String> = std::env::var("DELUGE_DEVICE_CXXFLAGS")
        .map(|s| s.split_whitespace().map(String::from).collect())
        .unwrap_or_else(|_| ARM_ARCH_FLAGS.iter().map(|s| s.to_string()).collect());
    let ar = bin.join("arm-none-eabi-ar");
    assert!(
        gxx.is_file(),
        "device C++ compiler not found at {}",
        gxx.display()
    );
    assert!(
        ar.is_file(),
        "arm-none-eabi-ar not found at {}",
        ar.display()
    );

    let obj = out_dir.join("shim.cpp.o");
    let status = Command::new(&gxx)
        .args(["-std=c++26", "-c"])
        .args(&arch_flags)
        // Native <arm_neon.h>, as in `arm_compile_check`.
        .arg(format!("-I{}", argon_inc.display()))
        .arg(format!("-I{}", src.display()))
        .arg(format!("-I{}", src_deluge.display()))
        .arg(format!("-I{}", include.display()))
        .arg(shim_cpp)
        .arg("-o")
        .arg(&obj)
        .status()
        .expect("run arm-none-eabi-g++ (device shim.cpp compile)");
    assert!(
        status.success(),
        "device build of cpp/shim.cpp failed (see g++ output above)"
    );

    let archive = out_dir.join("libdeluge_sample_convert_cc.a");
    let _ = std::fs::remove_file(&archive);
    let status = Command::new(&ar)
        .arg("crs")
        .arg(&archive)
        .arg(&obj)
        .status()
        .expect("run arm-none-eabi-ar");
    assert!(status.success(), "archiving device shim.cpp object failed");

    println!("cargo:rustc-link-search=native={}", out_dir.display());
    println!("cargo:rustc-link-lib=static=deluge_sample_convert_cc");
}
