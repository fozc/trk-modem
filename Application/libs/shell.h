/*
 * shell.h
 *
 *  Created on: Oct 6, 2022
 *      Author: fatih
 * Shell module for receiving and executing text-based commands.
 * It supports user and super-user levels, command registration, and basic line editing.
 * The shell runs as a Contiki process and interacts with the UART via a user-provided putchar function.
 *
 * NOTE: this file is kept in sync with the bootloader repository's
 * shell.h/shell.c by hand.  Application builds define
 * SHELL_USER_CONTIKI_PROCESS as a build symbol; the bootloader leaves it
 * undefined and polls shell_process() from its main loop instead.
 */

#ifndef LIBS_SHELL_H_
#define LIBS_SHELL_H_

#include <stdint.h>
#include "xprintf.h"

#define SHELL_LVL_USER       0
#define SHELL_LVL_SUPER_USER 1

/* Build-mode switch: defined builds integrate with Contiki (process_poll /
 * shell_task); undefined builds are poll-based (bootloader main loop).
 * The application provides it via the build system (-D) so this header can
 * stay byte-identical across the two repositories. */
//#define SHELL_USER_CONTIKI_PROCESS

/* Default built-in command enable/disable switches.
 * Comment out any line to exclude that command from the build. */
#define SHELL_CMD_RESET_ENABLED
#define SHELL_CMD_HELP_ENABLED
#define SHELL_CMD_SU_ENABLED
#define SHELL_CMD_WHOAMI_ENABLED
#define SHELL_CMD_EXIT_ENABLED
#define SHELL_CMD_INFO_ENABLED
#define SHELL_CMD_TERM_ENABLED


typedef int (*shell_cmd_handler_t)(int argc, char *argv[]);
typedef void (*shell_putchar_fn_t)(int ch);

typedef struct
{
    char *cmd;
    char *desc;
    uint8_t level;
    shell_cmd_handler_t func;
}shell_cmd_t;

#ifdef NO_SHELL_LOG
	#define  DISABLE_SHELL_LOG
#endif


#ifndef   DISABLE_SHELL_LOG
	#define SHELL_LOG(a...) \
		do{ \
			xfprintf(shell_putchr, a); \
		}while(0)
	#define SHELL_CLOG(color, a...) \
			do{ \
			xfprintf(shell_putchr, a); \
			}while(0)
#else
	#define SHELL_LOG(a...)
	#define SHELL_CLOG(color, a...)
#endif

void shell_init(const char *prompt, const char *password, shell_putchar_fn_t putchar_fn);
void shell_set_putchar(shell_putchar_fn_t fn);
shell_putchar_fn_t shell_get_putchar(void);
void shell_putchr(int ch);
int shell_register_command(const shell_cmd_t *cmd);
int shell_unregister_command(const shell_cmd_t *cmd);
void shell_on_rx_received(int chr);
void shell_process(void);
uint8_t shell_get_session_level(void);

#endif /* LIBS_SHELL_H_ */
