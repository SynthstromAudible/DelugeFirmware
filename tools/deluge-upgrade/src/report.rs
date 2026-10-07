//! What the upgrader could not convert, per file: shown on stderr and kept as JSON with `--report`.

/// One thing a file needs that its upgrade could not write.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Finding {
    /// The file it is in.
    pub file: String,
    /// The conversion that could not be made.
    pub conversion: &'static str,
    /// What was left, and why.
    pub detail: String,
}

/// Every finding of a run.
#[derive(Debug, Default)]
pub struct Report {
    pub findings: Vec<Finding>,
}

impl Report {
    /// Notes that `conversion` could not be made in `file`.
    pub fn note(&mut self, file: &str, conversion: &'static str, detail: String) {
        self.findings.push(Finding {
            file: file.to_string(),
            conversion,
            detail,
        });
    }

    /// The findings as a JSON array.
    pub fn to_json(&self) -> String {
        let items: Vec<String> = self
            .findings
            .iter()
            .map(|f| {
                format!(
                    "{{\"file\": {}, \"conversion\": {}, \"detail\": {}}}",
                    quote(&f.file),
                    quote(f.conversion),
                    quote(&f.detail)
                )
            })
            .collect();
        format!("[{}]\n", items.join(",\n "))
    }
}

fn quote(text: &str) -> String {
    format!("\"{}\"", text.replace('\\', "\\\\").replace('"', "\\\""))
}
