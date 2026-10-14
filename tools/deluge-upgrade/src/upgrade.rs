//! Upgrading one document: each conversion its version stamp calls for, then the firmware's current stamp.

use crate::passes::{self, Pass};
use crate::report::Report;
use crate::tree::Node;
use crate::version::FirmwareVersion;
use std::path::{Path, PathBuf};

/// What a conversion may consult: the file's own stamp, the card its samples are on, and where to report what it
/// could not convert.
pub struct Context<'a> {
    /// The version the file was read as.
    pub version: FirmwareVersion,
    /// The card root the file's sample paths are relative to, when the run was given one.
    pub card: Option<PathBuf>,
    file: String,
    report: &'a mut Report,
}

impl<'a> Context<'a> {
    /// A context for `file`, whose version is set when the document is upgraded.
    pub fn new(file: &Path, card: Option<&Path>, report: &'a mut Report) -> Self {
        Context {
            version: FirmwareVersion::OLDEST_OFFICIAL,
            card: card.map(Path::to_path_buf),
            file: file.display().to_string(),
            report,
        }
    }

    /// Notes that `conversion` could not be made in this file.
    pub fn note(&mut self, conversion: &'static str, detail: String) {
        self.report.note(&self.file, conversion, detail);
    }
}

/// Upgrades `doc` with every conversion.
pub fn upgrade(doc: &mut Node, ctx: &mut Context) {
    upgrade_with(doc, ctx, passes::ALL);
}

/// Upgrades a file's bytes to the firmware's JSON. The firmware reads a file's bytes as they are, so one that is not
/// UTF-8 is read a byte to a character and written back a character to a byte: its names keep their bytes. It is
/// reported, as a JSON tool other than the firmware may not read it.
pub fn upgrade_bytes(bytes: &[u8], ctx: &mut Context) -> Result<Vec<u8>, crate::UpgradeError> {
    let (text, utf8) = match std::str::from_utf8(bytes) {
        Ok(text) => (text.to_string(), true),
        Err(_) => (bytes.iter().map(|&b| char::from(b)).collect(), false),
    };
    let mut doc = Node::parse_xml(&text)?;
    upgrade(&mut doc, ctx);
    let json = doc.to_json();
    if utf8 {
        return Ok(json.into_bytes());
    }
    ctx.note(
        "text encoding",
        "the file is not UTF-8: its bytes are kept as they are, so the JSON is not UTF-8 either"
            .into(),
    );
    Ok(json
        .chars()
        .map(|c| u8::try_from(u32::from(c)).unwrap_or(b'?'))
        .collect())
}

/// Upgrades `doc` with `passes`: each one whose version is above the file's stamp, in order; then the file's stamps
/// are replaced by the firmware's current one, as the document's first entry, where every reader looks first.
pub fn upgrade_with(doc: &mut Node, ctx: &mut Context, passes: &[Pass]) {
    ctx.version = FirmwareVersion::of_document(doc);
    for pass in passes {
        if pass
            .before
            .as_ref()
            .is_none_or(|before| ctx.version < *before)
        {
            (pass.run)(doc, ctx);
        }
    }
    if let Some(entries) = doc.entries_mut() {
        entries.retain(|(key, _)| key != "firmwareVersion");
        for (_, root) in entries.iter_mut() {
            root.remove("firmwareVersion");
        }
        entries.insert(
            0,
            (
                "firmwareVersion".to_string(),
                Node::Text(FirmwareVersion::current().stamp()),
            ),
        );
    }
}
