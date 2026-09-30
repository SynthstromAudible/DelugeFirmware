//! Link helpers shared by the device and host links, and by `harness/golden_vt_render`'s build
//! script, which includes this file and [`host_app`](super::host_app) through `#[path]`.

use std::fs;
use std::path::{Path, PathBuf};

/// The `deluge_sample_stream_*` C ABI, kept whole in the link whether or not the C++ app
/// references each symbol (`deluge_sample_stream_get_asset_id` has no C++ caller).
pub const SAMPLE_STREAM_ROOTS: [&str; 6] = [
    "deluge_sample_stream_open",
    "deluge_sample_stream_close",
    "deluge_sample_stream_set_geometry",
    "deluge_sample_stream_get_asset_id",
    "deluge_sample_stream_set_asset_id",
    "deluge_sample_stream_read_at",
];

/// The efatfs mount C ABI. Each has a C++ caller; the roots keep the link intact if a refactor
/// drops the last one.
pub const EFATFS_ROOTS: [&str; 3] = [
    "deluge_efatfs_mount",
    "deluge_efatfs_remount",
    "deluge_efatfs_cluster_size",
];

/// Emits `-Wl,-u,SYM` for each symbol, forcing it as a link root. Each symbol is listed on its own,
/// not one per object, because `--gc-sections` prunes unreached function sections individually.
pub fn force_link_roots(syms: &[&str]) {
    for sym in syms {
        println!("cargo:rustc-link-arg=-Wl,-u,{sym}");
    }
}

/// Collects every object under `dir` whose source still exists into `out`, and every other one
/// into `skipped`.
///
/// Ninja never deletes the object of a source that was moved or deleted, and archiving one links a
/// second definition of everything its source defined — or references to symbols that no longer
/// exist. `base` is the target's object directory, whose layout mirrors `src_root`'s.
///
/// A source outside its target's own directory gets a `__/` per parent step in its object path,
/// which never resolves here, so it reads as an orphan. No target compiles such a source; the host
/// link's surviving-object floor catches it if one ever does.
pub fn collect_objs(
    base: &Path,
    dir: &Path,
    src_root: &Path,
    out: &mut Vec<PathBuf>,
    skipped: &mut Vec<PathBuf>,
) {
    for entry in fs::read_dir(dir).unwrap() {
        let p = entry.unwrap().path();
        if p.is_dir() {
            collect_objs(base, &p, src_root, out, skipped);
        } else if p.extension().is_some_and(|e| e == "obj" || e == "o") {
            let source = src_root.join(p.strip_prefix(base).unwrap().with_extension(""));
            if source.is_file() {
                out.push(p);
            } else {
                println!(
                    "cargo:warning=skipping {}: its source {} no longer exists",
                    p.display(),
                    source.display()
                );
                skipped.push(p);
            }
        }
    }
}

/// A hash of an object closure's paths and bytes, to force a relink when the objects change.
///
/// Cargo relinks only when a build script's output changes. When objects are rebuilt in place (say,
/// a tree reconfigured with `-fsanitize=thread`) the archive path in the link args stays the same,
/// so without this Cargo can keep linking an archive of stale objects. Not cryptographic: it only
/// has to tell one build's closure from the next.
pub fn hash_objs_content(objs: &[PathBuf]) -> u64 {
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
