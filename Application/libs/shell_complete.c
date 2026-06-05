/*
 * shell_complete.c
 *
 * Tab completion module for shell
 *
 *  Created on: Dec 26, 2024
 *      Author: fatih
 */
#include "shell_complete.h"

#ifdef SHELL_TAB_COMPLETION
#include <string.h>

#define BELL_CHAR           '\x07'
#define MAX_MATCH_COUNT     16



/* Tab completion context */
static struct {
    const shell_cmd_t *command_list;        /* Registered shell commands */
    const uint8_t *command_count;           /* Number of registered commands */
    const shell_cmd_t *matched_commands[MAX_MATCH_COUNT]; /* Commands matching prefix */
    char saved_prefix[64];                  /* Original prefix for cycling */
    shell_putchar_fn_t putchar_fn;          /* Output function from shell.c */
    int matched_count;                      /* Number of matched commands */
    int saved_prefix_length;                /* Length of saved prefix */
    int current_match_index;                /* Current index in cycle mode (-1 = not cycling) */
} tab_ctx;

/*---------------------------------------------------------------------------*/
/**
 * @brief Output a character via configured putchar function
 */
static void output_char(int ch)
{
    if (tab_ctx.putchar_fn != NULL) {
        tab_ctx.putchar_fn(ch);
    }
}

/*---------------------------------------------------------------------------*/
/**
 * @brief Clear characters from terminal line
 * @param char_count Number of characters to clear
 */
static void terminal_clear_chars(int char_count)
{
#ifdef USE_VT100_SEQUENCES
    /* VT100: Move cursor left N chars, then erase to end of line */
    /* ESC[nD = move cursor left n columns */
    /* ESC[K  = erase from cursor to end of line */
    if (char_count > 0) {
        output_char('\x1B');
        output_char('[');
        /* Output number as ASCII digits */
        if (char_count >= 100) output_char('0' + (char_count / 100));
        if (char_count >= 10)  output_char('0' + (char_count / 10) % 10);
        output_char('0' + (char_count % 10));
        output_char('D');
        /* Erase to end of line */
        output_char('\x1B');
        output_char('[');
        output_char('K');
    }
#else
    /* Universal method: backspace + space + backspace */
    while (char_count-- > 0) 
    {
        output_char('\b'); 
        output_char(' '); 
        output_char('\b');
    }
#endif
}

/*---------------------------------------------------------------------------*/
/**
 * @brief Write string to buffer and echo to terminal
 * @param str String to write
 * @param buffer Destination buffer
 * @param buffer_index Pointer to current buffer index (updated)
 * @param buffer_max Maximum buffer size
 */
static void buffer_write_and_echo(const char *str, uint8_t *buffer, uint32_t *buffer_index, uint32_t buffer_max)
{
    while (*str && *buffer_index < buffer_max - 1) 
    {
        buffer[*buffer_index] = *str;
        output_char(*str++);
        (*buffer_index)++;
    }
}

/*---------------------------------------------------------------------------*/
/**
 * @brief Find all commands matching the given prefix
 * @param prefix Prefix string to match
 * @param prefix_length Length of prefix
 */
static void find_matching_commands(const char *prefix, int prefix_length)
{
    tab_ctx.matched_count = 0;
    
    for (int i = 0; i < *tab_ctx.command_count && tab_ctx.matched_count < MAX_MATCH_COUNT; i++) 
    {
        const char *cmd_name = tab_ctx.command_list[i].cmd;
        if (cmd_name && strncmp(cmd_name, prefix, prefix_length) == 0) 
        {
            tab_ctx.matched_commands[tab_ctx.matched_count++] = &tab_ctx.command_list[i];
        }
    }
}

/*---------------------------------------------------------------------------*/
void shell_complete_init(const shell_cmd_t *cmd_list, const uint8_t *cmd_count, shell_putchar_fn_t putchar_fn)
{
    tab_ctx.command_list = cmd_list;
    tab_ctx.command_count = cmd_count;
    tab_ctx.putchar_fn = putchar_fn;
    tab_ctx.current_match_index = -1;
}

/*---------------------------------------------------------------------------*/
void shell_complete_reset(void)
{
    tab_ctx.current_match_index = -1;
    tab_ctx.saved_prefix_length = 0;
}

/*---------------------------------------------------------------------------*/
void shell_tab_complete(uint8_t *input_buffer, uint32_t *input_length, uint32_t max_length)
{
    /* Safety check - ensure module is initialized */
    if (!tab_ctx.putchar_fn || !tab_ctx.command_list || !tab_ctx.command_count) 
    {
        return;
    }
    
    if (!input_buffer || !input_length || *input_length == 0) 
    {
        shell_complete_reset();
        return;
    }

    /* Extract current input as prefix (stop at first space) */
    char current_prefix[64];
    int current_prefix_length = 0;
    
    for (uint32_t i = 0; i < *input_length && i < sizeof(current_prefix) - 1; i++)
    {
        if (input_buffer[i] == ' ') 
        { 
            /* Space found - command already complete, don't process */
            shell_complete_reset(); 
            return; 
        }
        current_prefix[current_prefix_length++] = input_buffer[i];
    }
    current_prefix[current_prefix_length] = '\0';

    /* Check if we're cycling through previous matches */
    int is_cycling = (tab_ctx.current_match_index >= 0 && 
                      tab_ctx.saved_prefix_length > 0 &&
                      strncmp(tab_ctx.saved_prefix, current_prefix, tab_ctx.saved_prefix_length) == 0);

    if (is_cycling) 
    {
        /* Move to next match in cycle */
        tab_ctx.current_match_index = (tab_ctx.current_match_index + 1) % tab_ctx.matched_count;
    }
    else 
    {
        /* New search - find all matching commands */
        find_matching_commands(current_prefix, current_prefix_length);
        
        if (tab_ctx.matched_count > 1) 
        {
            /* Multiple matches - save state for cycling */
            strncpy(tab_ctx.saved_prefix, current_prefix, sizeof(tab_ctx.saved_prefix) - 1);
            tab_ctx.saved_prefix_length = current_prefix_length;
            tab_ctx.current_match_index = 0;
        }
        else 
        {
            shell_complete_reset();
        }
    }

    /* Process completion result */
    if (tab_ctx.matched_count == 0) 
    {
        /* No match - beep */
        output_char(BELL_CHAR);
        shell_complete_reset();
    } 
    else if (tab_ctx.matched_count == 1) 
    {
        /* Single match - auto-complete and add space */
        const char *completed_cmd = tab_ctx.matched_commands[0]->cmd;
        buffer_write_and_echo(completed_cmd + current_prefix_length, input_buffer, input_length, max_length);
        
        if (*input_length < max_length - 1) 
        {
            input_buffer[*input_length] = ' ';
            output_char(' ');
            (*input_length)++;
        }
        shell_complete_reset();
    } 
    else 
    {
        /* Multiple matches - cycle through them */
        const char *selected_cmd = tab_ctx.matched_commands[tab_ctx.current_match_index]->cmd;
        
        terminal_clear_chars(*input_length);
        *input_length = 0;
        buffer_write_and_echo(selected_cmd, input_buffer, input_length, max_length);
        input_buffer[*input_length] = '\0';
    }
}

#endif /* SHELL_TAB_COMPLETION */
