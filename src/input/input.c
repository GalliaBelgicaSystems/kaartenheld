#include <gb/gb.h>
#include "input.h"

#ifdef DEBUG_BUILD
extern volatile uint8_t g_harness_mode;
#endif

/* InputButton bits MUST equal GBDK joypad()'s J_* bits, otherwise the real
 * game's controls diverge from what the harness tests (g_inp_mask uses
 * 1 << InputButton while release input uses joypad()'s layout).  A mismatch
 * makes this array size 0, which is a hard compile error. */
static const int g_input_bit_layout_ok[
    (1 << INPUT_RIGHT) == J_RIGHT &&
    (1 << INPUT_LEFT)  == J_LEFT  &&
    (1 << INPUT_UP)    == J_UP    &&
    (1 << INPUT_DOWN)  == J_DOWN  &&
    (1 << INPUT_A)     == J_A     &&
    (1 << INPUT_B)     == J_B     &&
    (1 << INPUT_SELECT)== J_SELECT &&
    (1 << INPUT_START) == J_START ? 1 : 0
];

volatile uint8_t g_inp_mask = 0;
static uint8_t pad_state = 0;
static uint8_t prev_pad_state = 0;
static uint8_t injected_pad_state = 0;

#ifdef DEBUG_BUILD
/* Cross-emulator joypad probe (diagnostic, harness/host reads it via the
 * symbol table): [0] = raw joypad() sample, [1] = P1 register byte as seen
 * right after the sample, [2] = post-injection pad_state, [3] = write
 * heartbeat (increments once per input_update).  Passive only: the actual
 * P1 sampling stays GBDK's joypad() (which ends with both rows
 * deselected); a local reimplementation that leaves a row selected
 * returns stale data on the next call in strict emulators. */
volatile uint8_t g_input_probe[4] = {0, 0, 0, 0};
#endif

const uint8_t g_input_button_bits[8] = {
    (uint8_t)(1 << INPUT_RIGHT),
    (uint8_t)(1 << INPUT_LEFT),
    (uint8_t)(1 << INPUT_UP),
    (uint8_t)(1 << INPUT_DOWN),
    (uint8_t)(1 << INPUT_A),
    (uint8_t)(1 << INPUT_B),
    (uint8_t)(1 << INPUT_SELECT),
    (uint8_t)(1 << INPUT_START)
};

void input_init(void)
{
    pad_state = joypad();
    prev_pad_state = pad_state;
    injected_pad_state = 0;
    g_inp_mask = 0;
}

void input_reset(void)
{
    pad_state = 0;
    prev_pad_state = 0;
    injected_pad_state = 0;
    g_inp_mask = 0;
}

void input_update(void)
{
#ifdef DEBUG_BUILD
    uint8_t physical_pad_state;
#endif
    prev_pad_state = pad_state;
#ifdef DEBUG_BUILD
    /* Debug builds must remain playable outside the harness.  The harness
     * writes g_inp_mask for deterministic one-frame presses, while mGBA,
     * Gambatte, and a human controller provide normal joypad input.  The
     * harness skips CRT0 and controls frames through the debugger, so do not
     * call the hardware joypad path there. */
    physical_pad_state = g_harness_mode ? 0 : joypad();
    if (g_inp_mask != 0) {
        injected_pad_state = g_inp_mask;
        g_inp_mask = 0;
    }
    pad_state = physical_pad_state | injected_pad_state;
    injected_pad_state = 0;
    g_input_probe[0] = physical_pad_state;
    g_input_probe[1] = P1_REG;
    g_input_probe[2] = pad_state;
    g_input_probe[3]++;
    /* NOTE: do NOT telemetry_emit pad edges here -- the 32-event ring is
     * semantic state (BATTLE_WON etc.) and per-button noise floods it out
     * (host-side joypad diagnosis reads g_input_probe via the debugger). */
#else
    /* Honor the debug injection channel outside DEBUG_BUILD too: without
     * this, g_inp_mask writes are silently discarded (the mask is cleared
     * below but never sampled), breaking any tooling that drives the
     * release ROM through the debugger. */
    if (g_inp_mask != 0) {
        injected_pad_state = g_inp_mask;
        g_inp_mask = 0;
    }
    pad_state = joypad() | injected_pad_state;
    injected_pad_state = 0;
#endif
}

bool input_pressed(InputButton button)
{
    uint8_t mask = (1 << button);
    return ((pad_state & mask) != 0) && ((prev_pad_state & mask) == 0);
}

bool input_held(InputButton button)
{
    uint8_t mask = (1 << button);
    return (pad_state & mask) != 0;
}

