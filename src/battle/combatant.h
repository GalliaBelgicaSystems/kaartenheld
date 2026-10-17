#ifndef COMBATANT_H
#define COMBATANT_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t hp;
    uint8_t max_hp;
    char name[13];   /* battle display name, up to 12 chars + NUL
                      * ("KNIGHT SLIME"); row-0 banners fit 20 cols */
} Combatant;

void combatant_take_damage(Combatant *c, uint8_t damage);

#endif /* COMBATANT_H */
