# Golden data

What the tests compare with, one directory per consumer. The corpus runners that read and record part of it are
in `tests/integration/corpus/` (its README says how they run).

## Menu reference tables (`ui/menu_oracle/`)

Upstream `main`'s menu answers, one row per answer, which the ported menu's scenes specs check it against
(`sim/scenes_spec/support/oracle_table.h` reads them; every spec that reads a table also checks it asked every
row). A row is `path<TAB>column<TAB>values…`: the path names what answered (a menu node by its child positions
under its root, a setting case, a shortcut cell), the column the question, the values the answer. `pixels/` holds
panels as PBM, `pads/` the main grid's pad images: eight lines from the top row (y = 7) down, each sixteen `r,g,b`
triples separated by spaces (`sim/scenes_spec/support/oracle_files.h` reads both, and a spec that reads them checks it
read every file under its own prefix).

Fields are raw bytes: only tab, newline and backslash are escaped, so a glyph byte such as the flat sign (0x81 in
`menu_ranges.tsv`) is stored as it is and the files are not always valid UTF-8.

These are reference data, not recordings: nothing in the tree regenerates them. Change a row only to follow `main`,
reading its source with `git show origin/main:<path>`, and name that source in the commit.

The rows were frozen from `next`'s legacy menu at 16b2d012b, before it was deleted, and audited against `origin/main`
96c002803 by reading code, not by regenerating them (nothing can produce the rows from `main`): every one of the 86
`fix` and `feat` commits since the merge base 4cdf49002 that touches the menu items, `gui/l10n` or the runtime
settings was reviewed, the other 114 such commits were scanned for changes to titles, options, ranges, relevance and
learn, and a sample of items was checked against `main`'s source. Refactors were assumed to preserve behaviour where
their diffs changed no answer. Rows changed since the freeze followed `main` (`b0aee86a8`, `cb63476d1`). The rows hold
`main`'s answer except where they differ from `main` and the difference is accepted, each a row of
`tests/ui-scripts/parity/DIFFERENCES.md`: the sample repeat titles (D149), the delay's three types (D155), the
community features `main` has and `next` removed (D156) and the audio clip attack's write (D157).

Where an answer is a model value main's item left behind rather than text the item returned, the row is that value as
the port's `bindings::read` reads it after main's write: the first value of every `write/<v>` row, and the
`sweep/<path>` rows (each drum's value after main's affect-entire sweep). The second value of a `write/<v>` row is
main's own read of the item.
