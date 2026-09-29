"""The save round-trip gate.

Saves a song through the REAL SaveSongUI ENTER gesture, loads it back, overwrites it, and requires the two written files
to be byte-identical. Also asserts the overwrite seam: a free name must not prompt, an occupied one must, and confirming
must close the prompt.

Its subject is a C++ structural change, so there is no second build to compare against. The round trip is
self-checking instead: nothing is baselined, so there is no golden anyone can quietly re-baseline into passing.

What the checks cover, and why each is separate:
  SR-SEAM       the prompt decisions. A save that silently overwrote would still round-trip fine.
  SR-ROUNDTRIP  the written bytes. A CRC, not a length -- a save that truncated or reordered would pass a length check.
  SL-NOSTREAM   that no streaming document reader exists. A SOURCE check, and unusually so: a gate cannot observe a
                path that is not compiled, so a grep is the only way to notice one appearing. A reader that streams a
                document off the card blocks the parsing tier for the whole parse, however rarely it runs.
  SL-EVICT      that the round trip still produces the SAME bytes when the song has to be staged into an SDRAM heap too
                small to hold it without evicting. Staging a song can give up cached sample chunks; that is a decision,
                so it gets measured. SL_EVICT_SDRAM_MB sets the clamp (2 MB by default).
"""

import subprocess

# The three StorageManager functions that would open a document by streaming it off the card while parsing it.
STREAMING_READERS = r"\bStorageManager::open(XML|Json)File\(|\bopenDelugeFile\("


def run(gate):
    first = gate.run()
    if not gate.require_standard(first):
        return
    gate.show(first)

    # SL-NOSTREAM. The streaming readers' absence is what makes "nothing parses from the card" true, and nothing at
    # runtime can notice one appearing, so it is checked here.
    found = subprocess.run(
        [
            "grep",
            "-rnE",
            STREAMING_READERS,
            str(gate.root / "src"),
            "--include=*.cpp",
            "--include=*.h",
            "--include=*.hpp",
        ],
        capture_output=True,
        text=True,
        check=False,
    )
    if found.returncode == 0:
        gate.fail("SL-NOSTREAM — a streaming document reader is back:")
        for line in found.stdout.splitlines():
            gate.say(line)
        gate.say(
            "  A streaming reader blocks on the card while it parses, which the interaction tier must never"
        )
        gate.say(
            "  do after boot. Parse from a staged buffer (StorageManager::openDocumentSynchronously) instead."
        )
        return
    gate.say("SL-NOSTREAM ok — no streaming document reader in src/")

    # SL-EVICT. Re-run the whole round trip with the usable SDRAM clamped low enough that staging a song cannot happen
    # without the resource manager evicting cached chunks first, and require the SAME result. alloc_sdram evicts rather
    # than failing, so this exercises that path rather than an error path -- and if the eviction ever corrupted or
    # truncated what was staged, the CRC comparison inside the run is what would notice.
    gate.say("save round-trip: running under a clamped SDRAM heap (SL-EVICT)")
    clamp = gate.env.get("SL_EVICT_SDRAM_MB") or "2"
    evict = gate.run(env={"DELUGE_SIM_SDRAM_MB": clamp})
    if evict.code != 0:
        gate.fail(f"SL-EVICT — the clamped-heap run exited {evict.code}")
        gate.show(evict)
        for line in evict.err.splitlines()[:25]:
            gate.say(line)
        return
    if not evict.has_line("SR-RESULT failures=0"):
        gate.fail("SL-EVICT — in-process failures under a clamped heap:")
        gate.show(evict)
        return
    # The round-trip CRC itself must be identical to the unclamped run: eviction must change WHAT IS CACHED, never what
    # is written.
    if first.starting("SR-ROUNDTRIP ") != evict.starting("SR-ROUNDTRIP "):
        gate.fail(
            "SL-EVICT — the clamped-heap run wrote different bytes than the unclamped one"
        )
        return
    gate.show(evict)
    gate.say(
        "SL-EVICT ok — same bytes with the song staged into a heap that had to evict first"
    )
    gate.say(
        "PASS — the song round-tripped byte-identically, the overwrite seam behaved, no streaming"
    )
    gate.say(
        "       document reader survives, and staging under eviction changed nothing"
    )
