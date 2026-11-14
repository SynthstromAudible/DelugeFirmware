//! `card-manifest <manifest> <out-dir>`: builds a card manifest (`storage/card_manifest.rs`) into a new directory,
//! for the tools that take a card as a directory of files (`tools/emu_parity.py` images one with delugemu's
//! `mksd.sh`). The manifest's `base` and `from=` paths are read from this repository. Exits 2 on a usage error or an
//! existing `<out-dir>`, 1 when the manifest does not parse or build.

use std::path::{Path, PathBuf};
use std::process::ExitCode;

use deluge_bsp_rust::storage::card_manifest;

fn main() -> ExitCode {
    let args: Vec<PathBuf> = std::env::args_os().skip(1).map(PathBuf::from).collect();
    let [manifest, out] = args.as_slice() else {
        eprintln!("usage: card-manifest <manifest> <out-dir>");
        return ExitCode::from(2);
    };
    if out.exists() {
        eprintln!("card-manifest: {} already exists", out.display());
        return ExitCode::from(2);
    }
    let repo = Path::new(env!("CARGO_MANIFEST_DIR"))
        .ancestors()
        .nth(3)
        .expect("src/bsp/rust is 3 below the repo root");
    match card_manifest::build_file(repo, manifest, out) {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("card-manifest: {e}");
            ExitCode::FAILURE
        }
    }
}
