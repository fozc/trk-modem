/*
 * reset_source.c
 *
 *  Created on: Jun 30, 2026
 *      Author: fatih.ozcan
 *
 * STM32 reset-cause detection. Reads the RCC clock control & status register
 * (RCC->CSR) once at boot, decodes it into generic reset_source_flag_t flags,
 * and clears the flags via the remove-flag (RMVF) bit. Individual flag macros
 * are #ifdef-guarded so the same file builds across STM32 families.
 */

#include "reset_source.h"

#include "main.h"  /* CMSIS device header: RCC + RCC_CSR_* flag macros. */
#include "bsp.h"   /* CSLOG logging macros. */
#include "shell.h" /* shell_register_command, shell_cmd_t. */

/* Maps a generic reset flag to its short textual token, used by the logger. */
typedef struct
{
    uint32_t     flag;
    const char * p_name;
} reset_source_name_map_t;

static const reset_source_name_map_t s_name_map[] =
{
    { (uint32_t)RESET_SOURCE_IWDG,         "IWDG" },
    { (uint32_t)RESET_SOURCE_WWDG,         "WWDG" },
    { (uint32_t)RESET_SOURCE_BROWNOUT,     "BOR"  },
    { (uint32_t)RESET_SOURCE_LOW_POWER,    "LPWR" },
    { (uint32_t)RESET_SOURCE_SOFTWARE,     "SFT"  },
    { (uint32_t)RESET_SOURCE_OPTION_BYTE,  "OBL"  },
    { (uint32_t)RESET_SOURCE_FIREWALL,     "FW"   },
    { (uint32_t)RESET_SOURCE_EXTERNAL_PIN, "PIN"  },
    { (uint32_t)RESET_SOURCE_POWER_ON,     "POR"  }
};

#define RESET_SOURCE_NAME_MAP_COUNT  (sizeof(s_name_map) / sizeof(s_name_map[0]))

/* Causes considered abnormal (fault-class) rather than a normal boot. */
#define RESET_SOURCE_ABNORMAL_MASK                                  \
    ((uint32_t)RESET_SOURCE_BROWNOUT  | (uint32_t)RESET_SOURCE_IWDG | \
     (uint32_t)RESET_SOURCE_WWDG      | (uint32_t)RESET_SOURCE_LOW_POWER)

static uint32_t s_flags = (uint32_t)RESET_SOURCE_UNKNOWN;
static uint32_t s_raw   = 0u;

void reset_source_init(void)
{
    uint32_t csr   = RCC->CSR;
    uint32_t flags = (uint32_t)RESET_SOURCE_UNKNOWN;

    s_raw = csr;

#ifdef RCC_CSR_PORRSTF
    if ((csr & RCC_CSR_PORRSTF) != 0u)
    {
        flags |= (uint32_t)RESET_SOURCE_POWER_ON;
    }
#endif

#ifdef RCC_CSR_BORRSTF
    if ((csr & RCC_CSR_BORRSTF) != 0u)
    {
        flags |= (uint32_t)RESET_SOURCE_BROWNOUT;
    }
#endif

#ifdef RCC_CSR_PINRSTF
    if ((csr & RCC_CSR_PINRSTF) != 0u)
    {
        flags |= (uint32_t)RESET_SOURCE_EXTERNAL_PIN;
    }
#endif

#ifdef RCC_CSR_SFTRSTF
    if ((csr & RCC_CSR_SFTRSTF) != 0u)
    {
        flags |= (uint32_t)RESET_SOURCE_SOFTWARE;
    }
#endif

#ifdef RCC_CSR_IWDGRSTF
    if ((csr & RCC_CSR_IWDGRSTF) != 0u)
    {
        flags |= (uint32_t)RESET_SOURCE_IWDG;
    }
#endif

#ifdef RCC_CSR_WWDGRSTF
    if ((csr & RCC_CSR_WWDGRSTF) != 0u)
    {
        flags |= (uint32_t)RESET_SOURCE_WWDG;
    }
#endif

#ifdef RCC_CSR_LPWRRSTF
    if ((csr & RCC_CSR_LPWRRSTF) != 0u)
    {
        flags |= (uint32_t)RESET_SOURCE_LOW_POWER;
    }
#endif

#ifdef RCC_CSR_OBLRSTF
    if ((csr & RCC_CSR_OBLRSTF) != 0u)
    {
        flags |= (uint32_t)RESET_SOURCE_OPTION_BYTE;
    }
#endif

#ifdef RCC_CSR_FWRSTF
    if ((csr & RCC_CSR_FWRSTF) != 0u)
    {
        flags |= (uint32_t)RESET_SOURCE_FIREWALL;
    }
#endif

    s_flags = flags;

    /* Clear the reset flags so the next boot reports a fresh cause. */
#ifdef RCC_CSR_RMVF
    RCC->CSR |= RCC_CSR_RMVF;
#endif
}

reset_source_flag_t reset_source_get_flags(void)
{
    return (reset_source_flag_t)s_flags;
}

uint32_t reset_source_get_raw(void)
{
    return s_raw;
}

bool reset_source_has(reset_source_flag_t flag)
{
    return ((s_flags & (uint32_t)flag) != 0u);
}

/* ===========================================================================
 *  Logging
 * =========================================================================*/

void reset_source_print(void)
{
    CSLOG("Reset cause (CSR=0x%08lX):\r\n", (unsigned long)s_raw);

    if (s_flags == (uint32_t)RESET_SOURCE_UNKNOWN)
    {
        CSLOG("  UNKNOWN\r\n");
        return;
    }

    for (size_t i = 0u; i < RESET_SOURCE_NAME_MAP_COUNT; i++)
    {
        if ((s_flags & s_name_map[i].flag) != 0u)
        {
            if ((s_name_map[i].flag & RESET_SOURCE_ABNORMAL_MASK) != 0u)
            {
                CSLOG_WARN("  %s\r\n", s_name_map[i].p_name);
            }
            else
            {
                CSLOG("  %s\r\n", s_name_map[i].p_name);
            }
        }
    }
}

/* ===========================================================================
 *  Shell integration
 * =========================================================================*/

/*
 * @brief Shell handler: print the reset cause captured at boot. Takes no
 *        arguments. Usage: "rstsrc".
 */
static int reset_source_shell_handler(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    reset_source_print();

    return 0;
}

void reset_source_shell_init(void)
{
    shell_register_command(&(shell_cmd_t){.cmd   = "rstsrc",
                                          .desc  = "Show the last MCU reset cause\r\n",
                                          .level = SHELL_LVL_USER,
                                          .func  = reset_source_shell_handler});
}

/*** end of file ***/
