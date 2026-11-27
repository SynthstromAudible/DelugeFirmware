#!/usr/bin/env python3
"""The schema-lock gate: the clean format's schema changes only with a schema revision bump and a re-recorded lock.

Builds and runs schema_lock in the host tree and compares its dump with tests/golden/schema/schema.lock.json.
./dbt test --update schema-lock re-records the lock, for a new revision only.

Gate schema-lock:
    the clean format's schema, every record a song or a preset reaches, equals the recorded lock; a change needs a
    schema revision bump and a re-recorded lock, and two records never share a name
"""

import json
import pathlib
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parents[4]
sys.path.insert(0, str(REPO / "scripts" / "tasks"))

import golden_update

LOCK = REPO / "tests" / "golden" / "schema" / "schema.lock.json"
TREE = REPO / "build" / "test-host"
TOOL = TREE / "unit" / "project" / "clean" / "Debug" / "schema_lock"
REVISION = "kSchemaRevision in src/project/clean_codec.h"
RERECORD = "./dbt test --update schema-lock"


def dump() -> dict:
    subprocess.run(
        ["cmake", "--build", str(TREE), "--target", "schema_lock", "--config", "Debug"],
        check=True,
        stdout=subprocess.DEVNULL,
    )
    return json.loads(
        subprocess.run([str(TOOL)], check=True, capture_output=True, text=True).stdout
    )


def first_change(now: dict, recorded: dict) -> str:
    """The first record whose description differs, or that one side lacks."""
    old = {record["name"]: record for record in recorded["records"]}
    for record in now["records"]:
        if old.get(record["name"]) != record:
            return record["name"]
    new = {record["name"] for record in now["records"]}
    gone = [name for name in old if name not in new]
    return gone[0] if gone else "the records' order"


def write(schema: dict) -> None:
    LOCK.parent.mkdir(parents=True, exist_ok=True)
    golden_update.write_text(
        LOCK, json.dumps(schema, indent=1) + "\n", str(LOCK.relative_to(REPO))
    )


def main() -> int:
    if not (TREE / "build.ninja").exists():
        print(
            "schema-lock: configure the host tree first (./dbt test unit)",
            file=sys.stderr,
        )
        return 2
    now = dump()
    names = [record["name"] for record in now["records"]]
    twice = sorted({name for name in names if names.count(name) > 1})
    if twice:
        print(
            f"schema-lock: two records share a name ({', '.join(twice)}): give each its own",
            file=sys.stderr,
        )
        return 1
    update = golden_update.updating()
    if not LOCK.exists():
        if update:
            write(now)
            print(f"schema-lock: recorded revision {now['revision']}")
            return 0
        print(
            f"schema-lock: no lock recorded; record one with {RERECORD}",
            file=sys.stderr,
        )
        return 1
    recorded = json.loads(LOCK.read_text())
    same_schema = now["records"] == recorded["records"]
    same_revision = now["revision"] == recorded["revision"]
    if same_schema and same_revision:
        print(f"schema-lock: OK at revision {now['revision']}")
        return 0
    if same_schema:
        print(
            f"schema-lock: the revision moved but the schema did not: put {REVISION} back to "
            f"{recorded['revision']}",
            file=sys.stderr,
        )
        return 1
    if same_revision:
        print(
            f"schema-lock: the schema changed at {first_change(now, recorded)} but the revision is still "
            f"{now['revision']}: bump {REVISION}, then re-record with {RERECORD}",
            file=sys.stderr,
        )
        return 1
    if update:
        write(now)
        print(f"schema-lock: re-recorded at revision {now['revision']}")
        return 0
    print(
        f"schema-lock: revision {now['revision']} (changed at {first_change(now, recorded)}) is not recorded: "
        f"re-record with {RERECORD}",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
