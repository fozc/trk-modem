/*
 * bsp_rf_reset.c
 *
 *  Created on: Oct 26, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Drive the active-low MH reset through an open-drain output.
 */

#include "bsp_rf_reset.h"
#include "main.h"

void bsp_rf_reset(bool asserted)
{
    if (asserted)
    {
        LL_GPIO_ResetOutputPin(RF_RESET_GPIO_Port, RF_RESET_Pin);
        LL_GPIO_SetPinOutputType(RF_RESET_GPIO_Port, RF_RESET_Pin, LL_GPIO_OUTPUT_OPENDRAIN);
        LL_GPIO_SetPinPull(RF_RESET_GPIO_Port, RF_RESET_Pin, LL_GPIO_PULL_NO);
        LL_GPIO_SetPinMode(RF_RESET_GPIO_Port, RF_RESET_Pin, LL_GPIO_MODE_OUTPUT);
    }
    else
    {
        LL_GPIO_SetPinMode(RF_RESET_GPIO_Port, RF_RESET_Pin, LL_GPIO_MODE_INPUT);
    }
}

/*** end of file ***/
