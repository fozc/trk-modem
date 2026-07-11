/*
 * reset_source.h
 *
 *  Created on: Jun 30, 2026
 *      Author: fatih.ozcan
 *
 * STM32 reset-cause detection.
 *
 * Reads the RCC clock control & status register (RCC->CSR) once at boot,
 * decodes it into the generic reset_source_flag_t bitmask, and clears the
 * flags so the next boot reports a fresh cause. The flag macros are guarded
 * with #ifdef so the same module builds across STM32 families that expose
 * different subsets of reset causes.
 *
 * Typical usage (call as early as possible at boot, e.g. start of main()):
 *
 *     reset_source_init();
 *     reset_source_print();
 */

#ifndef RESET_SOURCE_H
#define RESET_SOURCE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Generic reset-cause flags. A single boot may set more than one flag. */
typedef enum
{
    RESET_SOURCE_UNKNOWN      = 0x00000000, /* No known cause reported.        */
    RESET_SOURCE_POWER_ON     = 0x00000001, /* Power-on / power-down reset.    */
    RESET_SOURCE_BROWNOUT     = 0x00000002, /* Brown-out (supply collapse).    */
    RESET_SOURCE_EXTERNAL_PIN = 0x00000004, /* External NRST pin asserted.     */
    RESET_SOURCE_SOFTWARE     = 0x00000008, /* Software (e.g. NVIC) reset.     */
    RESET_SOURCE_IWDG         = 0x00000010, /* Independent watchdog timeout.   */
    RESET_SOURCE_WWDG         = 0x00000020, /* Window watchdog timeout.        */
    RESET_SOURCE_LOW_POWER    = 0x00000040, /* Illegal low-power mode entry.   */
    RESET_SOURCE_OPTION_BYTE  = 0x00000080, /* Option-byte loader reset.       */
    RESET_SOURCE_FIREWALL     = 0x00000100  /* Firewall reset.                 */
} reset_source_flag_t;

/*
 * @brief Capture and clear the hardware reset-cause register(s).
 *
 * Must be called once, as early as practical during boot. Subsequent query
 * functions operate on the snapshot taken here.
 */
void reset_source_init(void);

/*
 * @brief Get the decoded reset-cause flags captured by reset_source_init().
 *
 * @return Bitwise OR of reset_source_flag_t values, or RESET_SOURCE_UNKNOWN.
 */
reset_source_flag_t reset_source_get_flags(void);

/*
 * @brief Get the raw, platform-specific reset register value.
 *
 * @return The unmodified register snapshot (e.g. RCC->CSR on STM32).
 */
uint32_t reset_source_get_raw(void);

/*
 * @brief Test whether a specific reset cause was reported.
 *
 * @param[in] flag Single reset_source_flag_t value to test.
 *
 * @return true if the flag was set at the last boot, false otherwise.
 */
bool reset_source_has(reset_source_flag_t flag);

/*
 * @brief Log the captured reset cause via the project logger.
 *
 * Abnormal causes (brown-out, watchdog, low-power) are logged as a warning;
 * normal causes (power-on, pin) are logged at the default level.
 */
void reset_source_print(void);

/*
 * @brief Register the "rstsrc" shell command, which prints the captured reset
 *        cause. Call once after the shell has been initialised.
 */
void reset_source_shell_init(void);

#ifdef __cplusplus
}
#endif

#endif /* RESET_SOURCE_H */

/*** end of file ***/
