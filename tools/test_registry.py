#!/usr/bin/env python3
"""Registry invariant tests (levels/registry.json contract).

Runs against a temporary levels directory so it never touches real content.
Exits non-zero on any failure.  Invoked by `make registry-check` (CI).

Covers the future-proofing rules the editor and compiler both rely on:
versioning, append-only ids, never-reuse of retired ids, and registry/file
agreement.  The editor's rename/delete file operations are exercised
end-to-end against the live dev server separately; this gate locks the
contract those operations must satisfy.
"""
import json
import shutil
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / "level_compiler"))

import scene_registry as sr  # noqa: E402
import validate as vd  # noqa: E402
import compile as lc  # noqa: E402
from validate import (  # noqa: E402
    validate_registry_consistency, registry_warnings,
)


def use_root(root):
    """Point the registry reader at a temp root (the production path is
    repo-relative; tests relocate it)."""
    reg_path = Path(root) / "levels" / "registry.json"
    sr.registry_path = lambda: reg_path
    vd.load_registry = sr.load_registry


def write(root, scenes, retired=None, test_base=240, version=1,
          level_ids=()):
    levels = Path(root) / "levels"
    levels.mkdir(parents=True, exist_ok=True)
    # Each case starts from a clean slate (no leftover level files).
    for old in levels.glob("*.json"):
        old.unlink()
    (levels / "registry.json").write_text(json.dumps({
        **({"version": version} if version is not None else {}),
        "scenes": scenes,
        "_retired": retired or {},
        "_test_base": test_base,
    }, indent=2) + "\n", encoding="utf-8")
    for sid in level_ids:
        (levels / f"{sid}.json").write_text(
            json.dumps({"id": sid, "exits": []}, indent=2) + "\n",
            encoding="utf-8")


def expect_exit(fn, needle):
    try:
        fn()
    except SystemExit as exc:
        return needle in str(exc)
    except Exception as exc:  # noqa: BLE001
        return needle in str(exc)
    return False


def main():
    failures = []

    def check(label, ok):
        print(f"  [{'PASS' if ok else 'FAIL'}] {label}")
        if not ok:
            failures.append(label)

    tmp = tempfile.mkdtemp(prefix="regtest_")
    use_root(tmp)
    try:
        # Version too new -> rejected.
        write(tmp, {"field": 0}, version=99, level_ids=("field",))
        check("version too new rejected",
              expect_exit(sr.load_registry, "registry"))
        # Missing version accepted (pre-version upgrade), upgraded in memory.
        write(tmp, {"field": 0}, version=None, level_ids=("field",))
        reg = sr.load_registry()
        check("missing version accepted",
              reg["version"] == sr.REGISTRY_VERSION)

        write(tmp, {"field": 0}, version=1, level_ids=("field",))
        check("registry/file agreement clean",
              validate_registry_consistency(Path(tmp) / 'levels') == []
              and registry_warnings(Path(tmp) / 'levels') == [])

        # Duplicate numeric id -> rejected.
        write(tmp, {"a": 3, "b": 3}, version=1, level_ids=("a", "b"))
        check("duplicate id rejected",
              expect_exit(sr.load_registry, "duplicate"))

        # test_ name refused.
        write(tmp, {"test_field": 0}, version=1)
        check("test_ name refused",
              expect_exit(sr.load_registry, "reserved"))

        # A retired id must not be re-assigned.
        write(tmp, {"field": 0}, retired={"old": 0}, version=1)
        check("retired id reuse rejected",
              expect_exit(sr.load_registry, "retired"))

        # Missing level file -> loud error naming the level.
        write(tmp, {"field": 0, "forest": 1}, version=1,
              level_ids=("field",))
        check("missing file flagged",
              expect_exit(validate_registry_consistency,
                          "forest") or
              any("forest" in e for e in validate_registry_consistency(Path(tmp) / 'levels')))

        # Unregistered level file -> loud error naming the fix.
        write(tmp, {"field": 0}, version=1, level_ids=("field", "orphan"))
        check("unregistered file flagged",
              any("orphan" in e for e in validate_registry_consistency(Path(tmp) / 'levels')))

        # Retired id with a lingering file -> hard error (the stale file
        # would block Compile ROM and keep its actor ids reserved).
        write(tmp, {"field": 0}, retired={"gone": 9}, version=1,
              level_ids=("field", "gone"))
        check("retired lingering file errors",
              any("gone" in e and "retired" in e.lower()
                  for e in validate_registry_consistency(Path(tmp) / 'levels')))

        # next-id assignment never reuses a retired id.
        write(tmp, {"field": 0}, retired={"gone": 1}, version=1)
        reg = sr.load_registry()
        used = set(reg["scenes"].values())
        retired = set(reg["retired"].values())
        check("retired id never reused",
              not (used & retired))

        # MAP_REAL_COUNT must be max live id + 1 (the scene table length,
        # holes included), not the live count: after a delete, ids are
        # sparse and a level added at max+1 would otherwise be rejected at
        # runtime (NULL scene def -> default map/music/no actors).
        write(tmp, {"field": 0, "forest": 2}, retired={"gone": 1},
              version=1, level_ids=("field", "forest"))
        reg = sr.load_registry()
        hdr = lc.emit_ids_header(reg)
        check("MAP_REAL_COUNT = max id + 1 with retired holes",
              "#define MAP_REAL_COUNT 3" in hdr)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    if failures:
        print(f"\nregistry-check: {len(failures)} failure(s): "
              f"{', '.join(failures)}", file=sys.stderr)
        return 1
    print("\nregistry-check: all invariants OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
