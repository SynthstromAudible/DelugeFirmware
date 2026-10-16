"""Every song setting through SetSetting, over every corpus song. The song_settings driver checks each song itself
(exit 0 holds, 1 does not load, 3 a value is refused or leaves a record and its engine copy apart) and prints how many
values it applied per group and path; this runner sums them and fails when a group was never reached.

    export DELUGE_SONG_CORPUS=~/path/to/SONGS
    python3 tests/golden/song_settings_run.py
"""

import argparse
import collections
import os
import shlex
import sys

import driver

# Every group the corpus must reach. A MIDI or CV device is not in every corpus, so the instrument device is reported
# but not required.
REQUIRED = ("SONG", "TRACK", "LANE", "NOTE_ROW", "AUDIO_CLIP", "CLIP", "CLIP_VALUES")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--binary", default=str(driver.default_binary()))
    ap.add_argument("--wrapper", default="")
    ap.add_argument("--corpus", default=driver.default_corpus() or "")
    ap.add_argument("--filter", default="")
    ap.add_argument("--timeout", type=int, default=300)
    ap.add_argument("--jobs", type=int, default=driver.default_jobs())
    args = ap.parse_args(argv)
    if not os.path.isdir(args.corpus):
        print(f"no corpus at {args.corpus!r} (set DELUGE_SONG_CORPUS)")
        return 77
    songs = driver.find_songs(args.corpus, args.filter)
    if not songs:
        print("nothing to check")
        return 77
    pre = shlex.split(args.wrapper)
    results = driver.map_ordered(
        lambda song: driver.run(
            args.binary, "song_settings", [song, "SONG"], args.timeout, pre
        ),
        songs,
        args.jobs,
    )
    root = os.path.dirname(os.path.abspath(args.corpus))
    ok = fail = skip = 0
    applied = collections.Counter()
    saved = collections.Counter()
    for song, r in zip(songs, results):
        rel = os.path.relpath(song, root)
        if r.returncode == 0:
            ok += 1
            for line in r.stdout.splitlines():
                if line.startswith("SONG-SETTINGS-SAVE "):
                    saved[line.split()[1]] += 1
                if line.startswith("SONG-SETTINGS "):
                    key, _, n = line[len("SONG-SETTINGS ") :].rpartition(" n=")
                    applied[key] += int(n)
        elif r.returncode == 1:
            skip += 1
            print(f"skip     {rel}")
        else:
            fail += 1
            tail = "\n           ".join(r.stderr.strip().splitlines()[-3:])
            print(f"FAIL(rc={r.returncode}) {rel}\n           {tail}")
    for key in sorted(applied):
        print(f"SONG-SETTINGS {key} n={applied[key]}")
    print("=" * 60)
    print(f"OK={ok} FAIL={fail} SKIP={skip}  (song_settings, corpus={args.corpus})")
    for leg in ("audioSource", "midiDevice"):
        print(f"SONG-SETTINGS-SAVE {leg} songs={saved[leg]}")
    if not saved["audioSource"] or not saved["midiDevice"]:
        print("FAIL: a save leg found no song to run on")
        return 1
    reached = {key.split(" ")[0] for key in applied}
    missing = [group for group in REQUIRED if group not in reached]
    if missing:
        print(f"FAIL: no {', '.join(missing)} setting was applied")
        return 1
    return 1 if fail or not ok else 0


if __name__ == "__main__":
    sys.exit(main())
