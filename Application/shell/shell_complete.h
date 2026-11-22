/*
 * shell_complete.h
 *
 * Tab completion module for shell
 * Enable with: #define SHELL_TAB_COMPLETION
 *
 *  Created on: Dec 26, 2024
 *      Author: fatih
 */

#ifndef LIBS_SHELL_COMPLETE_H_
#define LIBS_SHELL_COMPLETE_H_

#define SHELL_TAB_COMPLETION

#ifdef SHELL_TAB_COMPLETION

#include "shell.h"
#include <stdint.h>


#define USE_VT100_SEQUENCES


/**
 * @brief Initialize tab completion module
 * @param cmd_list Pointer to shell command list
 * @param cmd_count Pointer to command count variable
 * @param putchar_fn Output character function (from shell.c)
 */
void shell_complete_init(const shell_cmd_t *cmd_list, const uint8_t *cmd_count, shell_putchar_fn_t putchar_fn);

/**
 * @brief Reset tab completion cycle state
 * Call this when user types any character other than TAB
 */
void shell_complete_reset(void);

/**
 * @brief Process tab completion for current input buffer
 * @param rx_buff Current input buffer
 * @param rx_idx Pointer to current buffer index (will be updated)
 * @param max_len Maximum buffer length
 */
void shell_tab_complete(uint8_t *rx_buff, uint32_t *rx_idx, uint32_t max_len);

#endif /* SHELL_TAB_COMPLETION */

#endif /* LIBS_SHELL_COMPLETE_H_ */
