#include <gb/gb.h>
#include "game.h"
#include "audio.h"
#include "banked.h"
#include "input.h"
#include "ui.h"
#include "content.h"

Game g_game;

/*
 * Set to 1 by the external test harness (via memory write) before
 * jumping to main.  When active, audio, interrupt handlers, and vsync
 * are skipped so the emulator debugger can use breakpoints for frame
 * synchronisation without blocking on VBlank or DMA.
 *
 * Core subsystem initialisation (input, UI, fonts) always runs.
 */
volatile uint8_t g_harness_mode;  /* BSS zero (AGENTS.md 52.27) */

/*
 * Boot-phase breadcrumb for the test harness.  Incremented at each
 * startup checkpoint so the harness can verify the boot sequence.
 *  0 = ROM loaded, CRT0 not yet entered
 *  1 = main() entered
 *  2 = ui_init() complete
 *  3 = game_init() complete
 *  4 = first game_render() complete
 */
volatile uint8_t g_boot_phase;  /* BSS zero (AGENTS.md 52.27) */

extern void timer_isr_init(void);
extern void oam_dma_init(void);
extern void banked_copy_init(void);
extern void banked_call_init(void);

int main(void)
{
    __asm
        di
        ld sp, #0xE000
    __endasm;
    *(volatile uint8_t *)0x2000 = 1;
    *(volatile uint8_t *)0x3000 = 0;
    /* Live-bank mirror for the timer ISR (banked.h): the home bank is
     * mapped here, before enable_interrupts() below.  Assigned (not
     * initialized) so the harness path, which skips CRT0, agrees. */
    g_rom_bank_live = 1;
    timer_isr_init();
    oam_dma_init();
    banked_copy_init();
    banked_call_init();
    g_boot_phase = 1;

    if (!g_harness_mode) {
        audio_init();
        /* The timer ISR (the music clock) is set up by CRT0 (timer vector
         * 0x0050 -> WRAM ISR -> audio_update) and timer IE is enabled there;
         * this enables IME.  Music deliberately does NOT ride VBlank: the
         * LCD-off full redraws on every screen/map transition produce no
         * VBlank interrupt, so a VBlank-driven clock would stall the music
         * (see AGENTS.md Music contract). */
        enable_interrupts();
    }

    input_init();
    ui_init();
    g_boot_phase = 2;

    /* Composition root: register THIS game's content with the generic
     * engine before boot glue runs. */
    game_content_init();
    game_init(&g_game);
    g_boot_phase = 3;

    while (1) {
        input_update();
        game_update(&g_game);
        if (g_boot_phase == 3) {
            g_boot_phase = 4;
        }
        /* Render updates WRAM buffers (shadow OAM, battle/menu state) during
         * active display.  Full redraws (screen/map changes) explicitly disable
         * the LCD in game_render because they exceed the VBlank budget. */
        game_render(&g_game);
        if (!g_harness_mode) {
            /* Synchronize to the start of VBlank (LY == 144) so hardware
             * scroll registers and OAM DMA execute strictly within Mode 1. */
            vsync();
        }
        if (g_game.screen == SCREEN_OVERWORLD) {
            ui_commit_camera(&g_game.world);
        }
        /* DMA shadow OAM (0xC000) to real OAM (0xFE00) strictly during VBlank.
         * Running OAM DMA while the PPU scans active display locks the OAM bus,
         * causing sprite flicker/dropouts on real hardware. */
        ui_sprite_commit();
    }
}
