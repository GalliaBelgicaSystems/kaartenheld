#include "banked.h"

/* Argument staging for the RAM-resident copy trampoline (src/crt0.s
 * _banked_copy_tramp, copied to WRAM 0xC940).  The C wrapper stores the
 * SDCC arguments here and then calls banked_copy_run() (asm) so the
 * trampoline never has to parse the compiler's stack layout.  Only the
 * switch + copy executes from WRAM; everything before/after runs in the
 * fixed bank. */

uint8_t g_bank_copy_bank;
void *g_bank_copy_dst;
const void *g_bank_copy_src;
uint8_t g_bank_copy_n;

/* Linker-allocated home for the RAM-resident copy trampoline.  crt0.s's
 * _banked_copy_init copies the ROM trampoline body here and
 * _banked_copy_run jumps to it, so the linker owns this address (a
 * hardcoded WRAM constant would collide with _INITIALIZED variables). */
uint8_t g_banked_tramp[64];

/* Live MBC5 ROM-bank mirror for the timer ISR (see banked.h).  Plain
 * definition (BSS): main() assigns the home bank before interrupts are
 * enabled, covering both the CRT0 boot path and the harness path (which
 * skips CRT0, so an initializer would not be copied there anyway). */
volatile uint8_t g_rom_bank_live;

/* ── Banked-call staging (mirrors banked_copy) ──────────────────────
 * The fixed-bank wrapper stores the target bank/address and its arguments
 * here, then calls banked_call_run() so the WRAM trampoline (crt0.s) never
 * parses SDCC's stack layout.  The banked target reads these back. */
uint8_t g_bk_call_bank;
uint16_t g_bk_call_target;
void *g_bk_ptr_a;
void *g_bk_ptr_b;
uint8_t g_bk_byte_a;
uint8_t g_bk_byte_b;

/* Linker-allocated home for the RAM-resident banked-call trampoline. */
uint8_t g_banked_call_tramp[64];

/* Extra staging bytes for banked ↔ fixed communication. */
uint8_t g_bk_byte_c;
uint8_t g_bk_byte_d;

extern void banked_copy_run(void);

void banked_copy(uint8_t bank, void *dst, const void *src, uint8_t n)
{
    g_bank_copy_bank = bank;
    g_bank_copy_dst = dst;
    g_bank_copy_src = src;
    g_bank_copy_n = n;
    banked_copy_run();
}
