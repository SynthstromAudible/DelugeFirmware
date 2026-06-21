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
        // CRITICAL: the C++ app is built arm-none-eabi, whose EABI default makes
        // enums the smallest type that fits (`-fshort-enums`) — e.g.
        // DelugeInputEventKind (0..3) is 1 byte, so DelugeInputEvent is
        // {kind@0, x@1, y@2, value@4}. bindgen runs under the *host* clang, which
        // sizes enums as 4-byte `int` by default; without this flag every
        // enum-bearing POD (DelugeInputEvent, DelugeBoard, MIDI/card events, …)
        // is laid out differently on the two sides and fields read as garbage
        // across the ABI. Point libclang at the actual armv7a EABI target so it
        // computes the same layout as the app; -fshort-enums alone is ignored
        // when libclang targets the host (x86_64 mandates 4-byte int enums).
        .clang_arg("--target=armv7a-none-eabihf")
        .clang_arg("-fshort-enums")
        // Layouts now match the armv7a app; the asserts would run host-side anyway.
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
    // Linker sources live under linker/ (NOT the crate root): the cross linker
    // resolves `INCLUDE memory.x` from its CWD (the crate root) before the -L
    // search path, so a memory.x in the root would shadow this generated one.
    let linker_src = manifest_dir.join("linker");
    fs::copy(linker_src.join(memory_src), out_dir.join("memory.x")).unwrap();
    // Supplementary fragment that places the app's SDRAM sections + symbols.
    fs::copy(linker_src.join("sdram_sections.x"), out_dir.join("sdram_sections.x")).unwrap();
    println!("cargo:rustc-link-search={}", out_dir.display());
    println!("cargo:rustc-link-arg=-Wl,-T,{linker_script}");
    println!("cargo:rustc-link-arg=-Wl,-T,sdram_sections.x");
    println!("cargo:rerun-if-changed=linker/memory.x");
    println!("cargo:rerun-if-changed=linker/memory_rtt.x");
    println!("cargo:rerun-if-changed=linker/sdram_sections.x");

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
    // Re-run (re-archive) when any object's CONTENT changes — a dir rerun-if-changed
    // only fires on add/remove, so editing a .cpp + rebuilding deluge_app wouldn't
    // otherwise re-archive, leaving a stale link.
    for o in &objs {
        println!("cargo:rerun-if-changed={}", o.display());
    }
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

    // __exidx_start/__exidx_end are now defined by rza1l.x's .ARM.exidx section
    // (kept, not discarded) so C++ exception unwinding works.

    println!("cargo:rerun-if-changed={}", app_objs_dir.display());

    // Self-clearing nag for the temporary rza1l.x stack/heap-overlap workaround
    // (program_stack_start retargeted to __sram_heap_end so the C++ app's
    // GeneralMemoryAllocator can't overrun the mode stacks). Warns on every build
    // while the HACK tag is present in the sibling deluge-sdk linker scripts;
    // goes silent automatically once the proper fix (app sources heap bounds via
    // libdeluge/memory.h) removes it.
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
