/*
 * test_coldboot_hang.c
 *
 *  Created on: Oct 31, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Exhausted recovery must stay terminal; the external runner bounds this process.
 */
#include "../../support/gsm_coldboot_fixture.inc"

int main(void)
{
    reset_ladder_state();
    gsm.reboot_counter = 2U;
    cold_boot_count = GSM_COLD_BOOT_MAX_ATTEMPTS;
    gsm_init_step_reset_module();
    return 1;
}
/*** end of file ***/
