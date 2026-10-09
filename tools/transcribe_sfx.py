#!/usr/bin/env python3
"""Transcribe tracker SFX (.uge via uge2source) into synth step tables.

The six assets/sfx/*.uge jingles are hUGESong_t data: too small to justify
song-takeover playback (which would interrupt BGM), but fully describable
as register-write timelines for the existing CH2-tone / CH4-noise synth
voices in src/audio/audio.c.

Method: emulate the hUGEDriver subset these songs actually use, at
dosound-tick resolution, recording every audible hardware register write:
- pattern note rows (attacks with trigger) and timbre-only rows;
- instrument loads (duty regs verbatim; sweep baked into the freq
  trajectory; noise envelope/len/step-width/GO decoded per the driver);
- subpattern tables: jumps, transpose offsets (incl. legato, untriggered
  CH4 pitch holds), portamento up/down;
- arpeggio 0xy cycling on pattern rows.
Score CH1 renders to the synth CH2 voice (register-compatible; sweep has
no CH2 equivalent, hence baked trajectories); score CH4 renders verbatim.
Score CH2/CH3 must be empty. Anything outside the observed subset (other
effects, wave instruments, routines, tempo changes, notes >= 72 on tone
channels, infinite envelopes) is a loud error, never a guess.

Driver references (lib/hUGEDriver/src/hUGEDriver.asm): note path ~1440,
do_table ~570, do_effect ~648, arp ~1065, CH4 load ~1644, tick_time ~1805.

Output: generated/sfx/sfx_tables.c (#pragma bank 7): per SFX one tone
array and one noise array of (tick, register image) steps at 64 Hz
dosound-tick resolution, plus lengths. Durations are the scored ones
(envelope decay to silence, verbatim).

Usage:
    python3 tools/transcribe_sfx.py --out generated/sfx/sfx_tables.c
    python3 tools/transcribe_sfx.py --out generated/sfx/sfx_tables.c assets/sfx/*.uge

The .uge file for each SFX id comes from screens/sfx.json (edited by the
level editor's Sound view).  With no positional files the whole registry is
transcribed; positional files are a partial/preview run.
"""

import sys
import os
import json
import re
import subprocess
import tempfile
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent  # tools/transcribe_sfx.py -> repo root
sys.path.insert(0, str(SCRIPT_DIR))

LAST_NOTE = 90
NO_NOTE = 90
TICKS_MAX_DOSOUND = 512  # uint8 table cursor is dosound ticks; sanity cap
# Tables live in bank 7 with the stepper (bank 6 holds driver + songs and
# is full); only the 7 voice-presence bytes live in the fixed bank.
# Includes the SfxEntry row per SFX (~7B).
SFX_BANK_BUDGET = 3072

# SFX id order must match the SFX_* enum in src/audio/audio.h
SFX_IDS = ["CURSOR", "CONFIRM", "SELECT", "BACK", "ATTACK", "HIT", "BLOCK"]
# Which .uge voices each SFX id is DATA, not code: screens/sfx.json maps
# each fixed SFX id to a .uge path (assets/sfx/* or assets/music/*).  The
# editor's Sound view edits that registry; this tool reads it so the two
# always agree.  Multiple ids may share one file (SELECT reuses CONFIRM).
SFX_REGISTRY = REPO_ROOT / "screens" / "sfx.json"


def load_sfx_sources():
    """Read screens/sfx.json -> ({basename: [SFX ids]}, [uge paths]).
    Fails loudly on unknown/missing ids or files (never guesses)."""
    try:
        data = json.loads(SFX_REGISTRY.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise TranscribeError(f"cannot read {SFX_REGISTRY}: {exc}")
    if not isinstance(data, dict):
        raise TranscribeError(f"{SFX_REGISTRY}: expected an object")
    unknown = sorted(set(data) - set(SFX_IDS))
    missing = sorted(set(SFX_IDS) - set(data))
    if unknown or missing:
        raise TranscribeError(
            f"{SFX_REGISTRY}: ids must be exactly {SFX_IDS} "
            f"(unknown={unknown}, missing={missing})")
    sources = {}
    paths = []
    for sfx in SFX_IDS:
        rel = data[sfx]
        if not isinstance(rel, str) or not rel:
            raise TranscribeError(f"{SFX_REGISTRY}: {sfx} must name a .uge")
        p = REPO_ROOT / rel
        if not p.is_file():
            raise TranscribeError(f"{SFX_REGISTRY}: {sfx} -> missing file {rel}")
        if p.suffix != ".uge":
            raise TranscribeError(f"{SFX_REGISTRY}: {sfx} -> {rel} is not .uge")
        sources.setdefault(p.name, []).append(sfx)
        paths.append(str(p))
    return sources, paths



class TranscribeError(Exception):
    pass


# ── Parse the converted C ─────────────────────────────────────────────

def parse_note_defines():
    out = {}
    text = (REPO_ROOT / "lib" / "hUGEDriver" / "include" / "hUGEDriver.h").read_text()
    for m in re.finditer(r"#define\s+([A-Za-z_][A-Za-z0-9_]*)\s+(\d+)", text):
        out[m.group(1)] = int(m.group(2))
    if out.get("___") != 90 or out.get("C_3") != 0:
        raise TranscribeError("note defines changed shape")
    return out


def parse_note_table():
    cands = list((REPO_ROOT / "lib" / "hUGEDriver").rglob("hUGE_note_table.inc"))
    if not cands:
        raise TranscribeError("hUGE_note_table.inc not found")
    periods = [int(x) for x in re.findall(r"dw\s+(\d+)", cands[0].read_text())]
    if len(periods) < 72:
        raise TranscribeError("note table too short")
    return periods


def parse_song_c(path, note_names):
    """Parse a uge2source C file. Rows are (note, instr, fx, param) where
    instr = DN-B & 0xF, fx = (DN-C >> 8) & 0xF, param = DN-C & 0xFF."""
    text = Path(path).read_text()
    out = {"tempo": None, "orders": [], "patterns": {}, "duty": [],
           "noise": [], "routines": None, "symbol": None, "order_cnt": 0}

    m = re.search(r"const hUGESong_t (\w+) = \{([^}]*)\};", text)
    if not m:
        raise TranscribeError(f"{path}: no song struct")
    out["symbol"] = m.group(1)
    head = [x.strip() for x in m.group(2).split(",")]
    out["tempo"] = int(head[0], 0)
    out["orders"] = [h.strip() for h in head[2:6]]
    out["routines"] = head[9].strip()
    if out["routines"] != "NULL":
        raise TranscribeError(f"{path}: song routines unsupported")
    if not (1 <= out["tempo"] <= 32):
        raise TranscribeError(f"{path}: insane tempo {out['tempo']}")

    m = re.search(r"static const unsigned char order_cnt = (\d+);", text)
    out["order_cnt"] = int(m.group(1)) if m else 0

    order_lists = {}
    for m in re.finditer(r"static const unsigned char\* const (order\d)\[\] = \{(.*?)\};",
                         text):
        order_lists[m.group(1)] = [x.strip() for x in m.group(2).split(",")
                                   if x.strip()]
    out["order_lists"] = order_lists

    def dn_row(line):
        mm = re.fullmatch(r"DN\(([^,]+),([^,]+),([^)]+)\)", line.strip())
        if not mm:
            raise TranscribeError(f"{path}: bad DN row: {line!r}")
        note_s, b_s, c_s = (x.strip() for x in mm.groups())
        b, c = int(b_s, 0), int(c_s, 0)
        if b >= 16:
            raise TranscribeError(f"{path}: DN B>=16 (note-steal path) unsupported")
        note = note_names[note_s] if note_s in note_names else int(note_s, 0)
        return (note, (b >> 0) & 0xF, (c >> 8) & 0xF, c & 0xFF)

    for m in re.finditer(
            r"static const unsigned char (P\d+|it\w+)\[\] = \{(.*?)\};",
            text, re.DOTALL):
        rows = []
        for line in m.group(2).strip().splitlines():
            line = line.strip().rstrip(",")
            if line:
                rows.append(dn_row(line))
        out["patterns"][m.group(1)] = rows

    for kind, size in (("duty_instruments", 5), ("noise_instruments", 5),
                       ("wave_instruments", 5)):
        items = []
        for m in re.finditer(r"static const hUGE\w+_t " + kind + r"(\[\d*\])? = \{(.*?)\};",
                             text, re.DOTALL):
            for it in re.finditer(r"\{([^{}]*)\}", m.group(2)):
                vals = []
                for tok in it.group(1).split(","):
                    tok = tok.strip()
                    if not tok:
                        continue
                    if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", tok):
                        vals.append(tok)
                    else:
                        vals.append(int(tok, 0))
                if len(vals) != size and not (kind == "wave_instruments"):
                    raise TranscribeError(f"{path}: {kind} entry size {len(vals)}")
                items.append(vals)
        out["duty" if kind.startswith("duty") else
            "noise" if kind.startswith("noise") else "wave"] = items
    return out


def note_poly(note):
    """hUGE get_note_poly: note id -> NR43 poly bits (RichardULZ formula)."""
    a = (63 - note) & 0xFF
    if a < 7:
        return a
    b = (a - 4) // 4
    c = (a % 4) + 4
    return (c | (b << 4)) & 0xFF


# ── Emulation ─────────────────────────────────────────────────────────

class Voice:
    def __init__(self, is_noise):
        self.is_noise = is_noise
        self.period = 0
        self.base_note = 0
        self.regs = {}
        self.table = None
        self.table_row = 0
        self.highmask = 0
        self.sweep = 0
        self.sweep_tick = 0
        self.step_width = 0
        self.go = 0x80
        self.events = []
        # length-timer state (NRx1 writes reload it; runs at 256 Hz)
        self.len_n = 0
        self.len_enable = False
        self.len_t0_isr = 0

    def emit(self, tick, regs, force=False):
        changed = force or any(self.regs.get(k) != v for k, v in regs.items())
        self.regs.update(regs)
        if changed:
            self.events.append((tick, dict(regs)))

    def env_at(self, tick):
        """Current (vol, pace, dir, t0): last NR22/NR42 write at or before tick."""
        key = "NR22" if not self.is_noise else "NR42"
        vol = pace = direction = t0 = 0
        for t, regs in self.events:
            if t > tick:
                break
            if key in regs:
                b = regs[key]
                vol, pace = (b >> 4) & 0x0F, b & 0x07
                direction, t0 = (b >> 3) & 0x01, t
        return vol, pace, direction, t0

    def vol_at(self, tick):
        vol, pace, direction, t0 = self.env_at(tick)
        if direction == 1:
            audible_vol = 15  # increasing: trim refuses these anyway
        elif pace == 0:
            audible_vol = vol
        else:
            audible_vol = max(0, vol - (tick - t0) // pace)
        if audible_vol == 0:
            return 0
        # length timer gates audibility (256 Hz units vs dosound ticks)
        if self.len_enable and self.len_t0_isr + (64 - self.len_n) * 1 <= tick * 4:
            return 0
        return audible_vol

    def note_len(self, len_val, tick):
        """Record an NRx1 length-counter reload (length enable untouched:
        it lives in NR24/NR44, rewritten only by attacks and arp)."""
        self.len_n = len_val & 0x3F
        self.len_t0_isr = tick * 4


class Emu:
    def __init__(self, parsed, periods):
        self.p = parsed
        self.periods = periods
        self.tempo = parsed["tempo"]
        self.counter = 0
        self.tone = Voice(False)
        self.noise = Voice(True)
        for ch in (1, 2):
            for note, instr, fx, param in parsed["patterns"].get(
                    parsed["orders"][ch], []):
                if note < LAST_NOTE or instr != 0 or fx != 0 or param != 0:
                    raise TranscribeError("score uses CH2/CH3 (music channels)")

    def period_of(self, note):
        if note < 0 or note >= 72:
            raise TranscribeError(f"tone note {note} out of range")
        return self.periods[note]

    # -- instrument loads (note attacks and timbre rows) --
    def load_duty(self, tick, idx):
        v = self.tone
        if idx == 0:
            return
        if idx - 1 >= len(self.p["duty"]):
            raise TranscribeError(f"duty instrument {idx} out of range")
        sweep, lenduty, env, sub, high = self.p["duty"][idx - 1]
        # NOTE: the driver's `res 7,a / jr z,.write_mask` looks like a
        # conditional load skip, but ZF was cleared two instructions
        # earlier by setup_instrument_pointer's `rla` ("reset the Z flag"),
        # so the branch is dead and every attack fully loads. Emulate the
        # observable behavior (always load); a driver change here would
        # shift every timeline below and fail review loudly.
        v.sweep = sweep
        v.sweep_tick = 0
        v.table = sub if isinstance(sub, str) else None
        v.table_row = 0
        v.highmask = high
        v.note_len(lenduty, tick)
        v.emit(tick, {"NR21": lenduty, "NR22": env})

    def load_noise(self, tick, idx):
        v = self.noise
        if idx == 0:
            return
        if idx - 1 >= len(self.p["noise"]):
            raise TranscribeError(f"noise instrument {idx} out of range")
        env, sub, b3 = self.p["noise"][idx - 1][:3]
        v.table = sub if isinstance(sub, str) else None
        v.table_row = 0
        v.step_width = 0x08 if (b3 & 0x80) else 0x00
        v.go = 0x80 | (b3 & 0x40)
        v.note_len(b3, tick)
        v.emit(tick, {"NR41": b3 & 0x3F, "NR42": env})

    def attack_tone(self, tick, note):
        v = self.tone
        v.period = self.period_of(note)
        v.base_note = note
        trig = v.highmask & 0x80
        v.len_enable = bool(v.highmask & 0x40)
        v.emit(tick, {"FREQ_LO": v.period & 0xFF,
                      "FREQ_HI": ((v.period >> 8) & 0x07) | trig,
                      "_trig": True}, force=True)

    def attack_noise(self, tick, note):
        v = self.noise
        v.base_note = note
        v.len_enable = bool(v.go & 0x40)
        v.emit(tick, {"NR43": note_poly(note) | v.step_width,
                      "NR44": v.go, "_trig": True}, force=True)

    def arp_offset(self, param):
        # Driver: counter is decremented before the mod-3 reduction, so
        # tick 0 and tick 1 share index 0 (sequence +lo,+lo,+hi,+0,...).
        # Counter starts at 0 on song init (zeroed RAM).
        idx = 0 if self.counter == 0 else (self.counter - 1) % 3
        return [param & 0x0F, (param >> 4) & 0x0F, 0][idx]

    def apply_arp(self, tick, param, is_noise=False):
        # Arp reaches update_channel_freq with h=0, so NR24/NR44 land
        # WITHOUT length-enable: arp clears the length timer (driver-verbatim).
        if is_noise:
            # Driver arp tail on CH4: tone period of base+offset is computed,
            # but only its LOW byte feeds the poly formula, and GO is written
            # with h=0 (no trigger, length disabled): a legato pitch wobble.
            v = self.noise
            new_note = v.base_note + self.arp_offset(param)
            if new_note >= 72:
                raise TranscribeError("arp out of range")
            lo = self.period_of(new_note) & 0xFF
            sw = v.step_width
            v.len_enable = False
            v.emit(tick, {"NR43": note_poly(lo) | sw, "NR44": 0x00})
            return
        v = self.tone
        new_note = v.base_note + self.arp_offset(param)
        v.period = self.period_of(new_note)
        v.len_enable = False
        v.emit(tick, {"FREQ_LO": v.period & 0xFF,
                      "FREQ_HI": ((v.period >> 8) & 0x07)})

    def porta(self, tick, voice, delta):
        voice.period = (voice.period + delta) & 0xFFFF
        if voice.is_noise:
            return
        voice.emit(tick, {"FREQ_LO": voice.period & 0xFF,
                          "FREQ_HI": ((voice.period >> 8) & 0x07)})

    def sweep_step(self, tick):
        v = self.tone
        pace = (v.sweep >> 4) & 0x07
        if pace == 0:
            return
        shift = v.sweep & 0x07
        direction = -1 if (v.sweep & 0x08) else 1
        prev = (v.sweep_tick * 2) // pace
        v.sweep_tick += 1
        cur = (v.sweep_tick * 2) // pace
        for _ in range(cur - prev):
            v.period = (v.period + direction * (v.period >> shift)) & 0xFFFF
        if cur != prev:
            v.emit(tick, {"FREQ_LO": v.period & 0xFF,
                          "FREQ_HI": ((v.period >> 8) & 0x07)})

    def table_step(self, tick, voice, tables, is_noise):
        if voice.table is None:
            return
        rows = self.p["patterns"][voice.table]
        row = rows[voice.table_row % len(rows)]
        voice.table_row += 1
        note, jump_nib, fx, param = row
        if note >= 128:
            raise TranscribeError("table note-steal path unsupported")
        if jump_nib:
            voice.table_row = jump_nib - 1
        if note < NO_NOTE:
            base = voice.base_note
            new_note = (base + (note - 36)) & 0xFF
            if is_noise:
                poly = note_poly(new_note)
                sw = voice.step_width
                voice.emit(tick, {"NR43": poly | sw,
                                  "NR44": voice.go & 0x7F})  # legato: no trigger
            else:
                if new_note >= 72:
                    raise TranscribeError(f"transpose out of range: {base}+{note - 36}")
                voice.period = self.period_of(new_note)
                voice.emit(tick, {"FREQ_LO": voice.period & 0xFF,
                                  "FREQ_HI": ((voice.period >> 8) & 0x07)})
        if fx == 1:
            self.porta(tick, voice, param)
        elif fx == 2:
            self.porta(tick, voice, -param)
        elif fx != 0:
            raise TranscribeError(f"table effect {fx} unsupported")

    def chan_rows(self, ch, order_idx):
        pname = self.p["order_lists"][self.p["orders"][ch]][order_idx]
        return self.p["patterns"][pname]

    def pattern_row(self, tick, ch, row, order_idx):
        """Tick-0-of-row path for one channel. Returns True if a note attacked."""
        voice = self.tone if ch == 0 else self.noise
        tables = self.p["duty"] if ch == 0 else self.p["noise"]
        rows = self.chan_rows(ch, order_idx)
        if row >= len(rows):
            raise TranscribeError(f"row {row} past pattern end")
        note, instr, fx, param = rows[row]
        attacked = False
        # Driver order on a note tick: instrument load, pattern effect
        # (arp), then play/trigger. Play runs last, so its length-enable
        # bit wins over an arp-cleared one.
        arp = fx == 0 and param != 0
        if fx != 0:
            raise TranscribeError(f"pattern effect {fx} unsupported")
        if note < LAST_NOTE:
            if instr == 0:
                raise TranscribeError("note attack with no instrument")
            # Base note latches before the arp runs (driver stores it in
            # channel_note during get_current_note, ahead of do_effect).
            (self.tone if ch == 0 else self.noise).base_note = note
            if ch == 0:
                self.load_duty(tick, instr)
            else:
                self.load_noise(tick, instr)
            if arp:
                self.apply_arp(tick, param, is_noise=(ch != 0))
            if ch == 0:
                self.attack_tone(tick, note)
            else:
                self.attack_noise(tick, note)
            attacked = True
        elif instr:
            if ch == 0:
                self.load_duty(tick, instr)
            else:
                self.load_noise(tick, instr)
        self.table_step(tick, voice, tables, ch == 3)
        if ch == 0:
            self.sweep_step(tick)
        return attacked

    def pattern_tick(self, tick, ch, row, order_idx):
        """Non-row ticks: pattern arp continues, then table, then sweep."""
        voice = self.tone if ch == 0 else self.noise
        tables = self.p["duty"] if ch == 0 else self.p["noise"]
        rows = self.chan_rows(ch, order_idx)
        if row >= len(rows):
            return
        _, _, fx, param = rows[row]
        if fx == 0:
            if param != 0:
                self.apply_arp(tick, param, is_noise=(ch != 0))
        else:
            raise TranscribeError(f"pattern effect {fx} on inner tick unsupported")
        self.table_step(tick, voice, tables, ch == 3)
        if ch == 0:
            self.sweep_step(tick)

    def emulate(self):
        # Driver advances current_order by 2 per 64 rows and wraps at
        # order_cnt, so song_orders = order_cnt // 2. SFX are single-order
        # one-shots (order_cnt == 2, one pattern per channel order list);
        # anything else is a loud error.
        if self.p["order_cnt"] != 2:
            raise TranscribeError("multi-order songs unsupported (SFX only)")
        for ch in range(4):
            lst = self.p["order_lists"].get(self.p["orders"][ch], [])
            if len(lst) != 1:
                raise TranscribeError("multi-pattern orders unsupported (SFX only)")
        total = 64 * self.tempo
        for tick in range(total):
            row = (tick // self.tempo) % 64
            if tick % self.tempo == 0:
                self.pattern_row(tick, 0, row, 0)
                self.pattern_row(tick, 3, row, 0)
            else:
                self.pattern_tick(tick, 0, row, 0)
                self.pattern_tick(tick, 3, row, 0)
            self.counter += 1
        return self.tone.events, self.noise.events


# ── Trim + emit ───────────────────────────────────────────────────────

def trim_voice(voice):
    """End the table at the last audible tick plus the envelope's natural
    decay to silence (the engine cuts there, like today's sfx_ticks).
    Returns (audible_events, end_tick). Infinite envelopes are a loud
    error: a one-shot table cannot hold them."""
    events = voice.events
    audible = [(t, r) for (t, r) in events if voice.vol_at(t) > 0]
    if not audible:
        return [], 0
    last_tick, _ = audible[-1]
    vol, pace, direction, t0 = voice.env_at(last_tick)
    if direction == 1:
        raise TranscribeError("increasing envelope has no natural end")
    if vol == 0:
        tail = 0
    elif pace == 0:
        raise TranscribeError("constant-volume tail never ends; trim rule needed")
    else:
        # volume already decayed for (last_tick - t0) ticks since the write
        tail = max(0, vol * pace - (last_tick - t0))
    end = last_tick + tail
    # the hardware length timer cuts sound even mid-decay; the engine
    # must unmute there, not at the envelope's natural end
    if voice.len_enable:
        # ceil: never cut audible sound early; extra ducking is harmless
        length_end = (voice.len_t0_isr + (64 - voice.len_n) + 3) // 4
        end = min(end, length_end)
    # Engine end-check is `sfx_tick > total` on a uint8 that wraps at 255,
    # so totals must stay strictly below 255 (wrap would replay forever).
    if end > 254:
        raise TranscribeError("SFX longer than 254 dosound ticks")
    return audible, end


def merge_ticks(events, keys):
    """Merge same-tick writes into delta steps: a tick is emitted only if
    it forces (note attack: full image, matching the driver's rewrites
    incl. length reload) or changes a register vs the running image.
    Deltas matter: rewriting NR21/NR41 would spuriously reload the
    hardware length timer, so unchanged regs are omitted."""
    steps = []
    image = {}
    cur_tick = None
    cur = {}

    def flush():
        if cur_tick is None:
            return
        force = cur.pop("_trig", False)
        trig_hi = cur.pop("_trig_hi", False)
        trig44 = cur.pop("_trig44", False)
        mask = 0
        vals = []
        for i, k in enumerate(keys):
            if k in cur and (force or image.get(k) != cur[k]):
                mask |= 1 << i
                vals.append(cur[k])
            else:
                vals.append(0)
        if trig_hi and "FREQ_HI" in keys:
            vals[keys.index("FREQ_HI")] |= 0x80
            mask |= 1 << keys.index("FREQ_HI")
        if trig44 and "NR44" in keys:
            vals[keys.index("NR44")] |= 0x80
            mask |= 1 << keys.index("NR44")
        if mask:
            steps.append((cur_tick, mask) + tuple(vals))
        image.update({k: v for k, v in cur.items()})

    for tick, regs in events:
        if tick != cur_tick:
            flush()
            cur_tick, cur = tick, {}
        if "FREQ_HI" in regs and regs["FREQ_HI"] & 0x80:
            cur["_trig_hi"] = True
        if "NR44" in regs and regs["NR44"] & 0x80:
            cur["_trig44"] = True
        cur.update(regs)
    flush()
    return steps


def build_tables(name, tone_voice, noise_voice):
    tone_ev, tone_end = trim_voice(tone_voice)
    noise_ev, noise_end = trim_voice(noise_voice)
    tone_steps = merge_ticks(tone_ev, ("NR21", "NR22", "FREQ_LO", "FREQ_HI"))
    noise_steps = merge_ticks(noise_ev, ("NR41", "NR42", "NR43", "NR44"))
    return tone_steps, noise_steps, max(tone_end, noise_end)


def emit_c(tables):
    """tables: {SFX_ID: (tone_steps, noise_steps, end_tick)}."""
    out = []
    out.append("/* Generated by tools/transcribe_sfx.py -- DO NOT EDIT DIRECTLY */")
    out.append("/*")
    out.append(" * Tracker SFX transcribed to synth step tables (64 Hz dosound")
    out.append(" * ticks). Score CH1 renders to the CH2 voice (sweep baked into")
    out.append(" * the freq trajectory: no CH2 sweep register exists); score CH4")
    out.append(" * renders verbatim. SFX_SELECT shares the accept tables.")
    out.append(" */")
    out.append("#pragma bank 7")
    out.append("")
    out.append("#include <stdint.h>")
    out.append("")
    out.append("/* Per-voice typed step arrays plus the SfxEntry table, all")
    out.append(" * bank 7: the banked stepper (src/audio/sfx_step.c) reads them")
    out.append(" * in-bank, so fixed code never pays generic-pointer or")
    out.append(" * struct-multiply costs (AGENTS.md 52.18). Mask bits select")
    out.append(" * which regs the stepper writes (bit order = field order);")
    out.append(" * unchanged regs are omitted so the hardware length timer is")
    out.append(" * never spuriously reloaded. freq_hi carries the scored")
    out.append(" * trigger bit; nr44 carries the scored GO byte. */")
    out.append("#include \"sfx_tables.h\"")
    out.append("")
    for sfx in SFX_IDS:
        tone, noise, end = tables[sfx]
        out.append(f"/* {sfx}: {len(tone)} tone steps, {len(noise)} noise steps,"
                   f" {end} dosound ticks */")
        out.append(f"const SfxToneStep s_sfx_{sfx.lower()}_tone[] = {{")
        if tone:
            for tick, mask, nr21, nr22, lo, hi in tone:
                out.append(f"    {{{tick}, 0x{mask:02X}, 0x{nr21:02X}, 0x{nr22:02X},"
                           f" 0x{lo:02X}, 0x{hi:02X}}},")
        else:
            out.append("    {0, 0x00, 0x00, 0x00, 0x00, 0x00},")
        out.append("};")
        out.append(f"const SfxNoiseStep s_sfx_{sfx.lower()}_noise[] = {{")
        if noise:
            for tick, mask, nr41, nr42, nr43, nr44 in noise:
                out.append(f"    {{{tick}, 0x{mask:02X}, 0x{nr41:02X}, 0x{nr42:02X},"
                           f" 0x{nr43:02X}, 0x{nr44:02X}}},")
        else:
            out.append("    {0, 0x00, 0x00, 0x00, 0x00, 0x00},")
        out.append("};")
    out.append("const SfxEntry s_sfx_index[] = {")
    for sfx in SFX_IDS:
        tone, noise, end = tables[sfx]
        out.append(f"    {{ s_sfx_{sfx.lower()}_tone, {len(tone)},"
                   f" s_sfx_{sfx.lower()}_noise, {len(noise)}, {end} }},"
                   f" /* {sfx} */")
    out.append("};")
    out.append("")
    return "\n".join(out) + "\n"


def emit_index(tables):
    """Fixed-bank voice-presence bytes only (7 bytes): the trigger mutes
    exactly the voices each SFX uses, with plain byte reads. Everything
    else lives in bank 7 with the stepper."""
    out = []
    out.append("/* Generated by tools/transcribe_sfx.py -- DO NOT EDIT DIRECTLY */")
    out.append("#include <stdint.h>")
    out.append("")
    out.append("/* bit0 = tone voice used, bit1 = noise voice used */")
    out.append("const uint8_t s_sfx_voices[] = {"
               + ", ".join(str((1 if tables[s][0] else 0)
                               | (2 if tables[s][1] else 0))
                           for s in SFX_IDS) + "};")
    out.append("")
    return "\n".join(out) + "\n"


def emit_h():
    out = []
    out.append("/* Generated by tools/transcribe_sfx.py -- DO NOT EDIT DIRECTLY */")
    out.append("#ifndef SFX_TABLES_H")
    out.append("#define SFX_TABLES_H")
    out.append("")
    out.append("#include <stdint.h>")
    out.append("")
    out.append(f"#define SFX_TABLE_COUNT {len(SFX_IDS)}")
    out.append("typedef struct { uint8_t tick, mask, nr21, nr22, freq_lo, freq_hi; } SfxToneStep;")
    out.append("typedef struct { uint8_t tick, mask, nr41, nr42, nr43, nr44; } SfxNoiseStep;")
    out.append("typedef struct {")
    out.append("    const SfxToneStep *tone;")
    out.append("    uint8_t tone_len;")
    out.append("    const SfxNoiseStep *noise;")
    out.append("    uint8_t noise_len;")
    out.append("    uint8_t total_ticks;")
    out.append("} SfxEntry;")
    out.append("extern const SfxEntry s_sfx_index[];")
    out.append("extern const uint8_t s_sfx_voices[];")
    out.append("")
    out.append("#endif /* SFX_TABLES_H */")
    out.append("")
    return "\n".join(out) + "\n"


def convert_uge(path, tmpdir):
    """Run the repo's music pipeline converter into scratch."""
    out = str(Path(tmpdir) / (Path(path).stem + ".c"))
    subprocess.run([sys.executable, str(REPO_ROOT / "tools" / "compile_music.py"),
                    str(path), "6", "song_tmp", out], check=True)
    return out


def main(argv):
    out_path = None
    uges = []
    args = list(argv)
    while args:
        a = args.pop(0)
        if a == "--out":
            out_path = Path(args.pop(0))
        elif a.startswith("-"):
            raise TranscribeError(f"unknown flag {a}")
        else:
            uges.append(a)
    sfx_sources, registry_paths = load_sfx_sources()
    if not uges:
        uges = registry_paths
    if not uges:
        raise TranscribeError("no .uge sources (check screens/sfx.json)")
    note_names = parse_note_defines()
    periods = parse_note_table()
    # The table order must track the SFX_* enum in src/audio/audio.h exactly.
    enum_ids = re.findall(r"SFX_([A-Z]+)\s*=",
                          (REPO_ROOT / "src" / "audio" / "audio.h").read_text())
    if enum_ids != SFX_IDS:
        raise TranscribeError(
            f"SFX id drift: audio.h has {enum_ids}, tool has {SFX_IDS}")
    songs = {}
    with tempfile.TemporaryDirectory(prefix="sfx_tr_") as tmp:
        for uge in uges:
            c_path = convert_uge(uge, tmp)
            parsed = parse_song_c(c_path, note_names)
            songs[Path(uge).name] = parsed
    tables = {}
    total_bytes = 0
    wanted = {Path(u).name for u in uges}
    for fname, ids in sfx_sources.items():
        if fname not in songs:
            if fname in wanted:
                raise TranscribeError(f"missing .uge for {ids}: {fname}")
            continue  # partial run: skip unprovided sources
        emu = Emu(songs[fname], periods)
        emu.emulate()
        tone_steps, noise_steps, end = build_tables(fname, emu.tone, emu.noise)
        total_bytes += len(tone_steps) * 6 + len(noise_steps) * 6 + 7
        for sfx in ids:
            tables[sfx] = (tone_steps, noise_steps, end)
        print(f"== {fname} -> {', '.join(ids)}: "
              f"{len(tone_steps)} tone + {len(noise_steps)} noise steps, "
              f"{end} ticks ({end / 64:.2f}s)")
        # display shows the running register image (what the engine holds),
        # with masked-out (unchanged) regs in lowercase
        image = {}
        for tick, mask, nr21, nr22, lo, hi in tone_steps:
            vals = (nr21, nr22, lo, hi)
            names = ("NR21", "NR22", "FREQ_LO", "FREQ_HI")
            shown = []
            for i, (k, v) in enumerate(zip(names, vals)):
                if mask & (1 << i):
                    image[k] = v
                    shown.append(f"{k}={v:02X}")
                else:
                    shown.append(f"{k.lower()}={image.get(k, 0):02X}")
            print(f"    tone t={tick:3d} m={mask:04b} " + " ".join(shown))
        image = {}
        for tick, mask, nr41, nr42, nr43, nr44 in noise_steps:
            vals = (nr41, nr42, nr43, nr44)
            names = ("NR41", "NR42", "NR43", "NR44")
            shown = []
            for i, (k, v) in enumerate(zip(names, vals)):
                if mask & (1 << i):
                    image[k] = v
                    shown.append(f"{k}={v:02X}")
                else:
                    shown.append(f"{k.lower()}={image.get(k, 0):02X}")
            print(f"    noise t={tick:3d} m={mask:04b} " + " ".join(shown))
    print(f"bank-7 table total: ~{total_bytes}B")
    if total_bytes > SFX_BANK_BUDGET:
        raise TranscribeError(f"table total {total_bytes}B over bank-7 budget")
    if out_path:
        if len(tables) < len(SFX_IDS):
            raise TranscribeError("partial run cannot emit complete tables")
        if sorted(tables) != sorted(SFX_IDS):
            raise TranscribeError("SFX id set drifted from src/audio/audio.h")
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(emit_c(tables))
        h_path = out_path.with_suffix(".h")
        h_path.write_text(emit_h())
        idx_path = out_path.parent / "sfx_index.c"
        idx_path.write_text(emit_index(tables))
        print(f"wrote {out_path} + {h_path.name} + {idx_path.name}")
    return 0


if __name__ == "__main__":
    try:
        main(sys.argv[1:])
    except TranscribeError as e:
        print(f"transcribe_sfx: error: {e}", file=sys.stderr)
        sys.exit(1)
