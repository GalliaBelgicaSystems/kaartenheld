#ifndef INPUT_H
#define INPUT_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    INPUT_RIGHT,
    INPUT_LEFT,
    INPUT_UP,
    INPUT_DOWN,
    INPUT_A,
    INPUT_B,
    INPUT_SELECT,   /* bit 0x40 == J_SELECT */
    INPUT_START     /* bit 0x80 == J_START */
} InputButton;

extern volatile uint8_t g_inp_mask;

/* Button bit values in enum order (1 << InputButton).  Mirrors GBDK
 * joypad()'s J_* layout, enforced by a compile-time check in input.c.  The
 * harness reads this table to build its injection masks, so the injected
 * input path always matches the real joypad path. */
extern const uint8_t g_input_button_bits[8];

void input_init(void);
#ifdef DEBUG_BUILD
/* Scenario setup only (scenario_begin): the release ROM never resets input. */
void input_reset(void);
#endif
void input_update(void);
bool input_pressed(InputButton button);
bool input_held(InputButton button);
#endif /* INPUT_H */
