; Minimal CRT0: reuses GBDK vectors/dispatch/logo, minimal init.
        .module crt0
; The project links with -yo8: the fixed-bank _CODE/_HOME area spans file
; 0x0000-0x7FFF and the second half is reached at CPU 0x4000-0x7FFF with
; ROMB=1 (see the banked-content copy trampoline notes below).  This is the
; architectural home bank: every bank-switching trampoline restores it, and
; boot init stores it into __current_bank.  Keep the trampolines' restore
; sites in sync with this single constant.
HOME_BANK .equ 1
        .globl  _main
        .globl  _audio_update
        .globl  _g_harness_mode
        .globl  _oam_dma_init
        .globl  __cpu
        .globl  __is_GBA
        .globl  _cpu
        .globl  __current_bank
        .globl  .mode
        .globl  .int
        .globl  __shadow_OAM_base
        .globl  .sys_time

        .area   _HEADER (ABS)
        .org    0x0000
        .db     0xD9, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xD9, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
        .db     0xD9, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xD9, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
        .db     0xE9, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xD9, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
        .db     0xD9, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xD9, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
        .db     0xD9, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xD9, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
        .db     0xC3, 0x00, 0xC9, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xD9, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
        .db     0xD9, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
        .db     0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80
        .db     0xC5, 0xD5, 0x2A, 0xB6, 0x28, 0x0B, 0xE5, 0x3A, 0x6E, 0x67, 0xE7, 0xE1, 0x23, 0x18, 0xF3, 0xE8
        .db     0x04, 0xD1, 0xC1, 0xE1, 0xF0, 0x41, 0xE6, 0x02, 0x20, 0xFA, 0xF1, 0xD9, 0x21, 0xA3, 0xC0, 0x34
        .db     0x20, 0x02, 0x23, 0x34, 0x3E, 0x01, 0xE0, 0x91, 0xC3, 0x80, 0xFF, 0xF0, 0x41, 0xE6, 0x02, 0x20
        .db     0xFA, 0x3E, 0xC0, 0xC3, 0x84, 0xFF, 0xAF, 0x01, 0x70, 0x03, 0x21, 0xA0, 0xC0, 0xCD, 0x57, 0x4C
        .db     0x3E, 0xC0, 0xE0, 0x92, 0x67, 0xAF, 0x6F, 0x0E, 0xA0, 0xC3, 0x28, 0x00, 0xF3, 0xE0, 0xFF, 0xAF
        .db     0xFB, 0xE0, 0x0F, 0xC9, 0xF0, 0x92, 0xB7, 0xC8, 0xE0, 0x46, 0x3E, 0x28, 0x3D, 0x20, 0xFD, 0xC9
        .db     0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
        .db     0xC9, 0xFF, 0xFF, 0xFF, 0xC9, 0xFF, 0xFF, 0xFF, 0xC9, 0xFF, 0xFF, 0xFF, 0xC9, 0xFF, 0xFF, 0xFF
        .db     0x18, 0x55, 0xFF, 0xFF, 0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B, 0x03, 0x73, 0x00, 0x83
        .db     0x00, 0x0C, 0x00, 0x0D, 0x00, 0x08, 0x11, 0x1F, 0x88, 0x89, 0x00, 0x0E, 0xDC, 0xCC, 0x6E, 0xE6
        .db     0xDD, 0xDD, 0xD9, 0x99, 0xBB, 0xBB, 0x67, 0x63, 0x6E, 0x0E, 0xEC, 0xCC, 0xDD, 0xDC, 0x99, 0x9F
        .db     0xBB, 0xB9, 0x33, 0x3E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        .db     0x00, 0x00, 0x00, 0x80, 0x30, 0x30, 0x00, 0x19, 0x01, 0x00, 0x00, 0x33, 0x01, 0xB9, 0xFC, 0x8B
        .db     0xFA, 0xA1, 0xC0, 0x47, 0xFA, 0xA0, 0xC0

        .org    0x0157
init:
        di
        ld      d, a
        ld      sp, #0xE000
        ld      hl, #0xC000
        ld      bc, #0x2000
clear_loop:
        xor     a
        ld      (hl), a
        inc     hl
        dec     bc
        ld      a, b
        or      c
        jr      nz, clear_loop
        ld      a, d
        ld      (__cpu), a
        ld      (_cpu), a
        xor     a
        ld      (__is_GBA), a
        inc     a
        ld      (__current_bank), a ; a was 0 -> __current_bank = HOME_BANK (1)

        ; Copy the RAM-resident timer ISR into WRAM (always mapped,
        ; independent of ROM banking).  Music runs on the hardware timer
        ; (TIMA overflow at 256 Hz; TAC/TMA programmed by audio_init), NOT
        ; on VBlank: VBlank is a PPU mode, so the VBlank interrupt cannot
        ; fire during the LCD-off full redraws that every screen/map
        ; transition performs -- a VBlank-driven music clock stalls for 1-2
        ; frames per transition (see AGENTS.md Music contract).  IE is set
        ; here; IME is enabled later by main() -> enable_interrupts().
        call    _timer_isr_init

        ; Copy the OAM DMA routine into HRAM (0xFF80).
        call    _oam_dma_init

        ; Timer-only IE: TIMA overflow -> INT 0x50 -> JP 0xC900.  VBlank,
        ; STAT, Serial, and Joypad interrupts stay disabled; nothing in the
        ; game uses them (vsync()/wait_vbl_done() are LY-polled, OAM DMA and
        ; refresh_OAM() are self-contained).
        ld      a, #0x04
        ldh     (0xFFFF), a

        jp      _main

        .area   _CODE
        .globl  .display_off
        .globl  _display_off
_display_off:
.display_off:
        ldh     a, (0xFF40)
        and     #0x80
        ret     z
        ldh     a, (0xFF40)
        and     #0x7F
        ldh     (0xFF40), a
        ret

        .globl  .wait_vbl_done
        .globl  .vsync
        .globl  _wait_vbl_done
        .globl  _vsync
_wait_vbl_done:
_vsync:
.wait_vbl_done:
.vsync:
        ldh     a, (0xFF44)
        cp      #0x90
        jr      z, .vsync                    ; if already at LY 144, wait until it passes
vsync_wait:
        ldh     a, (0xFF44)
        cp      #0x90
        jr      nz, vsync_wait
        ret

        .globl  .call_hl
.call_hl:
        jp      (hl)

; OAM DMA routine template copied to HRAM 0xFF80.
; Must run entirely from HRAM (0xFF80-0xFFFE) because Game Boy hardware
; locks ROM and WRAM during OAM DMA (160 microseconds).
dma_copy_src:
        ldh     (0x46), a                    ; initiate OAM DMA (0xFF46) from base in A
        ld      a, #0x28                     ; ~40 * 4 cycles (~160 microseconds)
dma_copy_wait:
        dec     a
        jr      nz, dma_copy_wait
        ret
dma_copy_src_end:

        ; C-callable initializer to copy the DMA routine into HRAM 0xFF80.
        ; Called by crt0 init and ui_init() so both real boot and harness boot
        ; paths have the DMA routine resident in HRAM.
        .globl  _oam_dma_init
_oam_dma_init:
        xor     a
        ld      (__shadow_OAM_base), a
        ld      a, #0xC0
        ld      (__shadow_OAM_base + 1), a
        ld      hl, #dma_copy_src
        ld      de, #0xFF80
        ld      bc, #(dma_copy_src_end - dma_copy_src)
copy_dma_loop:
        ld      a, (hl)
        inc     hl
        ld      (de), a
        inc     de
        dec     bc
        ld      a, b
        or      c
        jr      nz, copy_dma_loop
        ret

        ; refresh_OAM() copies shadow OAM (WRAM base 0xC000) to real OAM
        ; (0xFE00) via OAM DMA.  OAM DMA locks the external bus, so the
        ; transfer loop executes from HRAM 0xFF80.  Interrupts are disabled
        ; (di) during the transfer so the 256 Hz timer ISR cannot fire
        ; mid-DMA and attempt to access locked ROM/WRAM.
        .globl  _refresh_OAM
_refresh_OAM:
        di
        ld      a, #0xC0                    ; shadow OAM lives at 0xC000
        call    0xFF80                      ; execute DMA loop in HRAM
        ld      a, (_g_harness_mode)
        or      a
        ret     nz
        ei
        ret

        .globl  .reset
        .globl  .remove_int
        .globl  .add_int
        .globl  .add_VBL
        .globl  .remove_VBL
        .globl  _add_VBL
        .globl  _vsync
        .globl  _wait_vbl_done
        .globl  _display_off
.reset:
        jp      init
.remove_int:
.remove_int_stub:
        ret
.add_int:
.add_VBL:
.remove_VBL:
_add_VBL:
        ret

; RAM-resident timer ISR: copied to WRAM 0xC900 by init and entered via
; the timer vector (0x0050 -> JP 0xC900).  Fires at 256 Hz (TIMA overflow;
; TAC/TMA programmed by audio_init()).  Calls audio_update() directly;
; audio.c must remain in the fixed bank 0 so the baked-in call target is
; always mapped.  Saves af/bc/de/hl (SDCC clobber set).
timer_isr:
        push    af
        push    bc
        push    de
        push    hl
        ld      a, (_g_harness_mode)
        or      a
        jr      nz, timer_isr_skip
        call    _audio_update
timer_isr_skip:
        pop     hl
        pop     de
        pop     bc
        pop     af
        reti
timer_isr_end:

        .globl  _timer_isr_init
_timer_isr_init:
        ld      hl, #timer_isr
        ld      de, #0xC900
        ld      bc, #(timer_isr_end - timer_isr)
copy_isr_loop:
        ld      a, (hl)
        inc     hl
        ld      (de), a
        inc     de
        dec     bc
        ld      a, b
        or      c
        jr      nz, copy_isr_loop
        ret

; ── Banked-content copy trampoline ─────────────────────────────────
; Copies `n` bytes from a banked ROM source to a WRAM destination while
; running entirely from WRAM, so the MBC5 bank switch (rROMB0 at 0x2000)
; is safe regardless of which ROM bank the fixed-bank caller currently
; maps.  A bank switch executed from switchable ROM (0x4000+) unmaps the
; very instruction stream doing the switch; WRAM is always mapped.  The
; body is copied from ROM to the linker-allocated _g_banked_tramp buffer
; (WRAM) by _banked_copy_init (see §52.11/§35: like the timer ISR, it must
; be RAM-resident because the harness skips CRT0; game_init calls the init
; so it runs in both real boot and harness boot paths).
;
; No-arg entry: the C wrapper banked_copy() (src/core/banked.c) stores the
; four arguments into _DATA globals (g_bank_copy_bank/dst/src/n) before
; calling, so this trampoline never parses SDCC's stack layout.  The bank
; register is always restored to the project's home bank 1 before returning.
; Interrupts stay enabled across the copy: the timer ISR (WRAM 0xC900)
; saves/restores the live bank via g_rom_bank_live (banked.h), so a tick
; mid-copy resumes on the content bank.  Holding di for the whole copy
; would stall the music clock (verify-music gate).  Only the two bank
; switches themselves run under di.  ISR register safety comes from the
; ISR's own push/pop (af/bc/de/hl); PPU-window atomicity for LCD-on
; writers comes from BANKED_VRAM_WRITE's per-store di, not from here.
;
; Home bank is 1, NOT 0: the project links with -yo8, so the fixed-bank
; _CODE/_HOME area spans file 0x0000-0x7FFF and the second half (file
; 0x4000-0x7FFF, containing the SDCC runtime helpers and library code) is
; reached at CPU 0x4000-0x7FFF with ROMB=1 (crt0.s init stores
; __current_bank = 1).  Bank 0 (file 0x0000-0x3FFF) only covers the fixed
; region; with ROMB=0, CPU 0x4000+ reads bank 0's first half (file
; 0x0000-0x3FFF) and every fixed-bank call above 0x4000 executes garbage.
        .globl  _g_bank_copy_bank
        .globl  _g_bank_copy_dst
        .globl  _g_bank_copy_src
        .globl  _g_bank_copy_n
        .globl  _g_rom_bank_live
; The banked_copy_init byte-copy copies this whole body into the linker-
; allocated WRAM buffer g_banked_tramp[64] (banked.c).  These start/end
; symbols are exported so tools/memmap.py can verify at build time that the
; body still fits the buffer (fail instead of silently overwriting the
; banked-call staging globals that follow it in WRAM).
        .globl  _banked_copy_tramp
        .globl  _banked_copy_tramp_end
_banked_copy_tramp:
        push    bc
        push    de
        di                            ; protect the bank select + live store
        ld      a, (_g_bank_copy_bank)
        ld      (0x2000), a          ; select ROM bank (low byte only; bit 9
                                     ; stays 0 -- zeroed once at boot in
                                     ; main(), nothing else touches 0x3000;
                                     ; banks 16+ out of scope, AGENTS 55.5)
        ld      (_g_rom_bank_live), a ; ISR restores this, not home
        ei                            ; copy runs with the music clock live
        ld      hl, #_g_bank_copy_src
        ld      a, (hl)
        inc     hl
        ld      h, (hl)
        ld      l, a                ; hl = g_bank_copy_src
        ld      d, h
        ld      e, l                ; de = src
        ld      hl, #_g_bank_copy_dst
        ld      a, (hl)
        inc     hl
        ld      h, (hl)
        ld      l, a                ; hl = dst
        ld      a, (_g_bank_copy_n)
        or      a
        jr      z, banked_copy_done
        ld      c, a
 banked_copy_loop:
        ld      a, (de)             ; read src byte
        ld      (hl), a             ; write dst byte
        inc     de
        inc     hl
        dec     c
        jr      nz, banked_copy_loop
 banked_copy_done:
        di                            ; protect the bank restore
        ld      a, #HOME_BANK
        ld      (0x2000), a          ; restore home bank (HOME_BANK, see below)
        ld      (_g_rom_bank_live), a ; a == HOME_BANK == 1
        ei                            ; home bank restored, interrupts safe again
 banked_copy_ret:
        pop     de
        pop     bc
        ret
 _banked_copy_tramp_end:

; ROM stub for the C wrapper (src/core/banked.c): jumps into the WRAM
; trampoline.  The trampoline's WRAM home is the linker-allocated C buffer
; _g_banked_tramp (src/core/banked.c), so its address is resolved by the
; linker rather than hardcoded.  Runs from the fixed bank and switches
; nothing, so it is safe regardless of the caller's address.
        .globl  _g_banked_tramp
        .globl  _banked_copy_run
_banked_copy_run:
        ld      hl, #_g_banked_tramp
        jp      (hl)

; Copies the trampoline body from ROM into the _g_banked_tramp buffer.
; Called once from game_init() so both real hardware and the harness have it
; resident before any banked content is read.
        .globl  _banked_copy_init
_banked_copy_init:
        ld      hl, #_banked_copy_tramp
        ld      de, #_g_banked_tramp
        ld      bc, #(_banked_copy_tramp_end - _banked_copy_tramp)
banked_copy_init_loop:
        ld      a, (hl)
        inc     hl
        ld      (de), a
        inc     de
        dec     bc
        ld      a, b
        or      c
        jr      nz, banked_copy_init_loop
        ret

; ── Banked-call trampoline ─────────────────────────────────────────
; Mirrors _banked_copy_tramp but EXECUTES a fixed no-arg function that
; lives in a banked ROM bank, from a fixed-bank caller.  Lets a
; self-contained module (e.g. src/battle/combo.c) move out of the fixed
; bank to relieve the fixed-bank budget without pulling GBDK's
; RAM-resident ___sdcc_banked_call/___sdcc_call_hl helpers (which the
; harness never installs because it skips CRT0; see §52.1/§52.11).
;
; The fixed-bank C wrapper stages the target bank + logical address and
; any arguments into _DATA globals (banked.c), then calls the ROM stub
; _banked_call_run, which jumps into the WRAM copy of this body.  The WRAM
; trampoline selects the bank, computes the banked function's runtime
; address 0x4000 | (target & 0x3FFF), pushes the WRAM return address, and
; `jp`s to the target.  When the target `ret`s it pops that WRAM address,
; so control returns to WRAM (always mapped) which restores the home bank.
;
; Like banked_copy, the switch + restore run from WRAM; the body itself
; runs with interrupts ENABLED (ei after the select, di before the
; restore) so the 256 Hz music clock never stalls across long bodies.
; The timer ISR saves/restores the live bank via g_rom_bank_live, so the
; body resumes on its own bank.  The wrappers push/pop caller registers
; around the call.  The target must be self-contained: it may read its
; own banked data and the staged WRAM globals, but must NOT call any
; fixed-bank function (the bank is switched away for the duration).
; LCD-on VRAM/CRAM data writes inside the target must use
; BANKED_VRAM_WRITE (banked.h): the ISR can otherwise eclipse the
; wait->store window.
        .globl  _g_bk_call_bank
        .globl  _g_bk_call_target
        .globl  _g_rom_bank_live
        .globl  _g_banked_call_tramp
; _banked_call_tramp and _banked_call_tramp_end (both .globl below / above)
; bound the body copied into g_banked_call_tramp[64] (see the size contract
; at _banked_call_tramp_end).
_banked_call_tramp:
        push    bc
        push    de
        di                            ; protect the bank select + live store
        ld      a, (_g_bk_call_bank)
        ld      (0x2000), a          ; select the target ROM bank (low byte
                                     ; only; bit 9 stays 0, see copy tramp)
        ld      (_g_rom_bank_live), a ; ISR restores this, not home
        ei                            ; body runs with the music clock live
        ld      hl, #_g_bk_call_target
        ld      a, (hl)
        inc     hl
        ld      h, (hl)
        ld      l, a                ; hl = target logical 16-bit address
        ld      a, h
        and     a, #0x3F
        or      a, #0x40
        ld      h, a                ; hl = target runtime address (0x4000|off)
        ld      d, h
        ld      e, l                ; de = target runtime address
        ld      hl, #(_g_banked_call_tramp + (banked_call_ret_wram - _banked_call_tramp))
        push    hl                   ; WRAM return address for the target's ret
        ld      h, d
        ld      l, e
        jp      (hl)                ; run the banked function
 banked_call_ret_wram:
        di                            ; protect the bank restore
        ld      a, #HOME_BANK
        ld      (0x2000), a          ; restore home bank (HOME_BANK)
        ld      (_g_rom_bank_live), a ; a == HOME_BANK == 1
        ei                            ; home bank restored, interrupts safe again
 banked_call_ret:
        pop     de
        pop     bc
        ret
; Same build-time size contract as the copy trampoline above: the
; banked_call_init byte-copy must fit the linker-allocated WRAM buffer
; g_banked_call_tramp[64] (banked.c).  Start/end are exported so
; tools/memmap.py can verify the body size at build time.
        .globl  _banked_call_tramp
        .globl  _banked_call_tramp_end
_banked_call_tramp_end:

; ROM stub for the fixed-bank C wrapper (src/core/banked.c): jumps into the
; WRAM trampoline.  The trampoline's WRAM home is the linker-allocated C
; buffer _g_banked_call_tramp, so its address is resolved by the linker.
        .globl  _banked_call_run
_banked_call_run:
        ld      hl, #_g_banked_call_tramp
        jp      (hl)

; Copies the banked-call trampoline body from ROM into the
; _g_banked_call_tramp buffer.  Called once from game_init() so both real
; hardware and the harness have it resident before any banked call.
        .globl  _banked_call_init
_banked_call_init:
        ld      hl, #_banked_call_tramp
        ld      de, #_g_banked_call_tramp
        ld      bc, #(_banked_call_tramp_end - _banked_call_tramp)
banked_call_init_loop:
        ld      a, (hl)
        inc     hl
        ld      (de), a
        inc     de
        dec     bc
        ld      a, b
        or      c
        jr      nz, banked_call_init_loop
        ret

        .area   _HOME
        .area   _LIT
        .area   _GSINIT
        .area   _GSFINAL
        .area   _INITIALIZER

        .area   _DATA
__cpu:
        .ds     1
__is_GBA:
        .ds     1
_cpu:
        .ds     1
__current_bank:
        .ds     1
.mode:
        .ds     1
        .globl  _console_mode
_console_mode = .mode
.int:
        .ds     1
__shadow_OAM_base:
        .ds     2
.sys_time:
        .ds     4

        .area   _INITIALIZED
        .area   _BSS
        .area   _HEAP
        .area   _HEAP_END
