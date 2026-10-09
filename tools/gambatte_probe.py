#!/usr/bin/env python3
"""Minimal ctypes libretro frontend for the Gambatte core.

Diagnostic tool: boots a .gb/.gbc ROM in the same core RetroArch uses,
advances frames, injects input, and dumps screenshots + the visible BG
tilemap as ASCII.  Used to reproduce the field-spider white screen that
shows only under Gambatte (SameBoy/PyBoy run the same bytes fine).

Usage: python3 tools/gambatte_probe.py <rom> [script]
Script: semicolon list of actions, e.g. "wait 600;press start;wait 60;..."
Actions: wait N | press BTN | release BTN | shot NAME | text
Buttons: up down left right a b start select l r x y
"""
import ctypes as C
import sys, os, struct

LIB = "/tmp/opencode/ra-1/lib/retroarch/cores/gambatte_libretro.so"
if not os.path.exists(LIB):
    cands = ["/tmp/opencode/ra-1/lib/retro/cores/gambatte_libretro.so"]
    for c in cands:
        if os.path.exists(c):
            LIB = c
            break

BTN = {'b':0,'y':1,'select':2,'start':3,'up':4,'down':5,'left':6,'right':7,
       'a':8,'x':9,'l':10,'r':11}

# libretro environment command ids (libretro.h)
RETRO_ENV_GET_LOG_INTERFACE = 27
RETRO_ENV_GET_SYSTEM_DIRECTORY = 9
RETRO_ENV_GET_SAVE_DIRECTORY = 25
RETRO_ENV_SET_PIXEL_FORMAT = 10
RETRO_ENV_GET_CAN_DUPE = 3
RETRO_ENV_SET_MESSAGE_EXT = 60
RETRO_ENV_SHUTDOWN = 7

log_fn = None
pix_fmt = [0]
shutdown = [False]

CORE_LOG = C.CFUNCTYPE(None, C.c_uint, C.c_char_p, C.c_int, C.c_int, C.c_int, C.c_int, C.c_int, C.c_int)
core_log_cb = CORE_LOG(lambda lvl, fmt, *rest: sys.stderr.write("[core] %s\n" % fmt.decode('utf-8', 'replace')))

def env_cb(cmd, data):
    if os.environ.get('GB_TRACE_ENV'):
        print("env %d" % cmd, file=sys.stderr)
    if cmd == RETRO_ENV_GET_LOG_INTERFACE:
        struct_ = C.cast(data, C.POINTER(C.c_void_p))
        struct_.contents.value = C.cast(core_log_cb, C.c_void_p).value
        return True
    if cmd == RETRO_ENV_GET_SYSTEM_DIRECTORY or cmd == RETRO_ENV_GET_SAVE_DIRECTORY:
        struct_ = C.cast(data, C.POINTER(C.c_char_p))
        struct_.contents.value = b"/tmp/opencode/saves"
        return True
    if cmd == RETRO_ENV_SET_PIXEL_FORMAT:
        pix_fmt[0] = C.cast(data, C.POINTER(C.c_int)).contents.value
        return True
    if cmd == RETRO_ENV_GET_CAN_DUPE:
        C.cast(data, C.POINTER(C.c_bool)).contents.value = True
        return True
    if cmd == RETRO_ENV_SHUTDOWN:
        shutdown[0] = True
        return True
    return False

ENV_CB = C.CFUNCTYPE(C.c_bool, C.c_uint, C.c_void_p)
VID_CB = C.CFUNCTYPE(None, C.POINTER(C.c_uint16), C.c_uint, C.c_uint, C.c_size_t)
SMP_CB = C.CFUNCTYPE(None, C.c_int16, C.c_int16)
SMPB_CB = C.CFUNCTYPE(C.c_size_t, C.POINTER(C.c_int16), C.c_size_t)
POLL_CB = C.CFUNCTYPE(None)
STATE_CB = C.CFUNCTYPE(C.c_int16, C.c_uint, C.c_uint, C.c_uint, C.c_uint)

last_frame = {}
stats = {'vid': 0, 'state': 0, 'audio': 0}

def vid_cb(buf, w, h, pitch):
    stats['vid'] += 1
    data = C.string_at(buf, h * pitch)
    last_frame['w'], last_frame['h'], last_frame['pitch'] = w, h, pitch
    last_frame['data'] = data

def audio_ok(a, b):
    return 0

def audio_batch(data, frames):
    return frames

def poll():
    pass

input_mask = [0]
input_trace = [0]

def state_cb(port, dev, idx, ident):
    stats['state'] += 1
    r = (input_mask[0] >> ident) & 1 if dev == 1 else 0
    if os.environ.get('GB_TRACE_INPUT'):
        # Print a window around any press: every call while a button is
        # held, plus the 15 calls just before the first sampled press.
        if input_mask[0] != 0:
            input_trace[0] = 30
        if input_trace[0] > 0:
            input_trace[0] -= 1
            print("state(dev=%d id=%d) -> %d (mask=0x%x)" % (dev, ident, r, input_mask[0]), file=sys.stderr)
    if dev == 1:
        return r
    return 0

def main():
    rom_path = sys.argv[1]
    script = sys.argv[2] if len(sys.argv) > 2 else "shot boot"
    os.makedirs("/tmp/opencode/saves", exist_ok=True)

    lib = C.CDLL(LIB)
    print("step: dlopen ok", LIB, file=sys.stderr)
    # Keep the callback objects alive: ctypes temporaries get GC'd while
    # the core still holds the function pointers.
    global _keep
    _keep = [
        ENV_CB(env_cb), VID_CB(vid_cb), SMP_CB(audio_ok), SMPB_CB(audio_batch),
        POLL_CB(poll), STATE_CB(state_cb),
    ]
    lib.retro_set_environment(_keep[0])
    print("step: env set", file=sys.stderr)
    lib.retro_set_video_refresh(_keep[1])
    lib.retro_set_audio_sample(_keep[2])
    lib.retro_set_audio_sample_batch(_keep[3])
    lib.retro_set_input_poll(_keep[4])
    lib.retro_set_input_state(_keep[5])
    print("step: cbs set", file=sys.stderr)
    lib.retro_init()
    print("step: init ok", file=sys.stderr)
    # Frontend duty: declare port 0 as a JOYPAD device.  Without this the
    # core may sample with an uninitialized device type (ghost input).
    if hasattr(lib, 'retro_set_controller_port_device'):
        lib.retro_set_controller_port_device(0, 1)  # RETRO_DEVICE_JOYPAD
        print("step: controller port set", file=sys.stderr)

    data = open(rom_path, 'rb').read()
    # Build retro_game_info manually (layout: path*, data*, size, meta*)
    class GameInfo(C.Structure):
        _fields_ = [('path', C.c_char_p), ('data', C.c_void_p),
                    ('size', C.c_size_t), ('meta', C.c_void_p)]
    if os.environ.get('GB_PATH_ONLY'):
        gi = GameInfo(rom_path.encode(), None, 0, None)
    else:
        gi = GameInfo(rom_path.encode(), C.cast(C.c_char_p(data), C.c_void_p), len(data), None)
    print("step: calling load_game", file=sys.stderr)
    print("gi: path=%s data=%s size=%d" % (gi.path, gi.data, gi.size), file=sys.stderr)
    r = lib.retro_load_game(C.byref(gi))
    print("load_game:", r, "pix_fmt:", pix_fmt[0], file=sys.stderr)
    if not r:
        sys.exit(1)
    print("step: first run", file=sys.stderr)
    lib.retro_run()
    print("step: run ok, w=%s h=%s fmt=%s" % (last_frame.get('w'), last_frame.get('h'), pix_fmt[0]), file=sys.stderr)

    # action parser
    actions = [s.strip() for s in script.split(';') if s.strip()]
    frame = 0
    for act in actions:
        parts = act.split()
        op, arg = parts[0].lower(), parts[1] if len(parts) > 1 else None
        if op == 'wait':
            n = int(arg)
            for _ in range(n):
                lib.retro_run()
                frame += 1
        elif op == 'press':
            input_mask[0] |= (1 << BTN[arg.lower()])
            hold = int(os.environ.get('GB_HOLD', '2'))
            for _ in range(hold):
                lib.retro_run()
                frame += 1
        elif op == 'release':
            input_mask[0] &= ~(1 << BTN[arg.lower()])
            for _ in range(1):
                lib.retro_run()
                frame += 1
        elif op == 'shot':
            dump_frame(arg or ('f%d' % frame))
        elif op == 'text':
            dump_text()
        elif op == 'vram':
            dump_vram(lib)
        elif op == 'dump':
            dump_vram_named(lib, arg or 'sys')
            print("stats vid=%d state=%d" % (stats['vid'], stats['state']), file=sys.stderr)
        print("after %d frames: %s" % (frame, act), file=sys.stderr)
    lib.retro_unload_game()
    lib.retro_deinit()

def dump_vram_named(lib, name):
    lib.retro_get_memory_size.restype = C.c_size_t
    lib.retro_get_memory_data.restype = C.c_void_p
    size = lib.retro_get_memory_size(2)
    ptr = lib.retro_get_memory_data(2)
    if ptr and size:
        open('/tmp/opencode/mem_%s.bin' % name, 'wb').write(C.string_at(ptr, size))
        print("dumped -> /tmp/opencode/mem_%s.bin" % name, file=sys.stderr)

def dump_vram(lib):
    lib.retro_get_memory_size.restype = C.c_size_t
    lib.retro_get_memory_data.restype = C.c_void_p
    size = lib.retro_get_memory_size(2)
    ptr = lib.retro_get_memory_data(2)
    print("memory system id=2 size=0x%x ptr=%s" % (size, ptr), file=sys.stderr)
    if ptr and size:
        buf = C.string_at(ptr, size)
        path = '/tmp/opencode/mem_system.bin'
        open(path, 'wb').write(buf)
        print("dumped %d bytes -> %s" % (len(buf), path), file=sys.stderr)

def dump_frame(name):
    w, h, pitch = last_frame['w'], last_frame['h'], last_frame['pitch']
    data = last_frame['data']
    from PIL import Image
    if pix_fmt[0] == 1:  # RETRO_PIXEL_FORMAT_XRGB1555
        img = Image.new('RGB', (w, h))
        px = img.load()
        for y in range(h):
            row = data[y*pitch:(y*pitch + w*2)]
            for x in range(w):
                v = row[x*2] | (row[x*2+1] << 8)
                px[x, y] = ((v & 0x7C00) >> 7, (v & 0x03E0) >> 2, (v & 0x001F) << 3)
        path = '/tmp/opencode/%s.png' % name
        img.save(path)
    else:  # RGB565 (0)
        img = Image.new('RGB', (w, h))
        px = img.load()
        for y in range(h):
            row = data[y*pitch:(y*pitch + w*2)]
            for x in range(w):
                v = row[x*2] | (row[x*2+1] << 8)
                px[x, y] = ((v & 0xF800) >> 8, (v & 0x07E0) >> 3, (v & 0x001F) << 3)
        path = '/tmp/opencode/%s.png' % name
        img.save(path)
    print("shot ->", path, file=sys.stderr)

def dump_text():
    raise NotImplementedError

if __name__ == '__main__':
    main()
