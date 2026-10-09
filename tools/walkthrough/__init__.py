"""Semantic walkthrough package (docs/verify-walkthrough.md).

Drives the RELEASE ROM (real content from levels/) headlessly under PyBoy
and asserts semantic state read from WRAM via the ROM's own symbol table.
This is the real-content counterpart of the fixture-based scenario harness:
the two-tier test build never sees levels/, so engine regressions that only
fire with working content are caught here instead.
"""
