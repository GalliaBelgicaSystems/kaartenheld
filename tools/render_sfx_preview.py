#!/usr/bin/env python3
"""Render transcribed SFX step tables to WAV previews for the level editor.

Reads generated/sfx/sfx_tables.c (SfxToneStep/SfxNoiseStep arrays plus the
s_sfx_index entry table) and emulates the banked stepper
(src/audio/sfx_step.c) tick by tick: masked NR21-NR24 / NR41-NR44 writes,
envelope volume, pitch, and noise. Anything outside the observed table
shape is a loud error, never a guess -- the ROM remains authoritative.

The synth (square/noise voices, 22050 Hz mono) is shared with
render_music_preview.py; this script only emulates register state.

Usage:
    python3 tools/render_sfx_preview.py --all
    python3 tools/render_sfx_preview.py cursor attack
    python3 tools/render_sfx_preview.py --all --out some/dir/

Output: tools/level_editor/public/audio/sfx/<name>.wav (lowercase SFX id,
one shot plus a short tail so the envelope rings out). Deterministic:
same tables in, byte-identical WAV out.
"""
import re
import struct
import sys
import wave
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent  # tools/render_sfx_preview.py -> repo root
sys.path.insert(0, str(SCRIPT_DIR))
from render_music_preview import Synth, noise_freq, period_to_freq, DUTIES, SR

TABLES_C = REPO_ROOT / "generated" / "sfx" / "sfx_tables.c"
OUT_DIR = (REPO_ROOT / "tools" / "level_editor" / "public" / "audio" / "sfx")

SFX_IDS = ["CURSOR", "CONFIRM", "SELECT", "BACK", "ATTACK", "HIT", "BLOCK"]

TAIL_TICKS = 8
FADE_MS = 30


class RenderError(Exception):
    pass


def parse_tables(path):
    text = Path(path).read_text()
    tones, noises = {}, {}

    def steps(name, regex):
        for m in re.finditer(regex, text, re.S):
            rows = []
            for it in re.finditer(r"\{([^{}]*)\}", m.group(1)):
                vals = [int(t.strip(), 0) for t in it.group(1).split(",")
                        if t.strip()]
                if len(vals) != 6:
                    raise RenderError(f"{name}: step size {len(vals)}")
                rows.append(tuple(vals))
            return rows
        raise RenderError(f"{name}: array not found")

    for sfx in SFX_IDS:
        low = sfx.lower()
        tones[sfx] = steps(f"s_sfx_{low}_tone",
                           r"s_sfx_" + low + r"_tone\[\] = \{(.*?)\};")
        noises[sfx] = steps(f"s_sfx_{low}_noise",
                            r"s_sfx_" + low + r"_noise\[\] = \{(.*?)\};")

    index = {}
    m = re.search(r"s_sfx_index\[\] = \{(.*?)\};", text, re.S)
    if not m:
        raise RenderError("s_sfx_index not found")
    rows = re.findall(r"\{([^{}]*)\}", m.group(1))
    if len(rows) != len(SFX_IDS):
        raise RenderError(f"s_sfx_index has {len(rows)} rows, "
                          f"expected {len(SFX_IDS)}")
    for sfx, row in zip(SFX_IDS, rows):
        toks = [t.strip() for t in row.split(",") if t.strip()]
        # { tone_ptr, tone_len, noise_ptr, noise_len, total_ticks }
        if len(toks) != 5:
            raise RenderError(f"{sfx}: index row size {len(toks)}")
        tone_len, noise_len, total = (int(toks[1], 0), int(toks[3], 0),
                                      int(toks[4], 0))
        # Empty voices still emit a 1-row placeholder the stepper never
        # executes (tone_len/noise_len 0): slice to the live rows.
        if tone_len > len(tones[sfx]) or noise_len > len(noises[sfx]):
            raise RenderError(f"{sfx}: index lens exceed arrays")
        index[sfx] = (tones[sfx][:tone_len], noises[sfx][:noise_len], total)
    return index


def env_vol(env, elapsed):
    vol, pace, direction = (env >> 4) & 0x0F, env & 0x07, (env >> 3) & 0x01
    if pace == 0:
        return vol
    if direction == 1:
        return min(15, vol + elapsed // pace)
    return max(0, vol - elapsed // pace)


def render_sfx(name, index):
    t_rows, n_rows, total = index[name]
    t_steps = {s[0]: s for s in t_rows}
    n_steps = {s[0]: s for s in n_rows}
    # register images (power-on 0, like the stepper's first read)
    nr21 = nr22 = f_lo = f_hi = 0
    nr41 = nr42 = nr43 = nr44 = 0
    env_t0 = nenv_t0 = 0
    frames = []
    end = total + TAIL_TICKS
    for tick in range(end):
        if tick in t_steps:
            _, mask, a, b, c, d = t_steps[tick]
            if mask & 0x01:
                nr21 = a
            if mask & 0x02:
                nr22 = b
                env_t0 = tick
            if mask & 0x04:
                f_lo = c
            if mask & 0x08:
                f_hi = d
        if tick in n_steps:
            _, mask, a, b, c, d = n_steps[tick]
            if mask & 0x01:
                nr41 = a
            if mask & 0x02:
                nr42 = b
                nenv_t0 = tick
            if mask & 0x04:
                nr43 = c
            if mask & 0x08:
                nr44 = d
        # tone voice (CH2): duty + envelope + period
        tvol = env_vol(nr22, tick - env_t0)
        period = f_lo | ((f_hi & 0x07) << 8)
        if tvol == 0 or period == 0 or period >= 2048:
            tone = (0.0, 0, 0, 0)
        else:
            tone = (period_to_freq(period), tvol, DUTIES[(nr21 >> 6) & 0x03], 0)
        # noise voice (CH4)
        nvol = env_vol(nr42, tick - nenv_t0)
        if nvol == 0:
            noise = (0.0, 0, 0, 0)
        else:
            noise = (noise_freq(nr43), nvol, 0, nr43)
        frames.append(((0.0, 0, 0, 0), tone, (0.0, 0, 0, 0), noise))
    synth = Synth()
    samples = []
    for f in frames:
        samples.extend(synth.render_tick(f))
    peak = max(1e-6, max(abs(s) for s in samples))
    gain = 0.89 / peak
    samples = [s * gain for s in samples]
    fade_n = min(len(samples), int(SR * FADE_MS / 1000))
    for i in range(fade_n):
        samples[len(samples) - fade_n + i] *= 1.0 - i / fade_n
    pcm = b"".join(struct.pack("<h", max(-32768, min(32767, int(s * 32767))))
                   for s in samples)
    return pcm, len(samples) / SR


def main():
    argv = sys.argv[1:]
    out_dir = OUT_DIR
    if "--out" in argv:
        out_dir = Path(argv[argv.index("--out") + 1])
    names = [a.upper() for a in argv
             if not a.startswith("--") and a != str(out_dir)]
    if "--all" in argv or not names:
        names = SFX_IDS
    for n in names:
        if n not in SFX_IDS:
            print(f"render_sfx_preview.py: error: unknown SFX '{n}' "
                  f"(expected one of {', '.join(s.lower() for s in SFX_IDS)})",
                  file=sys.stderr)
            return 2
    index = parse_tables(TABLES_C)
    for n in names:
        pcm, secs = render_sfx(n, index)
        dest = out_dir / f"{n.lower()}.wav"
        dest.parent.mkdir(parents=True, exist_ok=True)
        with wave.open(str(dest), "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(SR)
            w.writeframes(pcm)
        print(f"render_sfx_preview.py: wrote {dest} ({secs:.2f}s, "
              f"{dest.stat().st_size // 1024} KiB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
