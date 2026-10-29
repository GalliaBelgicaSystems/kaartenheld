#!/usr/bin/env python3
"""Measure gate-crossing transition cost in VBlanks (headless PyBoy).

Drives the RELEASE ROM (real timing), pokes the player next to a gate,
steps through it, and counts VBlanks from the scene flip to the player
sprite reappearing in real OAM (entry 0 Y != 0).  That count is exactly
the user-perceived "sprites take time to show" delay: sprites are hidden
up front by ui_sprite_begin_transition and only recommitted after the
full redraw (main.c game_render/commit ordering).

Usage:
    python3 tools/probe_transition.py [rom] [--trace LEG]

Prints per-leg walk ticks + transition ticks and a one-line summary.
Not a CI gate: an engineering probe for transition work.  Timing comes
from pb.frame_count (one PyBoy tick == one VBlank); the transition runs
synchronously inside game frames, so VBlanks keep elapsing through it.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from walkthrough.session import Session

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROM = sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].startswith(
    "--") else os.path.join(REPO, "build", "kaartenheld.gb")
TRACE = (sys.argv[2] == "--trace") if len(sys.argv) > 2 else False

DIRECTION = {"UP": 0, "DOWN": 1, "LEFT": 2, "RIGHT": 3}
BTN = {"UP": "up", "DOWN": "down", "LEFT": "left", "RIGHT": "right"}

# (label, start_scene, stage_x, stage_y, facing, step_btn, arrive_scene).
# Scene ids are registry ids (MapId): wake_grove 19, field 0, town 1,
# forest 2, grassy_forest 16.  Staging cells sit one tile short of a
# walkable whole-edge gate cell (grove east [8,9], field east [8,9,10],
# town north [9,10], forest north [11,12,13,14]).
LEGS = [
    ("grove->field", 19, 18, 8, "RIGHT", "right", 0),
    ("field->town", 0, 22, 9, "RIGHT", "right", 1),
    ("town->forest", 1, 9, 1, "UP", "up", 2),
    ("forest->grassy", 2, 12, 1, "UP", "up", 16),
]

SETTLE = 12
POLL_BUDGET = 1200


def oam_player_y(sess):
    return sess.pb.memory[0xFE00]


def shadow_player_y(sess):
    return sess.pb.memory[0xC000]


def vram_blank_rows(sess):
    """How many of the 20x18 visible BG cells read as blank space right
    now (wipe turns cells to tile 0 == ' '; reveal fills '?' terrain /
    font glyphs).  Tracks the wipe/reveal sweep in real time."""
    n = 0
    scx = sess.pb.memory[0xFF43]
    scy = sess.pb.memory[0xFF42]
    for r in range(18):
        for c in range(20):
            t = sess.pb.memory[0x9800 + ((scy // 8 + r) % 32) * 32
                               + (scx // 8 + c) % 32]
            if t == 0:
                n += 1
    return n


def scene_id(sess):
    return sess.reader.scene_state()["scene_id"]


def settled(sess, frames=10):
    p = sess.pos()
    for _ in range(frames):
        sess.tick()
        if sess.pos() != p:
            return False
    return True


def run_leg(sess, label, start_scene, sx, sy, facing, btn, arrive_scene,
            trace=False):
    """Returns ((walk_ticks, trans_ticks), None) or (None, reason)."""
    if scene_id(sess) != start_scene:
        return None, "not in start scene (in %s)" % scene_id(sess)
    if not settled(sess):
        return None, "never settled before staging"
    base = sess.reader.world + 5
    sess.pb.memory[base] = sx
    sess.pb.memory[base + 1] = sy
    sess.pb.memory[base + 5] = DIRECTION[facing]
    sess.tick(SETTLE)
    if sess.pos() != (sx, sy):
        return None, "staging did not stick (got %s)" % (sess.pos(),)
    if sess.text_has("DECK:"):
        return None, "ambushed while staging"
    f_press = sess.pb.frame_count
    sess.pb.button_press(BTN[facing])
    f_flip = None
    f_hidden = None
    f_back = None
    trace2 = TRACE and label == "grove->field"
    for i in range(POLL_BUDGET):
        sess.tick()
        if trace2 and i % 2 == 0:
            print("  t+%d pos=%s scene=%s shy=%s oam=%s blank=%d/%d"
                  % (sess.pb.frame_count - f_press, sess.pos(),
                     scene_id(sess), shadow_player_y(sess),
                     oam_player_y(sess),
                     vram_blank_rows(sess), 20 * 18))
        if sess.text_has("DECK:"):
            sess.pb.button_release(BTN[facing])
            return None, "ambushed on the gate walk"
        if f_flip is None and scene_id(sess) != start_scene:
            f_flip = sess.pb.frame_count
        if f_flip is not None and f_hidden is None \
                and shadow_player_y(sess) == 0:
            f_hidden = sess.pb.frame_count
        if f_hidden is not None and shadow_player_y(sess) != 0:
            f_back = sess.pb.frame_count
            break
    sess.pb.button_release(BTN[facing])
    if f_flip is None:
        return None, "scene never flipped (dropped press?)"
    if f_hidden is None:
        return None, "sprites never hid after the flip"
    if f_back is None:
        return None, "sprites never reappeared within %d ticks" % POLL_BUDGET
    if scene_id(sess) != arrive_scene:
        return None, "landed in scene %s, want %s" % (scene_id(sess),
                                                     arrive_scene)
    frames = f_back - f_flip
    return ((f_flip - f_press, frames), None)


def main():
    checks = []
    sess = Session(checks, "probe", rom=ROM)
    results = []
    try:
        for leg in LEGS:
            label = leg[0]
            res, err = run_leg(sess, *leg, trace=TRACE)
            if err is not None:
                print("%-14s ABORT: %s" % (label, err))
                return 1
            walk_ticks, trans_ticks = res
            print("%-14s walk=%-4d sprites-visible-after=%d frames"
                  % (label, walk_ticks, trans_ticks))
            results.append(trans_ticks)
    finally:
        sess.close()
    print("SUMMARY transition_frames=%s" % (results,))
    return 0


if __name__ == "__main__":
    sys.exit(main())
