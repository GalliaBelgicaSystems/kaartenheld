"""Parity guard for two-way tunnel diagnostics (review-13ba2f5 #2).

Locks the Python side of the contract shared with TS `tunnelStatus()`
in tools/level_editor/vite.config.ts: same invariants (count == 2,
different levels, mutual targets, spawn-tracks-gate) over the shared
corpus in tunnel_parity_cases.json.

Run: python3 tools/level_compiler/tests/test_tunnel_parity.py
Exit code 0 = all cases agree, 1 = drift/failure.
"""
import json
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from collision import tunnel_report

CORPUS = os.path.join(os.path.dirname(__file__), "tunnel_parity_cases.json")


def main():
    with open(CORPUS, encoding="utf-8") as f:
        cases = json.load(f)["cases"]
    failures = 0
    for case in cases:
        name = case["name"]
        tunnel = case["tunnel"]
        report = {e["tunnel"]: e for e in tunnel_report(case["levels"])}
        entry = report.get(tunnel)
        if entry is None:
            print("FAIL %s: tunnel '%s' missing from report" % (name, tunnel))
            failures += 1
            continue
        if entry["ok"] != case["ok"]:
            print("FAIL %s: ok=%r want %r (errors=%r)"
                  % (name, entry["ok"], case["ok"], entry["errors"]))
            failures += 1
            continue
        frag = case["py_fragment"]
        if frag is not None:
            blob = " ".join(entry["errors"])
            if frag not in blob:
                print("FAIL %s: fragment %r not in %r" % (name, frag, blob))
                failures += 1
                continue
        print("ok %s" % name)
    if failures:
        print("%d/%d cases FAILED" % (failures, len(cases)))
        return 1
    print("all %d cases pass" % len(cases))
    return 0


if __name__ == "__main__":
    sys.exit(main())
