//! Firmware version stamps, compared as the firmware compares them (`src/serialization/firmware_version.h`,
//! `semver.cpp`): official before community before unknown, then major, minor, patch, then a pre-release below its
//! release, pre-release tags compared as plain strings.

use crate::tree::Node;
use std::cmp::Ordering;

/// Which firmware line wrote a file, in the order the firmware ranks them.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum Kind {
    Official,
    Community,
    /// A stamp that does not parse: newest of all, so no old-file rule applies to it.
    Unknown,
}

/// A firmware version, as a file's `firmwareVersion` stamp names it.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct FirmwareVersion {
    pub kind: Kind,
    pub major: u8,
    pub minor: u8,
    pub patch: u8,
    pub pre_release: String,
}

impl FirmwareVersion {
    /// What a file with no stamp reads as: the oldest official firmware.
    pub const OLDEST_OFFICIAL: FirmwareVersion = FirmwareVersion::official(0, 0, 0);
    /// What an unparseable stamp reads as.
    pub const UNKNOWN: FirmwareVersion = FirmwareVersion {
        kind: Kind::Unknown,
        major: 0,
        minor: 0,
        patch: 0,
        pre_release: String::new(),
    };

    /// An official release; add a pre-release tag with `with_tag`.
    pub const fn official(major: u8, minor: u8, patch: u8) -> Self {
        FirmwareVersion {
            kind: Kind::Official,
            major,
            minor,
            patch,
            pre_release: String::new(),
        }
    }

    /// A community release; add a pre-release tag with `with_tag`.
    pub const fn community(major: u8, minor: u8, patch: u8) -> Self {
        FirmwareVersion {
            kind: Kind::Community,
            major,
            minor,
            patch,
            pre_release: String::new(),
        }
    }

    /// The same version with pre-release tag `tag`.
    pub fn with_tag(mut self, tag: &str) -> Self {
        self.pre_release = tag.to_string();
        self
    }

    /// Parses a stamp such as `4.1.4-alpha` or `c1.2.1`, as `FirmwareVersion::parse` does.
    pub fn parse(stamp: &str) -> Self {
        let (kind, rest) = match stamp.strip_prefix('c') {
            Some(rest) => (Kind::Community, rest),
            None => (Kind::Official, stamp),
        };
        match parse_semver(rest) {
            Some((major, minor, patch, pre_release)) => FirmwareVersion {
                kind,
                major,
                minor,
                patch,
                pre_release: pre_release.to_string(),
            },
            None => Self::UNKNOWN,
        }
    }

    /// The version a document reads as: a `firmwareVersion` entry at its top, beside its root element, or on its
    /// root element; the oldest official when it has none.
    pub fn of_document(doc: &Node) -> Self {
        if let Some(stamp) = doc.get("firmwareVersion").and_then(Node::text) {
            return Self::parse(stamp);
        }
        for (_, root) in doc.entries() {
            if let Some(stamp) = root.get("firmwareVersion").and_then(Node::text) {
                return Self::parse(stamp);
            }
        }
        Self::OLDEST_OFFICIAL
    }

    /// The stamp as the firmware writes it.
    pub fn stamp(&self) -> String {
        let prefix = if self.kind == Kind::Community {
            "c"
        } else {
            ""
        };
        let tag = if self.pre_release.is_empty() {
            String::new()
        } else {
            format!("-{}", self.pre_release)
        };
        format!("{prefix}{}.{}.{}{tag}", self.major, self.minor, self.patch)
    }
}

/// `major.minor.patch[-pre]`, each number 0..=255; anything after the patch number that is not `-` is ignored, as
/// the firmware's parser ignores it. A pre-release runs to a `+` or the end.
fn parse_semver(s: &str) -> Option<(u8, u8, u8, &str)> {
    let (major, rest) = number(s)?;
    let rest = rest.strip_prefix('.')?;
    let (minor, rest) = number(rest)?;
    let rest = rest.strip_prefix('.')?;
    let (patch, rest) = number(rest)?;
    let pre_release = match rest.strip_prefix('-') {
        Some(tag) => tag.split('+').next().unwrap_or(""),
        None => "",
    };
    Some((major, minor, patch, pre_release))
}

/// A leading decimal number that fits a byte, and what follows it.
fn number(s: &str) -> Option<(u8, &str)> {
    let digits = s.bytes().take_while(u8::is_ascii_digit).count();
    let n: u8 = s[..digits].parse().ok()?;
    Some((n, &s[digits..]))
}

impl Ord for FirmwareVersion {
    fn cmp(&self, other: &Self) -> Ordering {
        self.kind
            .cmp(&other.kind)
            .then(self.major.cmp(&other.major))
            .then(self.minor.cmp(&other.minor))
            .then(self.patch.cmp(&other.patch))
            .then_with(
                || match (self.pre_release.is_empty(), other.pre_release.is_empty()) {
                    (true, false) => Ordering::Greater,
                    (false, true) => Ordering::Less,
                    _ => self.pre_release.cmp(&other.pre_release),
                },
            )
    }
}

impl PartialOrd for FirmwareVersion {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}
