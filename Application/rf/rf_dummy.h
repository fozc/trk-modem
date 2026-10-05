/*
 * rf_dummy.h
 *
 *  Created on: Oct 5, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Isolated synthetic R1 payload generation for explicit development tests.
 */

#ifndef RF_RF_DUMMY_H_
#define RF_RF_DUMMY_H_

#include "rf_scp_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

/* These functions never change configuration, ACK bindings or live state.
 * The application still leaves initialization disabled by default.
 */
void rf_dummy_init(void);
void rf_dummy_tick(void);
bool rf_dummy_get_live(uint8_t source, rf_scp_live_t *out);

#ifdef __cplusplus
}
#endif

#endif /* RF_RF_DUMMY_H_ */

/*** end of file ***/
