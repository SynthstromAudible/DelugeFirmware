//! `deluge-upgrade <file or dir>... --out <dir> [--card <root>] [--report <file>]`
//!
//! Writes each Deluge XML song or preset (a directory is walked for `*.XML`) to `--out` as the firmware's JSON, at
//! its path relative to the input it was found under, with `.json` for its extension. `--card` is the card root the
//! files' samples are found under; `--report` keeps what could not be converted as JSON.
#![forbid(unsafe_code)]

use deluge_upgrade::report::Report;
use deluge_upgrade::tree::Node;
use std::path::{Path, PathBuf};
use std::process::ExitCode;

struct Args {
    inputs: Vec<PathBuf>,
    out: PathBuf,
    card: Option<PathBuf>,
    report: Option<PathBuf>,
}

fn parse_args() -> Result<Args, String> {
    let mut inputs = Vec::new();
    let (mut out, mut card, mut report) = (None, None, None);
    let mut args = std::env::args_os().skip(1);
    while let Some(arg) = args.next() {
        let mut value = |flag: &str| {
            args.next()
                .map(PathBuf::from)
                .ok_or(format!("{flag} needs a path"))
        };
        match arg.to_str() {
            Some("--out") => out = Some(value("--out")?),
            Some("--card") => card = Some(value("--card")?),
            Some("--report") => report = Some(value("--report")?),
            Some(flag) if flag.starts_with("--") => return Err(format!("unknown option {flag}")),
            _ => inputs.push(PathBuf::from(arg)),
        }
    }
    let out = out.ok_or("--out is required")?;
    if inputs.is_empty() {
        return Err("no input files".into());
    }
    Ok(Args {
        inputs,
        out,
        card,
        report,
    })
}

/// Every XML document under `input` (or `input` itself), with its path relative to where it was found.
fn documents(input: &Path) -> std::io::Result<Vec<(PathBuf, PathBuf)>> {
    if input.is_file() {
        let name = input.file_name().map(PathBuf::from).unwrap_or_default();
        return Ok(vec![(input.to_path_buf(), name)]);
    }
    let mut found = Vec::new();
    let mut pending = vec![input.to_path_buf()];
    while let Some(dir) = pending.pop() {
        for entry in std::fs::read_dir(&dir)? {
            let path = entry?.path();
            if path.is_dir() {
                pending.push(path);
            } else if path
                .extension()
                .is_some_and(|e| e.eq_ignore_ascii_case("xml"))
            {
                let relative = path.strip_prefix(input).unwrap_or(&path).to_path_buf();
                found.push((path, relative));
            }
        }
    }
    found.sort();
    Ok(found)
}

fn upgrade_file(
    source: &Path,
    target: &Path,
    _card: Option<&Path>,
    _report: &mut Report,
) -> Result<(), String> {
    let text = std::fs::read_to_string(source).map_err(|e| e.to_string())?;
    let doc = Node::parse_xml(&text).map_err(|e| e.to_string())?;
    if let Some(parent) = target.parent() {
        std::fs::create_dir_all(parent).map_err(|e| e.to_string())?;
    }
    std::fs::write(target, doc.to_json()).map_err(|e| e.to_string())
}

fn main() -> ExitCode {
    let args = match parse_args() {
        Ok(args) => args,
        Err(why) => {
            eprintln!("deluge-upgrade: {why}");
            eprintln!(
                "usage: deluge-upgrade <file or dir>... --out <dir> [--card <root>] [--report <file>]"
            );
            return ExitCode::from(2);
        }
    };
    let mut report = Report::default();
    let mut failed = 0;
    for input in &args.inputs {
        let found = match documents(input) {
            Ok(found) => found,
            Err(e) => {
                eprintln!("deluge-upgrade: {}: {e}", input.display());
                failed += 1;
                continue;
            }
        };
        for (source, relative) in found {
            let target = args.out.join(relative).with_extension("json");
            if let Err(why) = upgrade_file(&source, &target, args.card.as_deref(), &mut report) {
                eprintln!("deluge-upgrade: {}: {why}", source.display());
                failed += 1;
            }
        }
    }
    for finding in &report.findings {
        eprintln!(
            "REPORTED {}: {}: {}",
            finding.file, finding.conversion, finding.detail
        );
    }
    if let Some(path) = &args.report
        && let Err(e) = std::fs::write(path, report.to_json())
    {
        eprintln!("deluge-upgrade: {}: {e}", path.display());
        failed += 1;
    }
    if failed > 0 {
        ExitCode::FAILURE
    } else {
        ExitCode::SUCCESS
    }
}
