"""The audible window's song set: Glitchy_2 (the song the matrix's patching cost was measured on), then from the
corpus the song with the most patch cables, the one with the most kit rows and the one with the most automated params,
each not already chosen. A song is named by its path under SONGS/, without `.json`.

The corpus holds community-format JSON songs: a cable is a `patchCable` object, a kit's rows are its `soundSources`
list, and an automated param is a string of more than one hex word (0x and 9 or more hex digits). Note rows use the same
encoding under keys starting with `noteData`, which do not count.
"""

import json
import re
from collections import Counter
from pathlib import Path

FIXED = ["Glitchy_2"]
ORDER = ["cables", "kit_rows", "automated"]
AUTOMATED = re.compile(r"0x[0-9A-Fa-f]{9,}")


def counts(song) -> Counter:
    """The cables, kit rows and automated params in @p song (parsed JSON)."""
    out: Counter = Counter()

    def walk(node, key: str = "") -> None:
        if isinstance(node, dict):
            for k, v in node.items():
                if k == "patchCable":
                    out["cables"] += 1
                if (
                    k == "kit"
                    and isinstance(v, dict)
                    and isinstance(v.get("soundSources"), list)
                ):
                    out["kit_rows"] += len(v["soundSources"])
                walk(v, k)
        elif isinstance(node, list):
            for v in node:
                walk(v, key)
        elif (
            isinstance(node, str)
            and not key.startswith("noteData")
            and AUTOMATED.fullmatch(node)
        ):
            out["automated"] += 1

    walk(song)
    return out


def pick(corpus: Path) -> list[str]:
    """The song set from @p corpus (a SONGS directory)."""
    corpus = Path(corpus)
    tallies: dict[str, Counter] = {}
    for path in sorted(corpus.rglob("*.json")):
        name = path.relative_to(corpus).with_suffix("").as_posix()
        try:
            tallies[name] = counts(json.loads(path.read_text(errors="replace")))
        except json.JSONDecodeError:
            continue
    chosen = [name for name in FIXED if name in tallies]
    for measure in ORDER:
        candidates = [n for n in tallies if n not in chosen and tallies[n][measure] > 0]
        if candidates:
            chosen.append(max(sorted(candidates), key=lambda n: tallies[n][measure]))
    return chosen
