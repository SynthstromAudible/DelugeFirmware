"""The recorder-take gate: an audio-input recording take leaves the app loop running.

The take is started the way a user starts it, from the sound editor's recorder menu item, so the gesture must return
with the take running under the recorder UI (RT-OPEN). A take that instead holds the caller until it settles holds the
whole app loop inside one executor poll: on this single-threaded harness neither the virtual clock nor the storage
channels the take writes through can then advance, so the run never ends. That is why this gate runs under a wall-clock
bound, and reports a run that hits it as the freeze rather than as a crash.
"""

# Wall-clock seconds. A passing run takes a few seconds.
TIMEOUT_S = 60


def run(gate):
    result = gate.run(timeout=TIMEOUT_S)
    if result.code == 124:
        gate.fail(
            f"RT-FROZEN the run did not end within {TIMEOUT_S} s of wall time: the recorder gesture held the app loop "
            "instead of returning with the take running"
        )
        gate.show(result)
        return
    if gate.require_standard(result):
        gate.show(result)
