/*
 * bsp_rf_reset.h
 *
 *  Created on: Oct 26, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Drive the active-low MH reset through an open-drain output.
 */

#ifndef BSP_RF_RESET_H
#define BSP_RF_RESET_H

#include <stdbool.h>

/** Assert low with open drain, or release to the CubeMX input/high-Z state. */
void bsp_rf_reset(bool asserted);

#endif /* BSP_RF_RESET_H */

/*** end of file ***/
