use std::env;
use std::fs;
use std::path::PathBuf;
use std::process::Command;

fn main() {
    let out_dir = PathBuf::from(env::var("OUT_DIR").unwrap());
    let manifest_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    // DelugeFirmware repo root (crate is at <root>/src/bsp/rust).
    let repo_root = manifest_dir.join("../../..").canonicalize().unwrap();

    // ---------------------------------------------------------------------
    // bindgen: generate the libdeluge POD types from the canonical headers
    // (include/libdeluge/*.h). Types only — we DEFINE the service functions
    // ourselves in src/ffi.rs (#[no_mangle]); the C contract drives the types
    // so a layout/type change is a compile error.
    // ---------------------------------------------------------------------
    let include_dir = repo_root.join("include");
    let wrapper = manifest_dir.join("wrapper.h");
    let bindings = bindgen::Builder::default()
        .header(wrapper.to_str().unwrap())
        .clang_arg(format!("-I{}", include_dir.display()))
        // Types only (incl. the fn-pointer aliases). The service functions are
        // DEFINED in src/ffi.rs; emitting bindgen's `extern "C"` decls too would
        // trip edition-2024's "extern blocks must be unsafe" (bindgen 0.70).
        .allowlist_type("Deluge.*")
        .allowlist_type("RunCondition")
        .use_core()
        // Host vs armv7a layout is identical for these PODs; skip the asserts.
        .layout_tests(false)
        .generate()
        .expect("bindgen failed on libdeluge headers");
    bindings
        .write_to_file(out_dir.join("libdeluge_sys.rs"))
        .expect("write libdeluge_sys.rs");
    println!("cargo:rerun-if-changed={}", wrapper.display());
    println!("cargo:rerun-if-changed={}", include_dir.display());

    // ---------------------------------------------------------------------
    // Linker script + memory layout (rza1l-hal's build.rs puts rza1l.x on the
    // link search path; we supply the matching memory.x). Mirrors deluge-sdk's
    // firmwares/controller-firmware.
    // ---------------------------------------------------------------------
    let rtt = env::var("CARGO_FEATURE_RTT").is_ok();
    let (memory_src, linker_script) = if rtt {
        ("memory_rtt.x", "rza1l_rtt.x")
    } else {
        ("memory.x", "rza1l.x")
    };
    fs::copy(manifest_dir.join(memory_src), out_dir.join("memory.x")).unwrap();
    // Supplementary fragment that places the app's SDRAM sections + symbols.
    fs::copy(manifest_dir.join("sdram_sections.x"), out_dir.join("sdram_sections.x")).unwrap();
    println!("cargo:rustc-link-search={}", out_dir.display());
    println!("cargo:rustc-link-arg=-Wl,-T,{linker_script}");
    println!("cargo:rustc-link-arg=-Wl,-T,sdram_sections.x");
    println!("cargo:rerun-if-changed=memory.x");
    println!("cargo:rerun-if-changed=sdram_sections.x");

    // ---------------------------------------------------------------------
    // Link the portable C++ application (built by CMake into the `build/` dir).
    // deluge_app is an OBJECT lib (no .a), so archive its objects here, then
    // link that + the static-lib closure as one --start-group (mutual refs:
    // C++ calls our deluge_* services, we call its deluge_main()).
    //
    // BRING-UP: assumes the Release config is already built in <root>/build
    // (`cmake --build build --target deluge_app fatfs NE10 …`). Parametrize the
    // build dir / config later; this is the two-step flow from the plan.
    // ---------------------------------------------------------------------
    let build_dir =
        env::var("DELUGE_BUILD_DIR").map(PathBuf::from).unwrap_or_else(|_| repo_root.join("build"));
    // Bring-up uses Debug: Release compiles the app with -flto=auto (GCC slim-LTO
    // objects whose symbols rust's lld can't read). Debug objects are plain ELF
    // (and carry debug_info). Switch to Release later via bfd ld if LTO is wanted.
    let cfg = env::var("DELUGE_BUILD_CONFIG").unwrap_or_else(|_| "Debug".into());
    let ar = repo_root
        .join("toolchain/v22/linux-x86_64/arm-none-eabi-gcc/bin/arm-none-eabi-ar");

    let app_objs_dir = build_dir.join(format!("src/deluge/CMakeFiles/deluge_app.dir/{cfg}"));
    if !app_objs_dir.is_dir() {
        panic!(
            "C++ app objects not found at {}. Build them first:\n  \
             cmake --build {} --target deluge_app fatfs NE10 eyalroz_printf \
             deluge_dsp deluge_scheduler deluge_foundation deluge_midi",
            app_objs_dir.display(),
            build_dir.display()
        );
    }

    // Collect all deluge_app .obj files and archive them into libdeluge_app_objs.a.
    let mut objs = Vec::new();
    collect_objs(&app_objs_dir, &mut objs);
    objs.sort();
    let app_objs_archive = out_dir.join("libdeluge_app_objs.a");
    let _ = fs::remove_file(&app_objs_archive);
    let status = Command::new(&ar)
        .arg("crs")
        .arg(&app_objs_archive)
        .args(&objs)
        .status()
        .expect("run arm-none-eabi-ar");
    assert!(status.success(), "archiving deluge_app objects failed");

    // The portable static-lib closure (argon/etl are header-only).
    let deps: [(&str, &str); 7] = [
        ("src/fatfs", "libfatfs.a"),
        ("src/NE10", "libNE10.a"),
        ("src/lib", "libeyalroz_printf.a"),
        ("src/deluge/dsp", "libdeluge_dsp.a"),
        ("src/OSLikeStuff", "libdeluge_scheduler.a"),
        ("src/foundation", "libdeluge_foundation.a"),
        ("src/midi", "libdeluge_midi.a"),
    ];

    // One link group so the mutual C++/Rust refs resolve.
    println!("cargo:rustc-link-arg=-Wl,--start-group");
    println!("cargo:rustc-link-arg={}", app_objs_archive.display());
    for (dir, lib) in deps {
        let p = build_dir.join(dir).join(&cfg).join(lib);
        assert!(p.is_file(), "missing dep archive {}", p.display());
        println!("cargo:rustc-link-arg={}", p.display());
    }
    println!("cargo:rustc-link-arg=-Wl,--end-group");

    // C++/C runtime: rustc passes -nodefaultlibs, so re-add the g++ runtime the
    // app needs (libstdc++/libsupc++ for std::, __cxa_*, vtables; libgcc for
    // helpers like __popcountsi2; newlib libc/libm; libnosys for unhosted
    // syscall stubs). g++'s own search paths resolve these. Grouped for the
    // libstdc++<->libc<->libgcc circular refs.
    println!("cargo:rustc-link-arg=-Wl,--start-group");
    for l in ["-lstdc++", "-lsupc++", "-lc", "-lm", "-lgcc", "-lnosys"] {
        println!("cargo:rustc-link-arg={l}");
    }
    println!("cargo:rustc-link-arg=-Wl,--end-group");

    // ARM EH index bounds: the app references these but rza1l.x emits no
    // .ARM.exidx span symbols. M0: define empty (no unwind tables). The SDRAM
    // section-boundary symbols (__heap_start, __frunk_*, __sdram_bss_end,
    // program_stack_end) are PROVIDEd by sdram_sections.x instead.
    for sym in ["__exidx_start", "__exidx_end"] {
        println!("cargo:rustc-link-arg=-Wl,--defsym,{sym}=0");
    }

    println!("cargo:rerun-if-changed={}", app_objs_dir.display());
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
