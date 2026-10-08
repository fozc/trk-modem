/*
 * contiki_replay_scheduler.c
 *
 *  Created on: Oct 7, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Compile the unchanged scheduler for the host replay scenario only.
 */
/* The two existing vendor conversion findings remain excluded by A09.
 * Suppression covers these includes only, not the production replay. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include "../../contiki-kernel/sys/process.c"
#include "../../contiki-kernel/sys/etimer.c"
#include "../../contiki-kernel/sys/timer.c"
#pragma GCC diagnostic pop

/*** end of file ***/
