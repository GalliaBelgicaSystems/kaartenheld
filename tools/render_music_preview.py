#!/usr/bin/env python3
"""Render hUGEDriver songs to WAV previews for the level editor.

Reads the uge2source6325+generated C (already-parsed patterns, orders,
instruments) and emulates the driver's 64 Hz tick engine closely enough
for a recognizable preview: note attacks, instruments + subpatterns,
arpeggio, portamento, note cut, sweep, envelopes, and the length timer.
Anything outside the observed subset (other pattern effects, routines,
note-steal, CH-wave surprises) is a loud error, never a guess -- the ROM
remains the authoritative sound.

Usage:
    python3 tools/render_music_preview.py --all
    python3 tools/render_music_preview.py forest village
    python3 tools/render_music_preview.py --all --out some/dir/

Output: tools/level_editor/public/audio/<song>.wav (22050 Hz mono 16-bit,
exactly one song loop plus a short fade so the browser can loop it).
Deterministic: same song C in, byte-identical WAV out.
"""
import math
import os
import re
import struct
import sys
import wave
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent  # tools/render_music_preview.py -> repo root
GEN_MUSIC = REPO_ROOT / "generated" / "music"
OUT_DIR = (REPO_ROOT / "tools" / "level_editor" / "public" / "audio")

SONGS = ["battle", "desolate_landscape", "forest", "boss_fight",
         "village", "castle", "mimic", "mimic_intro"]

SR = 22050          # output sample rate
TICK_HZ = 64        # hUGE_dosound rate (256 Hz timer / 4)
LOOPS = 1           # song loops rendered (browser loops the file)
MAX_SECONDS = 90    # safety cap per file
FADE_MS = 120

LAST_NOTE = 72
NO_NOTE = 90


class RenderError(Exception):
    pass


# ── Parse ─────────────────────────────────────────────────────────────

def parse_note_defines():
    out = {}
    text = (REPO_ROOT / "lib" / "hUGEDriver" / "include" /
            "hUGEDriver.h").read_text()
    for m in re.finditer(r"#define\s+([A-Za-z_][A-Za-z0-9_]*)\s+(\d+)", text):
        out[m.group(1)] = int(m.group(2))
    if out.get("___") != 90 or out.get("C_3") != 0:
        raise RenderError("note defines changed shape")
    return out


def parse_note_table():
    cands = list((REPO_ROOT / "lib" / "hUGEDriver").rglob("hUGE_note_table.inc"))
    if not cands:
        raise RenderError("hUGE_note_table.inc not found")
    periods = [int(x) for x in re.findall(r"dw\s+(\d+)",
                                          cands[0].read_text())]
    if len(periods) < 72:
        raise RenderError("note table too short")
    return periods


def parse_song_c(path, note_names):
    """Rows are (note, instr, fx, param); instruments may carry it-table names."""
    text = Path(path).read_text()
    out = {"tempo": None, "orders": [], "patterns": {}, "duty": [],
           "wave": [], "noise": [], "waves": [], "order_cnt": 0}

    m = re.search(r"const hUGESong_t (\w+) = \{([^}]*)\};", text)
    if not m:
        raise RenderError(f"{path}: no song struct")
    out["symbol"] = m.group(1)
    head = [x.strip() for x in m.group(2).split(",")]
    out["tempo"] = int(head[0], 0)
    out["orders"] = [h.strip() for h in head[2:6]]
    if head[9].strip() != "NULL":
        raise RenderError(f"{path}: song routines unsupported")
    if not (1 <= out["tempo"] <= 32):
        raise RenderError(f"{path}: insane tempo {out['tempo']}")

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
            raise RenderError(f"{path}: bad DN row: {line!r}")
        note_s, b_s, c_s = (x.strip() for x in mm.groups())
        b, c = int(b_s, 0), int(c_s, 0)
        if b >= 16:
            raise RenderError(f"{path}: DN B>=16 (note-steal) unsupported")
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

    for kind, want in (("duty", "duty"), ("noise", "noise"), ("wave", "wave")):
        items = []
        for m in re.finditer(r"static const hUGE\w+_t \S*" + kind + r"\w*(\[\d*\])? = \{(.*?)\};",
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
                items.append(vals)
        out[want] = items

    m = re.search(r"static const unsigned char waves(\[\d*\])? = \{(.*?)\};",
                  text, re.S)
    if not m:
        raise RenderError(f"{path}: no waves table")
    out["waves"] = [int(x) for x in re.findall(r"\d+", m.group(2))]
    # compile_music.py patches wave-less songs to a single-zero dummy
    # (waves[1] = {0}); their CH3 is silent, so no 16-byte slice is read.
    if out["waves"] != [0] and len(out["waves"]) % 16:
        raise RenderError(f"{path}: waves not a multiple of 16 bytes")
    return out


# ── GB hardware models ────────────────────────────────────────────────

def period_to_freq(period):
    if period >= 2048 or period < 0:
        raise RenderError(f"period {period} out of range")
    return 131072.0 / (2048 - period)


def noise_freq(nr43):
    s = (nr43 >> 4) & 0x0F
    r = nr43 & 0x07
    div = 0.5 if r == 0 else float(r)
    return 524288.0 / div / (2.0 ** (s + 1))


def note_poly(note):
    """hUGE get_note_poly: note id -> NR43 value (RichardULZ formula)."""
    a = (63 - note) & 0xFF
    if a < 7:
        return a
    b = (a - 4) // 4
    c = (a % 4) + 4
    return (c | (b << 4)) & 0xFF


DUTIES = (0.125, 0.25, 0.5, 0.75)
WAVE_VOLS = (0.0, 1.0, 0.5, 0.25)


# ── Emulation (one dosound tick = one frame of channel state) ─────────

class Chan:
    def __init__(self, kind):
        self.kind = kind  # 'duty' | 'wave' | 'noise'
        self.period = 0
        self.base_note = 0
        self.instr = 0
        # duty voice state
        self.sweep = 0
        self.sweep_tick = 0
        self.lenduty = 0
        self.env = 0
        # wave voice state
        self.wavelength = 0
        self.wavevol = 0
        self.waveform = 0
        # noise voice state
        self.step_width = 0
        self.go = 0
        self.nr43 = 0
        # shared
        self.table = None
        self.table_row = 0
        self.highmask = 0
        self.len_n = 0
        self.len_enable = False
        self.len_t0 = 0
        self.env_t0 = 0
        self.cut = False


class Emu:
    def __init__(self, parsed, periods):
        self.p = parsed
        self.periods = periods
        self.tempo = parsed["tempo"]
        self.counter = 0
        self.tick = 0
        self.chans = [Chan("duty"), Chan("duty"), Chan("wave"), Chan("noise")]
        # structural validation up front (loud errors, never guesses)
        if parsed["order_cnt"] % 2:
            raise RenderError("odd order_cnt")
        n_orders = parsed["order_cnt"] // 2
        for ch in range(4):
            lst = parsed["order_lists"].get(parsed["orders"][ch], [])
            if len(lst) != n_orders:
                raise RenderError(f"ch{ch}: order list len {len(lst)} != {n_orders}")
            for pname in lst:
                rows = parsed["patterns"].get(pname)
                if rows is None:
                    raise RenderError(f"ch{ch}: unknown pattern {pname}")
                if len(rows) != 64:
                    raise RenderError(f"ch{ch}: pattern {pname} len {len(rows)} != 64")
        for tables in (parsed["duty"], parsed["wave"], parsed["noise"]):
            for e in tables:
                for v in e:
                    if isinstance(v, str) and v not in parsed["patterns"]:
                        raise RenderError(f"dangling subpattern {v}")
        self.n_orders = n_orders
        self.frames = []  # per-tick snapshots

    def period_of(self, note):
        if note < 0 or note >= 72:
            raise RenderError(f"note {note} out of range")
        return self.periods[note]

    # -- instrument loads --
    def load_instr(self, tick, ch, idx):
        v = self.chans[ch]
        if idx == 0:
            return
        if ch < 2:
            tab = self.p["duty"]
            if idx - 1 >= len(tab):
                raise RenderError(f"duty instrument {idx} out of range")
            sweep, lenduty, env, sub, high = tab[idx - 1]
            v.sweep = sweep
            v.sweep_tick = 0
            v.lenduty = lenduty
            v.env = env
            v.env_t0 = tick
            v.highmask = high
            v.len_n = lenduty & 0x3F
        elif ch == 2:
            tab = self.p["wave"]
            if idx - 1 >= len(tab):
                raise RenderError(f"wave instrument {idx} out of range")
            length, volume, waveform, sub, high = tab[idx - 1]
            v.wavelength = length
            v.wavevol = volume
            v.waveform = waveform
            v.env_t0 = tick
            v.highmask = high
            v.len_n = length & 0xFF
        else:
            tab = self.p["noise"]
            if idx - 1 >= len(tab):
                raise RenderError(f"noise instrument {idx} out of range")
            env, sub, b3 = tab[idx - 1][:3]
            v.env = env
            v.env_t0 = tick
            v.step_width = 0x08 if (b3 & 0x80) else 0x00
            v.go = 0x80 | (b3 & 0x40)
            v.len_n = b3 & 0x3F
        v.table = sub if isinstance(sub, str) else None
        v.table_row = 0
        v.len_t0 = tick
        v.cut = False

    def attack(self, tick, ch, note):
        v = self.chans[ch]
        v.base_note = note
        if ch < 2:
            v.period = self.period_of(note)
        elif ch == 2:
            v.period = self.period_of(note)
            if v.waveform * 16 + 16 > len(self.p["waves"]):
                raise RenderError(f"waveform {v.waveform} out of range")
        else:
            v.nr43 = note_poly(note) | v.step_width
        v.len_enable = bool((v.go if ch == 3 else v.highmask) & 0x40)
        v.len_t0 = tick
        v.cut = False

    # -- per-tick effects --
    def arp_offset(self, param):
        idx = 0 if self.counter == 0 else (self.counter - 1) % 3
        return [param & 0x0F, (param >> 4) & 0x0F, 0][idx]

    def apply_arp(self, tick, ch, param):
        v = self.chans[ch]
        new_note = v.base_note + self.arp_offset(param)
        if ch == 3:
            if new_note >= 72:
                raise RenderError("arp out of range")
            v.nr43 = note_poly(self.period_of(new_note) & 0xFF) | v.step_width
            v.len_enable = False
            return
        v.period = self.period_of(new_note)
        v.len_enable = False

    def porta(self, ch, delta):
        v = self.chans[ch]
        if ch == 3:
            return
        v.period = (v.period + delta) & 0xFFFF

    def sweep_step(self, tick):
        v = self.chans[0]
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

    def table_step(self, ch):
        v = self.chans[ch]
        if v.table is None:
            return
        rows = self.p["patterns"][v.table]
        row = rows[v.table_row % len(rows)]
        v.table_row += 1
        note, jump_nib, fx, param = row
        if note >= 128:
            raise RenderError("table note-steal unsupported")
        if jump_nib:
            v.table_row = jump_nib - 1
        if note < NO_NOTE:
            new_note = (v.base_note + (note - 36)) & 0xFF
            if ch == 3:
                v.nr43 = note_poly(new_note) | v.step_width
            else:
                if new_note >= 72:
                    raise RenderError(f"transpose out of range: {new_note}")
                v.period = self.period_of(new_note)
        if fx == 1:
            self.porta(ch, param)
        elif fx == 2:
            self.porta(ch, -param)
        elif fx != 0:
            raise RenderError(f"table effect {fx} unsupported")

    # -- channel state -> audible output --
    def chan_out(self, ch):
        """(freq_hz, vol_0_15, duty_or_wave, nr43). vol 0 = silent."""
        v = self.chans[ch]
        if v.cut:
            return (0.0, 0, 0, 0)
        if ch < 2:
            vol = self.env_vol(v.env, v.env_t0)
            if vol == 0:
                return (0.0, 0, 0, 0)
            if v.len_enable and self.tick - v.len_t0 >= 64 - v.len_n:
                return (0.0, 0, 0, 0)
            if v.period == 0:
                return (0.0, 0, 0, 0)
            return (period_to_freq(v.period), vol,
                    DUTIES[(v.lenduty >> 6) & 0x03], 0)
        if ch == 2:
            code = (v.wavevol >> 5) & 0x03
            if code == 0 or v.period == 0:
                return (0.0, 0, 0, 0)
            if v.len_enable and self.tick - v.len_t0 >= 256 - v.len_n:
                return (0.0, 0, 0, 0)
            raw = self.p["waves"][v.waveform * 16:(v.waveform + 1) * 16]
            # 16 bytes = 32 nibbles, high nibble first
            nibbles = tuple(((b >> 4) & 0xF, b & 0xF) for b in raw)
            nibbles = tuple(n for pair in nibbles for n in pair)
            return (period_to_freq(v.period), code, nibbles, 0)
        vol = self.env_vol(v.env, v.env_t0)
        if vol == 0:
            return (0.0, 0, 0, 0)
        if v.len_enable and self.tick - v.len_t0 >= 64 - v.len_n:
            return (0.0, 0, 0, 0)
        return (noise_freq(v.nr43), vol, 0, v.nr43)

    def env_vol(self, env, t0):
        vol, pace, direction = (env >> 4) & 0x0F, env & 0x07, (env >> 3) & 0x01
        if pace == 0:
            return vol
        if direction == 1:
            return min(15, vol + (self.tick - t0) // pace)
        return max(0, vol - (self.tick - t0) // pace)

    # -- main loop --
    def run_tick(self):
        inner = self.tick % self.tempo
        row = (self.tick // self.tempo) % 64
        order_idx = (self.tick // (64 * self.tempo)) % self.n_orders
        for ch in range(4):
            pname = self.p["order_lists"][self.p["orders"][ch]][order_idx]
            note, instr, fx, param = self.p["patterns"][pname][row]
            v = self.chans[ch]
            if inner == 0:
                # row tick: attack path (porta skipped on tick 0, like the driver)
                if fx not in (0, 1, 2, 0xE):
                    raise RenderError(f"pattern effect {fx} unsupported")
                arp = fx == 0 and param != 0
                if note < LAST_NOTE:
                    if instr == 0:
                        raise RenderError("note attack with no instrument")
                    v.base_note = note
                    self.load_instr(self.tick, ch, instr)
                    if arp:
                        self.apply_arp(self.tick, ch, param)
                    self.attack(self.tick, ch, note)
                elif note == NO_NOTE:
                    if instr:
                        self.load_instr(self.tick, ch, instr)
                else:
                    raise RenderError(f"note value {note} unsupported")
                if fx == 0xE and param == 0:
                    v.cut = True
                self.table_step(ch)
                if ch == 0:
                    self.sweep_step(self.tick)
            else:
                # inner tick: continuing effect
                if fx == 0:
                    if param != 0:
                        self.apply_arp(self.tick, ch, param)
                elif fx == 1:
                    self.porta(ch, param)
                elif fx == 2:
                    self.porta(ch, -param)
                elif fx == 0xE:
                    if inner == param:
                        v.cut = True
                else:
                    raise RenderError(f"pattern effect {fx} on inner tick unsupported")
                self.table_step(ch)
                if ch == 0:
                    self.sweep_step(self.tick)
        self.frames.append(tuple(self.chan_out(ch) for ch in range(4)))
        self.counter += 1
        self.tick += 1

    def emulate(self, total_ticks):
        for _ in range(total_ticks):
            self.run_tick()
        return self.frames


# ── Synthesis ─────────────────────────────────────────────────────────

class Synth:
    def __init__(self):
        self.phase = [0.0, 0.0, 0.0, 0]
        self.lfsr = 0x7FFF
        self.lfsr_acc = 0.0

    def render_tick(self, frame):
        """One 1/64 s tick -> samples (list of floats)."""
        n = SR / TICK_HZ
        count = int(n)
        out = []
        frac = n - count
        self._frac = getattr(self, "_frac", 0.0) + frac
        if self._frac >= 1.0:
            count += 1
            self._frac -= 1.0
        for _ in range(count):
            s = 0.0
            for ch in range(4):
                freq, vol, aux, nr43 = frame[ch]
                if vol == 0 or freq <= 0:
                    continue
                if ch < 2:
                    self.phase[ch] = (self.phase[ch] + freq / SR) % 1.0
                    s += (1.0 if self.phase[ch] < aux else -1.0) * (vol / 15.0)
                elif ch == 2:
                    # 32-nibble wave loop; scale 0..15 -> -1..1
                    self.phase[ch] = (self.phase[ch] + freq * 32.0 / SR) % 32.0
                    nib = aux[int(self.phase[ch]) % 32]
                    s += ((nib / 7.5) - 1.0) * WAVE_VOLS[vol]
                else:
                    width7 = bool(nr43 & 0x08)
                    self.lfsr_acc += freq / SR
                    while self.lfsr_acc >= 1.0:
                        self.lfsr_acc -= 1.0
                        if width7:
                            fb = ((self.lfsr ^ (self.lfsr >> 1)) & 1) << 6
                            self.lfsr = ((self.lfsr >> 1) | fb) & 0x7F
                        else:
                            fb = ((self.lfsr ^ (self.lfsr >> 1)) & 1) << 14
                            self.lfsr = ((self.lfsr >> 1) | fb) & 0x7FFF
                    s += (-1.0 if self.lfsr & 1 else 1.0) * (vol / 15.0)
            out.append(s * 0.25)
        return out


def render_song(name):
    note_names = parse_note_defines()
    periods = parse_note_table()
    parsed = parse_song_c(GEN_MUSIC / f"{name}.c", note_names)
    emu = Emu(parsed, periods)
    loop_ticks = emu.n_orders * 64 * parsed["tempo"]
    total = min(loop_ticks * LOOPS, MAX_SECONDS * TICK_HZ)
    frames = emu.emulate(total)
    synth = Synth()
    samples = []
    for f in frames:
        samples.extend(synth.render_tick(f))
    # normalize + fade
    peak = max(1e-6, max(abs(s) for s in samples))
    gain = 0.89 / peak
    samples = [s * gain for s in samples]
    fade_n = min(len(samples), int(SR * FADE_MS / 1000))
    for i in range(fade_n):
        samples[len(samples) - fade_n + i] *= 1.0 - i / fade_n
    pcm = b"".join(struct.pack("<h", max(-32768, min(32767, int(s * 32767))))
                   for s in samples)
    return pcm, len(samples) / SR


def write_wav(path, pcm):
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(pcm)


def main(argv):
    argv = sys.argv[1:]
    out_dir = OUT_DIR
    if "--out" in argv:
        out_dir = Path(argv[argv.index("--out") + 1])
    names = [a for a in argv if not a.startswith("--") and a != str(out_dir)]
    if "--all" in argv or not names:
        names = SONGS
    for n in names:
        if n not in SONGS:
            print(f"render_music_preview.py: error: unknown song '{n}' "
                  f"(expected one of {', '.join(SONGS)})", file=sys.stderr)
            return 2
    for n in names:
        pcm, secs = render_song(n)
        dest = out_dir / f"{n}.wav"
        write_wav(dest, pcm)
        print(f"render_music_preview.py: wrote {dest} ({secs:.1f}s, "
              f"{dest.stat().st_size // 1024} KiB)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
