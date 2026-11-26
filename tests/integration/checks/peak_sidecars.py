"""The peak-sidecars gate: a sample's sidecar through the card.

Drives one sample per behaviour through the idle job's sidecar machine and reads the card directly:
    PS-WRITE            a complete index leaves a sound sidecar under its key and no .TMP
    PS-PITCH-WRITE      a detected pitch is written to the sidecar's PTCH section
    PS-RELOAD           a reloaded sample's index is complete from its sidecar alone (the scan held off)
    PS-PITCH            and it answers its detected pitch without a detection
    PS-REWRITE          once the file changes (a computer appended to it), its sidecar is refused and a new one is
                        written under the new key
    PS-CORRUPT          a sidecar damaged on the card is refused, and removed
    PS-FULL, PS-PROTECT a full or write-protected card is tried twice, then left alone, with nothing on the card
    PS-TMP              a stray .TMP of the key is overwritten by its next write
    PS-PARTIAL          a partial index is written from a quarter complete, not again until it grows by a quarter
    PS-TRIM             a deep-zoom view scrolled across an eight-chunk file holds at most four level-0 chunks at
                        once, and none once nothing reads

Gate peak-sidecars:
    a sample's sidecar through the card. One sample per behaviour, driven through the idle job's sidecar machine, the
    card read back directly: a complete index's sidecar is sound and leaves no .TMP; a detected pitch is written to the
    PTCH section; a reload completes the index from the sidecar alone with the background scan held off, and answers
    the pitch without a detection; a file changed on a computer has its sidecar refused and rewritten under the new
    key; a sidecar damaged on the card is refused and removed; a full or write-protected card (host write faults) is tried twice, then left alone; a stray .TMP is
    overwritten; a partial index is written from a quarter complete and again only after a quarter's growth; a deep-zoom
    view scrolled across an eight-chunk file holds at most four level-0 chunks at once, and none once nothing reads.
"""


def run(gate):
    result = gate.run(
        env={
            "GOLDEN_VIRTUAL_BUDGET_MS": gate.env.get(
                "GOLDEN_VIRTUAL_BUDGET_MS", "1800000"
            )
        }
    )
    if not gate.require_standard(result):
        return
    for line in result.starting("PS-"):
        gate.say(line)
