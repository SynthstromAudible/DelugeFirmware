"""The path-operations gate: SysEx path requests, the browser's folder creation, and deleting a file from a browser.

What the checks cover (tests/integration/scenarios/path_operations_scenario.cpp writes the lines), and why each is separate:
    PO-SYSEX     that the protocol still answers. A real request for each covered verb must produce its ^verb reply,
                 captured from a loopback MIDICable rather than inferred from the filesystem -- asserting the effect
                 alone would pass for a request path that stopped replying, which is exactly what deferring a reply
                 risks.
    PO-MKDIRALL  that a nested path with all three segments missing is created by one request, and that a second
                 creation of an existing path is success rather than EXISTS.
    PO-DELETE    that a file deleted from a browser goes through the byte channel: the delete is still pending a step
                 after the menu accepts it, input that would close the menu is refused meanwhile, and then the file is
                 gone from the card and the listing.
    PO-DELETE-ABSENT
                 that deleting a listed file already gone from the card ends as missing, not removed, and a plain file
                 stays listed, as it always has.
    PO-DELETE-STALL
                 that a delete the channel never answers ends as failed, with the file still listed and the menu
                 closed.
    PO-TIER      that no browser folder creation or delete ran on the interaction tier.
    PO-ILLEGAL   that no folder creation or delete was refused for want of a legal place to run.
    PO-CHANNEL   that the async arm was executed. Required non-zero: smsysex's tier count reads zero whichever arm
                 carries it, so this counter is the whole of its evidence.
    TIER-SYNC-PROBE
                 the tier meter's positive control: the scenario makes one synchronous open after boot on the
                 interaction tier, and the meter must count exactly one. Without it, a meter that sees nothing and a
                 clean tree read the same.

The registry entry requires each marker and the result line; this module adds PO-CHANNEL's non-zero count and the
probe.
"""

from scenario_gate import count_field, field


def check(gate, run):
    served = count_field(run.first("PO-CHANNEL "), "served")
    if not served:
        gate.fail(
            f"PO-CHANNEL served={field(run.first('PO-CHANNEL '), 'served') or 'none'}: the byte channel served nothing, "
            "so the async arm never ran"
        )
        return
    probes = [field(line, "n") or "" for line in run.starting("TIER-SYNC-PROBE ")]
    if probes != ["1"]:
        found = "\n".join(probes) or "none"
        gate.fail(
            f"TIER-SYNC-PROBE — n={found}; the meter did not count the scenario's one synchronous open"
        )
        return
    gate.say(
        "PASS — the protocol replied, browser deletes went through the byte channel, no browser folder creation or"
    )
    gate.say(
        f"       delete ran on the interaction tier, and the byte channel served {served} operation(s)"
    )
