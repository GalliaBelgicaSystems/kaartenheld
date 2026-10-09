"""Session: one headless PyBoy boot of the release ROM with semantic state
access (docs/verify-walkthrough.md §3 Phase 2).

Replaces the per-walk duplicated closures of the old capture_walkthrough
(pos/pos2/pos3/pos5, walk/walk2/..., press/press2/...) with one class.
Every walk in walks.py instantiates its own Session (fresh boot = fresh
state, matching §56.2's five-fresh-sessions design).
"""

import io
import os
import sys
import time

from pyboy import PyBoy

from walkthrough.state_reader import StateReader

REPO = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))
ROM = os.path.join(REPO, "build", "kaartenheld.gb")
OUT = os.path.join(REPO, "screenshots")
LEVELS_DIR = os.path.join(REPO, "levels")

# Boot settle: ticks to run after PyBoy construction before touching state.
BOOT_TICKS = 180

# Title / intro BG markers (same self-healing boot as the old walkthrough:
# press START until the overworld; a START landing on the open overworld
# opens the quick screen, which the loop detects and closes again).
TITLE_LINES = (
    "A GAME BY",
    "GALLIA BELGICA",
    "KAARTENHELD",
    "BATTLE DEMO",
    "NEW GAME",
    "CONTINUE",
    "SOUND",
    "A troubled land",
    "calls out for a",
    "hero of a new",
    "kind.",
    "Your strength is",
    "not in steel,",
    "but in the cards",
    "you carry.",
    "Only the Lord of",
    "Slimes stands",
    "between all that",
    "lives and the end.",
    "[A] NEXT",
)


class Session:
    def __init__(self, checks, label, rom=ROM, ram_file=None, boot=True):
        self.checks = checks
        self.label = label
        self.rom = rom
        self.closed = False
        if ram_file is not None:
            ram_file.seek(0)
        self.pb = PyBoy(rom, window="null", ram_file=ram_file)
        for _ in range(BOOT_TICKS):
            self.pb.tick()
        self.reader = StateReader(self.pb, rom)
        import json
        field = json.load(open(os.path.join(LEVELS_DIR, "field.json")))
        self.spawn = field["player"]["spawn"]
        self.reader.assert_boot_anchors(self.spawn, label)
        self.t_start = time.time()
        if boot:
            self.boot_to_overworld()

    # ── lifecycle ────────────────────────────────────────────────────
    def close(self):
        if self.closed:
            return
        self.closed = True
        try:
            self.reader.close()
        finally:
            self.pb.stop(save=False)

    def stop_save_ram(self):
        """Stop the emulator, returning the cartridge RAM (battery save)
        as bytes (docs/verify-walkthrough.md §3 Phase 4, load roundtrip)."""
        self.closed = True
        buf = io.BytesIO()
        try:
            self.reader.close()
        finally:
            self.pb.stop(save=True, ram_file=buf)
        return buf.getvalue()

    # ── boot ─────────────────────────────────────────────────────────
    def on_title_or_intro(self):
        # The canonical screen id is authoritative (g_game.screen, offset 0
        # of the Game struct): the splash renders a bitmap logo whose BG
        # tiles read as non-text, so the text markers alone cannot detect it.
        screen = self.pb.memory[self.reader.g_game]
        if screen in (9, 10, 11, 12):  # TITLE, INTRO, TUTORIAL, SPLASH
            return True
        return any(m in r for r in self.bg_text() for m in TITLE_LINES)

    def quick_open(self):
        return self.text_has("CARDS QUEST")

    def on_overworld(self):
        return (not self.quick_open()) and not self.on_title_or_intro()

    def _stable_overworld(self, frames=15):
        """on_overworld held for `frames` consecutive ticks.  The boot
        studio splash / title / intro each perform an LCD-off redraw that
        briefly blanks the screen, and on_overworld is a negative
        heuristic (anything not title/intro/quick counts) — a single
        blank transition frame would otherwise be mistaken for the
        overworld (AGENTS.md 56.2)."""
        return self.stable(self.on_overworld, frames=frames)

    def boot_to_overworld(self, tries=12):
        """Press START through the boot splash, title menu and the 3-slide
        intro until the overworld is stable (self-healing, per §56.2)."""
        self.check("%s: reached overworld" % self.label,
                   self.wait_for(self._stable_overworld, ticks=120)
                   or self._start_through(tries))
        return self.on_overworld()

    def _start_through(self, tries):
        for _ in range(tries):
            self.wait_for(lambda: self.screen_alnum(), ticks=360)
            if self._stable_overworld():
                return True
            self.press("start", settle=40)
            if self.wait_for(self._stable_overworld, ticks=120):
                return True
        return self.on_overworld()

    # ── low-level primitives ─────────────────────────────────────────
    def tick(self, n=1):
        for _ in range(n):
            self.pb.tick()

    def bg_text(self):
        """Visible BG tilemap as 18 rows x 20 chars (font tiles read as
        ASCII; terrain tiles become '?').  SCX/SCY-aware (the same read
        the old walkthrough used, AGENTS.md §56.2)."""
        pb = self.pb
        scx = pb.memory[0xFF43]
        scy = pb.memory[0xFF42]
        rows = []
        for r in range(18):
            line = ""
            for c in range(20):
                t = pb.memory[0x9800 + ((scy // 8 + r) % 32) * 32
                               + (scx // 8 + c) % 32]
                line += chr(t + 32) if t < 96 else "?"
            rows.append(line)
        return rows

    def text_has(self, needle):
        return any(needle in r for r in self.bg_text())

    def pos(self):
        """World player tile position (the same WRAM locate the old
        walkthrough used; also available semantically via
        reader.scene_state())."""
        a = self.reader.world + 5
        return (self.pb.memory[a], self.pb.memory[a + 1])

    def press(self, btn, settle=12):
        """A 4-tick press edge (edge-triggered input; PyBoy applies
        queued events at frame boundaries, so a one-tick press can miss
        the window entirely)."""
        self.pb.button_press(btn)
        self.tick(4)
        self.pb.button_release(btn)
        self.tick(settle)

    def wait_for(self, cond, ticks=600, label=""):
        for _ in range(ticks):
            if cond():
                return True
            self.tick()
        return bool(cond())

    def press_until(self, btn, cond, tries=8, settle=30, timeout=90,
                    label=""):
        """Press `btn` repeatedly until cond() holds (a dropped press or
        an in-transition eat is retried).  Warns loudly on exhaustion."""
        for _ in range(tries):
            if cond():
                return True
            self.press(btn, settle=settle)
            if self.wait_for(cond, ticks=timeout):
                return True
        if not cond():
            print("warning: %s: press_until(%s%s) never reached its "
                  "condition" % (self.label, btn,
                                 " " + label if label else ""),
                  file=sys.stderr)
        return bool(cond())

    def stable(self, cond, frames=20):
        if not cond():
            return False
        for _ in range(frames):
            self.tick()
            if not cond():
                return False
        return True

    def screen_alnum(self):
        return any(any(c.isalnum() for c in row) for row in self.bg_text())

    def screen_rendered(self):
        """True when the framebuffer is non-blank (a full-screen
        transition wipes the display white for tens of frames — ~54 on
        the FIELD->TOWN gate crossing).  Render readiness only; never a
        gameplay heuristic (AGENTS.md §56.2)."""
        im = self.pb.screen.image.convert("RGB")
        colors = im.getcolors(maxcolors=100000)
        return colors is not None and len(colors) > 1

    def settle_scene(self, expected_scene=None, frames=40, ticks=1800):
        """Wait out a transition until canonical state is coherent:
        state.scene position matches the world player (and, when given,
        the expected scene id), held stable for `frames`.  Used after
        exits/events before semantic asserts."""
        def coherent():
            st = self.reader.scene_state()
            ok = (st["player_x"], st["player_y"]) == self.pos()
            if expected_scene is not None:
                ok = ok and st["scene_id"] == expected_scene
            return ok
        if not self.wait_for(coherent, ticks=ticks):
            return False
        return self.stable(coherent, frames=frames)

    # ── walking ──────────────────────────────────────────────────────
    def walk_btn(self, btn, goal, budget=600):
        """Discrete one-tile presses until goal() holds; a press that
        produced no movement is retried, so the route converges
        regardless of host timing (same mechanics as the old walk())."""
        for _ in range(budget):
            if goal():
                return True
            x0, y0 = self.pos()
            self.pb.button_press(btn)
            self.tick(4)
            self.pb.button_release(btn)
            for _ in range(24):
                self.tick()
                if self.pos() != (x0, y0):
                    break
        return bool(goal())

    def wiped(self, frames=90):
        for _ in range(frames):
            self.tick()
            if not self.screen_alnum():
                return True
        return False

    def cross_exit(self, btn, expect_scene, tries=4):
        """Step onto an exit tile and ride out the transition wipe.  A
        dropped press leaves us unmoved — only the wipe tells them
        apart, so a wipe-free arrival is retried, never trusted."""
        for _ in range(tries):
            self.press(btn, settle=12)
            if self.wiped() and self.settle_scene():
                return self.reader.scene_state()["scene_id"] == expect_scene
        return self.reader.scene_state()["scene_id"] == expect_scene

    # ── OAM ──────────────────────────────────────────────────────────
    def oam_actor_ys(self):
        """Real-OAM Y bytes for the actor region (entries 1..26; entry 0 is
        the player).  0 = hidden; GBDK stores screenY + 16.  Used to assert
        nothing draws over the dialogue box."""
        base = 0xFE00
        return [self.pb.memory[base + i * 4] for i in range(1, 27)]

    # ── assertions ───────────────────────────────────────────────────
    def check(self, name, ok, expected="", actual=""):
        self.checks.append((self.label, name, bool(ok),
                            expected, actual))
        return ok

    def check_eq(self, name, actual, expected):
        return self.check(name, actual == expected,
                          expected=repr(expected), actual=repr(actual))

    # ── screenshots ──────────────────────────────────────────────────
    def shoot(self, label, need=None):
        self.wait_for(self.screen_rendered, ticks=360)
        self.tick(8)   # settle frames so the new screen finishes drawing
        path = os.path.join(OUT, label + ".png")
        self.pb.screen.image.save(path)
        rows = self.bg_text()
        top = next((r.strip() for r in rows if r.strip()), "")
        print("saved", os.path.relpath(path, REPO), "[%s]" % top[:18])
        if need:
            self.check("%s shows %r" % (label, need), self.text_has(need),
                       expected=need, actual=top[:20])
        return path
