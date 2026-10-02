# Rust documentation style

How the Rust in this repository is documented: `crates/`, `src/bsp/rust/`, and the Rust harnesses
under `tests/component/differential/`. The vendored crates (`crates/embedded-fatfs`,
`crates/block-device-*`) keep their upstream style.

Two goals, in order: **accurate**, then **short**. A comment says what the code is and guarantees
now, and why; it says it once, where a reader looks for it.

## Form

- `//!` opens every module and crate: one summary sentence, a blank line, then only what someone
  using or changing the module needs. A crate root may use `#` headings.
- `///` documents items. The first line is one sentence ending in a period: third person for
  functions ("Opens …"), a noun phrase for types and constants ("The single-slot …").
- Sections, when they apply, in this order: `# Errors`, `# Panics`, `# Safety`. Every `unsafe fn`,
  and every `extern "C"` export that dereferences a caller's pointer, has `# Safety` stating what
  the caller must guarantee. Every `unsafe` block has a `// SAFETY:` comment saying why it holds.
- A module is documented in its own file. A `mod foo;` line carries no `///`; at most a `//` line
  explaining a non-obvious `cfg`.
- `//` comments explain *why*, directly above the code they explain.
- Link Rust items with intra-doc links (`` [`Tier`] ``, `` [`Self::take`] ``,
  `` [`crate::storage::card::generation`] ``). C and C++ names, headers (`libdeluge/tier.h`), file
  paths and commands go in plain backticks.
- Wrap comment text at 100 columns. Use `—` or restructure the sentence, never `--`.
- No capitalised words for emphasis. Use `*emphasis*` rarely.

## Keep

Invariants; safety contracts; ordering and lifetime constraints; which tier or executor may call
something and whether it may block; units, encodings, Q-formats and sentinels; ABI facts (a
`#[repr(C)]` type mirrors a header type; a symbol has no C++ fallback, so it must be linked); the
non-obvious reason for a current decision; live `TODO`s.

## Leave out

- Process and history: task, rung, phase and milestone labels, dates, "now", "no longer", "used
  to", "moved from", "a later task will". Git and the design docs hold those.
- Restatements of the code, and field docs that repeat the field's name.
- Chains of cross-references ("see X's doc, which mirrors Y"). One link, when the reader needs it.
- Essays on why something is not done another way. One sentence, if the other way is a trap a
  maintainer would walk into.
- Measurement logs. Keep the conclusion if it justifies a value.
- Claims you have not checked. Before writing "nothing calls this" or "only X uses it", search.

## TOML

A comment in `Cargo.toml`, `config.toml` or `rust-toolchain.toml` earns its place only when the
setting is non-obvious and someone would otherwise "fix" it wrongly: a pin and why, a feature gate
and why, a patch and what breaks without it. Feature documentation is `##` lines above the feature.

## Checking

`cargo doc --no-deps --document-private-items` must be free of warnings in every package; CI's
`rustdoc` job builds the docs with `-D warnings`. The `doc-cleanup` skill's guard
(`.claude/skills/doc-cleanup/scripts/assert_doc_only_rust.py`) proves a documentation-only change
touched no code.
