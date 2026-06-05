/*
 * gsm_init.h
 *
 * GSM modem initialization state machine.
 */

#ifndef GSM_INIT_H
#define GSM_INIT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "gsm_process.h"

/* Init state / vector management */
void             gsm_load_common_init_vector(void);
void             gsm_load_LE910R1_init_vector(void);
void             gsm_set_init_state(gsm_init_state_t state);
gsm_init_state_t gsm_get_init_state(void);
void             gsm_reset_init_vector(void);
void             gsm_init_next_step(void);
void             gsm_init_set_step_old(uint32_t step);

/* Main init dispatch — called every gsm_process cycle in init modes */
void gsm_init_old(void);

#ifdef __cplusplus
}
#endif

#endif /* GSM_INIT_H */
