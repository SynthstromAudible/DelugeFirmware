//! A Deluge document as the firmware's parser builds it (`src/serialization/parse_xml.cpp`), and the firmware's JSON
//! for it.
//!
//! An element is a map of its attributes and child elements in document order, duplicate keys kept; an element with
//! neither is its text, or an empty map when it has none. Attributes and child tags are the same entries: the
//! firmware reads both alike, so the upgrader neither keeps nor needs the difference.

use crate::UpgradeError;

/// One node of a document.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Node {
    /// An attribute's value or an element's text.
    Text(String),
    /// An element's entries, in document order; a key may repeat.
    Map(Vec<(String, Node)>),
}

/// The containers the firmware writes as JSON arrays: those its writers open with `writeArrayStart`, and those its
/// codecs build as `Value` arrays. Each item is a one-entry map naming the item's element.
pub const ARRAY_CONTAINERS: &[&str] = &[
    "arrangementOnlyTracks",
    "chord",
    "chordMem",
    "chordSlot",
    "depthControlledBy",
    "favourites",
    "instruments",
    "list",
    "midiKnobs",
    "modKnobs",
    "modeNotes",
    "noteRows",
    "patchCables",
    "sampleRanges",
    "sections",
    "sessionClips",
    "sessionMacros",
    "soundSources",
    "wavetableRanges",
];

impl Node {
    /// Parses a document: its top-level elements, each an entry of the returned map.
    pub fn parse_xml(text: &str) -> Result<Node, UpgradeError> {
        Parser {
            text: text.as_bytes(),
            src: text,
            pos: 0,
        }
        .document()
    }

    /// The entries of a map; none for text.
    pub fn entries(&self) -> &[(String, Node)] {
        match self {
            Node::Map(entries) => entries,
            Node::Text(_) => &[],
        }
    }

    /// The entries of a map, to change; none for text.
    pub fn entries_mut(&mut self) -> Option<&mut Vec<(String, Node)>> {
        match self {
            Node::Map(entries) => Some(entries),
            Node::Text(_) => None,
        }
    }

    /// The text of a text node.
    pub fn text(&self) -> Option<&str> {
        match self {
            Node::Text(text) => Some(text),
            Node::Map(_) => None,
        }
    }

    /// The first entry named `key`.
    pub fn get(&self, key: &str) -> Option<&Node> {
        self.entries()
            .iter()
            .find(|(k, _)| k == key)
            .map(|(_, v)| v)
    }

    /// The first entry named `key`, to change.
    pub fn get_mut(&mut self, key: &str) -> Option<&mut Node> {
        self.entries_mut()?
            .iter_mut()
            .find(|(k, _)| k == key)
            .map(|(_, v)| v)
    }

    /// Replaces the first entry named `key`, or appends one.
    pub fn set(&mut self, key: &str, value: Node) {
        if let Some(entries) = self.entries_mut() {
            match entries.iter_mut().find(|(k, _)| k == key) {
                Some((_, v)) => *v = value,
                None => entries.push((key.to_string(), value)),
            }
        }
    }

    /// Removes and returns the first entry named `key`.
    pub fn remove(&mut self, key: &str) -> Option<Node> {
        let entries = self.entries_mut()?;
        let at = entries.iter().position(|(k, _)| k == key)?;
        Some(entries.remove(at).1)
    }

    /// A text node's integer, read as the firmware reads one: decimal, or hex after `0x`.
    pub fn int(&self) -> Option<i64> {
        let text = self.text()?.trim();
        match text.strip_prefix("0x").or_else(|| text.strip_prefix("0X")) {
            Some(hex) => u32::from_str_radix(hex, 16)
                .ok()
                .map(|n| i64::from(n as i32)),
            None => text.parse().ok(),
        }
    }

    /// The firmware's JSON for this document: a map is an object, an array container an array of one-entry objects,
    /// a whole decimal number bare, any other text a string.
    pub fn to_json(&self) -> String {
        let mut out = String::new();
        write_object(self.entries(), 0, &mut out);
        out.push('\n');
        out
    }
}

fn write_object(entries: &[(String, Node)], depth: usize, out: &mut String) {
    out.push('{');
    for (i, (key, value)) in entries.iter().enumerate() {
        if i > 0 {
            out.push(',');
        }
        newline(depth + 1, out);
        write_string(key, out);
        out.push_str(": ");
        write_value(key, value, depth + 1, out);
    }
    if !entries.is_empty() {
        newline(depth, out);
    }
    out.push('}');
}

fn write_value(key: &str, value: &Node, depth: usize, out: &mut String) {
    match value {
        Node::Text(text) if is_bare_number(text) => out.push_str(text),
        Node::Text(text) => write_string(text, out),
        Node::Map(entries) if ARRAY_CONTAINERS.contains(&key) => {
            out.push('[');
            for (i, item) in entries.iter().enumerate() {
                if i > 0 {
                    out.push(',');
                }
                newline(depth + 1, out);
                write_object(std::slice::from_ref(item), depth + 1, out);
            }
            if !entries.is_empty() {
                newline(depth, out);
            }
            out.push(']');
        }
        Node::Map(entries) => write_object(entries, depth, out),
    }
}

/// Whether the firmware writes `text` as a bare JSON number: an optional `-` and decimal digits, no leading zero.
fn is_bare_number(text: &str) -> bool {
    let digits = text.strip_prefix('-').unwrap_or(text);
    !digits.is_empty()
        && digits.bytes().all(|b| b.is_ascii_digit())
        && (digits == "0" || !digits.starts_with('0'))
        && digits.len() <= 10
}

fn write_string(text: &str, out: &mut String) {
    out.push('"');
    for c in text.chars() {
        match c {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            c if (c as u32) < 0x20 => out.push_str(&format!("\\u{:04x}", c as u32)),
            c => out.push(c),
        }
    }
    out.push('"');
}

fn newline(depth: usize, out: &mut String) {
    out.push('\n');
    for _ in 0..depth {
        out.push('\t');
    }
}

/// The firmware's XML parser, rule for rule: names may start with a digit; the five predefined entities are
/// unescaped and any other `&` kept; comments and the prolog are skipped; text beside attributes or children is
/// dropped; an element with neither attributes, children nor text is an empty map.
struct Parser<'a> {
    text: &'a [u8],
    src: &'a str,
    pos: usize,
}

impl Parser<'_> {
    fn document(&mut self) -> Result<Node, UpgradeError> {
        let mut doc = Vec::new();
        self.skip_misc();
        loop {
            let (name, element) = self.element()?;
            doc.push((name, element));
            self.skip_misc();
            if self.pos >= self.text.len() {
                return Ok(Node::Map(doc));
            }
        }
    }

    fn at_end(&self) -> bool {
        self.pos >= self.text.len()
    }

    fn peek(&self) -> u8 {
        self.text[self.pos]
    }

    fn starts_with(&self, s: &str) -> bool {
        self.text[self.pos..].starts_with(s.as_bytes())
    }

    fn consume(&mut self, s: &str) -> bool {
        if self.starts_with(s) {
            self.pos += s.len();
            true
        } else {
            false
        }
    }

    fn skip_whitespace(&mut self) {
        while !self.at_end() && matches!(self.peek(), b' ' | b'\t' | b'\n' | b'\r') {
            self.pos += 1;
        }
    }

    fn skip_past(&mut self, end: &str) -> bool {
        match self.src[self.pos..].find(end) {
            Some(at) => {
                self.pos += at + end.len();
                true
            }
            None => {
                self.pos = self.text.len();
                false
            }
        }
    }

    fn skip_misc(&mut self) {
        loop {
            self.skip_whitespace();
            if self.starts_with("<?") {
                self.pos += 2;
                self.skip_past("?>");
            } else if self.starts_with("<!--") {
                self.pos += 4;
                self.skip_past("-->");
            } else {
                return;
            }
        }
    }

    fn is_name_start(c: u8) -> bool {
        c.is_ascii_alphanumeric() || c == b'_' || c == b':'
    }

    fn is_name_char(c: u8) -> bool {
        Self::is_name_start(c) || c == b'-' || c == b'.'
    }

    fn name(&mut self) -> Result<String, UpgradeError> {
        if self.at_end() || !Self::is_name_start(self.peek()) {
            return Err(UpgradeError::Malformed("expected a name"));
        }
        let start = self.pos;
        while !self.at_end() && Self::is_name_char(self.peek()) {
            self.pos += 1;
        }
        Ok(self.src[start..self.pos].to_string())
    }

    fn attribute_value(&mut self) -> Result<String, UpgradeError> {
        if self.at_end() || !matches!(self.peek(), b'"' | b'\'') {
            return Err(UpgradeError::Malformed("expected a quoted attribute value"));
        }
        let quote = self.peek();
        self.pos += 1;
        let start = self.pos;
        while !self.at_end() && self.peek() != quote {
            self.pos += 1;
        }
        if self.at_end() {
            return Err(UpgradeError::Malformed("unterminated attribute value"));
        }
        let raw = &self.src[start..self.pos];
        self.pos += 1;
        Ok(unescape(raw))
    }

    fn element(&mut self) -> Result<(String, Node), UpgradeError> {
        if !self.consume("<") {
            return Err(UpgradeError::Malformed("expected an element"));
        }
        let name = self.name()?;
        let mut entries = Vec::new();
        let mut has_attributes = false;
        loop {
            self.skip_whitespace();
            if self.at_end() {
                return Err(UpgradeError::Malformed("unterminated tag"));
            }
            if matches!(self.peek(), b'/' | b'>') {
                break;
            }
            let attribute = self.name()?;
            self.skip_whitespace();
            if !self.consume("=") {
                return Err(UpgradeError::Malformed(
                    "expected '=' after an attribute name",
                ));
            }
            self.skip_whitespace();
            let value = self.attribute_value()?;
            entries.push((attribute, Node::Text(value)));
            has_attributes = true;
        }
        if self.consume("/>") {
            return Ok((name, Node::Map(entries)));
        }
        if !self.consume(">") {
            return Err(UpgradeError::Malformed("stray '/' in a tag"));
        }
        let body = self.content(&name, has_attributes, entries)?;
        Ok((name, body))
    }

    fn content(
        &mut self,
        name: &str,
        has_attributes: bool,
        mut entries: Vec<(String, Node)>,
    ) -> Result<Node, UpgradeError> {
        let mut text = String::new();
        let mut saw_child = false;
        loop {
            if self.at_end() {
                return Err(UpgradeError::Malformed("unterminated element"));
            }
            if self.starts_with("</") {
                self.pos += 2;
                let close = self.name()?;
                self.skip_whitespace();
                if close != name || !self.consume(">") {
                    return Err(UpgradeError::Malformed("mismatched closing tag"));
                }
                break;
            }
            if self.starts_with("<!--") {
                self.pos += 4;
                if !self.skip_past("-->") {
                    return Err(UpgradeError::Malformed("unterminated comment"));
                }
                continue;
            }
            if self.peek() == b'<' {
                entries.push(self.element()?);
                saw_child = true;
                continue;
            }
            let start = self.pos;
            while !self.at_end() && self.peek() != b'<' {
                self.pos += 1;
            }
            text.push_str(&unescape(&self.src[start..self.pos]));
        }
        if has_attributes || saw_child || text.trim_matches([' ', '\t', '\r', '\n']).is_empty() {
            return Ok(Node::Map(entries));
        }
        Ok(Node::Text(text))
    }
}

/// The five predefined entities; any other `&` is kept as it is.
fn unescape(raw: &str) -> String {
    const ENTITIES: [(&str, char); 5] = [
        ("&amp;", '&'),
        ("&lt;", '<'),
        ("&gt;", '>'),
        ("&quot;", '"'),
        ("&apos;", '\''),
    ];
    let mut out = String::with_capacity(raw.len());
    let mut rest = raw;
    while let Some(at) = rest.find('&') {
        out.push_str(&rest[..at]);
        rest = &rest[at..];
        match ENTITIES.iter().find(|(entity, _)| rest.starts_with(entity)) {
            Some((entity, c)) => {
                out.push(*c);
                rest = &rest[entity.len()..];
            }
            None => {
                out.push('&');
                rest = &rest[1..];
            }
        }
    }
    out.push_str(rest);
    out
}
