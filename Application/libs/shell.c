/*
 * shell.c
 *
 *  Created on: Oct 6, 2022
 *      Author: fatih
 */
#include "shell.h"
#include "string.h"
#include "bsp.h"
#include "contiki.h"
#include "contiki_process.h"

#include "utils.h"
#include "shell_complete.h"

enum
{
	UP_ARROW = 1,
	DOWN_ARROW
};

#define SHELL_ENTER_CODE '\r'
#define SHELL_BACKSPACE_CODE '\x08'
#define SHELL_TAB_CODE '\t'
#define SHELL_DEFAULT_PROMPT "> \r\n"
#define SHELL_DEFAULT_PASSWORD "admin"

#define SHELL_TEXT_MAXLEN 64
#define SHELL_TEXT_MAXARGS 8

#define SHELL_MAX_CMD_LIST_COUNT 24

#ifdef SHELL_HISTORY
typedef struct
{
	uint8_t buff[SHELL_TEXT_MAXLEN];
	uint8_t len;
}shell_historical_cmd_buff_t;

static shell_history_ctx_t *sh_history;

#endif

static char prompt_buff[16] = {0};
static char admin_pass[16] = {0};

static uint8_t session_level = SHELL_LVL_USER;
static shell_cmd_t cmd_list[SHELL_MAX_CMD_LIST_COUNT] = {0};
static uint8_t cmd_list_counter = 0;

static uint8_t rx_buff[SHELL_TEXT_MAXLEN];

static uint32_t rx_idx = 0;
static uint32_t rx_state = 1;

static uint8_t special_chr = 0;
static uint8_t rx_ready = 0;
static uint8_t rx_special_ready = 0;
static shell_putchar_fn_t shell_putchar_fn = NULL;

PROCESS(shell_task, "shell_task");
static process_event_t cmd_spcl_rcvd_event; /* A special character received (like up arrow, etc)*/
static process_event_t cmd_rcvd_event;      /* A line received from UART*/

void shell_set_putchar(shell_putchar_fn_t fn)
{
	shell_putchar_fn = fn;
}

shell_putchar_fn_t shell_get_putchar(void)
{
	return shell_putchar_fn;
}

void shell_putchr(int chr)
{
	if(shell_putchar_fn) {
		shell_putchar_fn(chr);
	} 
}

static inline void shell_puts(const char *buff)
{
	while(*buff){
		bsp_putchr(*buff++);
	}
}

int shell_rx_special_ready(void)
{
	return rx_special_ready;
}

void shell_clear_rx_special_ready(void)
{
	rx_special_ready = 0;
}

int shell_rx_ready(void)
{
	return rx_ready;
}

void shell_clear_rx_ready(void)
{
	rx_ready = 0;
}


/**
 * @brief Search the command.
 * @param cmd
 * @return -1   : not found
 *         >= 0 : cmd index
 *
 */
int shell_find_command(const shell_cmd_t *cmd)
{
	if(!cmd || !cmd->cmd){
		return -1;
	}

	for(int i = 0; i < cmd_list_counter; i++)
	{
		if(cmd_list[i].cmd && !strcmp(cmd_list[i].cmd, cmd->cmd))
		{
			return i; /* return index of cmd */
		}
	}

	return -1;
}

int shell_register_command(const shell_cmd_t *cmd)
{
	if(!cmd || (cmd_list_counter >= SHELL_MAX_CMD_LIST_COUNT))
	{
		SHELL_LOG("Shell Command registration failed!\r\n");
		return -1;
	}

	if(shell_find_command(cmd) >= 0) /* Is cmd already in the list? */
	{
		SHELL_LOG("Shell Command already registered!\r\n");
		return -2;
	}

	cmd_list[cmd_list_counter] = *cmd;

	return cmd_list_counter++;
}

int shell_unregister_command(const shell_cmd_t *cmd)
{
	if(!cmd){
		return -1;
	}

	int idx = shell_find_command(cmd);
	if(idx < 0){
		return -2; /* Command not found */
	}

	/* Shift remaining commands to fill the gap */
	for(int i = idx; i < cmd_list_counter - 1; i++){
		cmd_list[i] = cmd_list[i + 1];
	}

	/* Clear the last slot and decrement counter */
	cmd_list[cmd_list_counter - 1] = (shell_cmd_t){0};
	cmd_list_counter--;

	return 0;
}

static inline int is_delimiter(uint8_t c)
{
	return (((c) == '\r') || ((c) == '\n') || ((c) == '\t') || ((c) == '\0') || ((c) == ' '));
}

/**
 * @brief Get the sentence count of the given text string.
 * @param str A text string.
 * @return Count of the given sentence.
 */
static int get_count(const char *str)
{
    int cnt = 0;
    int wc = 0;
    char *p = (char *)str;
    while (*p)
    {
        if (!is_delimiter(*p))
        {
            wc++;
            if (wc == 1)
            {
                cnt++;
            }
        }
        else
        {
            wc = 0;
        }
        p++;
    }
    return cnt;
}

/**
 * @brief Get the sentence of the given text string.
 * @param str A text string.
 * @param n Index number. (0 to ntopt-get_count(str) - 1)
 * @param buf The pointer to a stored buffer.
 * @param siz The size of the stored buffer.
 * @param len The stored string length.
 * @retval !NULL Success. The pointer to the buffer.
 * @retval NULL Failure.
 */
static char *get_text(const char *str, const int n, char *buf, int siz, int *len)
{
    int cnt = 0;
    int wc = 0;
    char *p = (char *)str;
    *len = 0;

    while (*p)
    {
        if (!is_delimiter(*p))
        {
            wc++;
            if (wc == 1)
            {
                if (cnt == n)
                {
                    char *des = buf;
                    int cc = 0;
                    while (!is_delimiter(*p))
                    {
                        cc++;
                        if (siz <= cc)
                        {
                            break;
                        }
                        *des = *p;
                        des++;
                        p++;
                    }
                    *des = '\0';
                    *len = cc;
                    return buf;
                }
                cnt++;
            }
        }
        else
        {
            wc = 0;
        }
        p++;
    }
    return (char *)0;
}

int shell_command_executer(int argc, char *argv[])
{
    if (argc == 0)
    {
        return -1;
    }

    const shell_cmd_t *p = &cmd_list[0];
    for (int i = 0; i < cmd_list_counter; i++)
    {
        if (strcmp((const char *)argv[0], p->cmd) == 0)
        {
        	if(argc >= 2 && (!memcmp(argv[1], "help", 4) ||
        			!memcmp(argv[1], "-h", 3) ||
					!memcmp(argv[1], "?", 1)))
        	{
        		SHELL_LOG("%s\t:%s\r\n", p->cmd, p->desc);
        		return 0;
        	}

			if(p->level > session_level){
				SHELL_CLOG(XCOLOR_RED, "Permission denied\r\n");
				return -3;
			}

            return p->func(argc, argv);
        }
        p++;
    }

    return -2;
}

void shell_on_command_received(const char *str)
{
    int argc;
    char argv[SHELL_TEXT_MAXLEN] = {0};
    char *argvp[SHELL_TEXT_MAXARGS] = {0};
    int i;
    int total;
    char *p;

    argc = get_count(str);
    if (SHELL_TEXT_MAXARGS <= argc)
    {
        argc = SHELL_TEXT_MAXARGS;
    }

    total = 0;
    p = &argv[0];
    for (i = 0; i < argc; i++)
    {
        int len;
        argvp[i] = get_text(str, i, p, SHELL_TEXT_MAXLEN - total, &len);
        if (total + len + 1 < SHELL_TEXT_MAXLEN)
        {
            p += len + 1;
            total += len + 1;
        }
        else
        {
            break;
        }
    }

    int res = shell_command_executer(argc, &argvp[0]);

    if(res == -2)
    {
    	shell_puts("Command not found\r\n");
    }
}

static void shell_reset_rx(void)
{
	rx_idx = 0;
	rx_state = 1;
	rx_ready = 0;
}


void shell_on_rx_received(int chr)
{
	static uint32_t esc_seq = 0;

	if(rx_state)
	{
		if(chr == 0x03) /* ctrl + c, ^C, ETX*/
		{
			rx_idx = 0;
			return;
		}
		if(chr == 0x1B) /* escape received, we should get an escape sequence. */
		{
			//TODO: start timeouttimer
			esc_seq = 1;
		}

		if(esc_seq)
		{
			switch(esc_seq)
			{
			case 1:
				esc_seq++; /* get '[' chr. */
				break;
			case 2:
				if(chr == '[')
				{
					esc_seq++;
				}
				else
				{
					esc_seq = 0;
				}
				break;
			case 3:
				if(chr == 'A')
				{
					special_chr = UP_ARROW;
				}
				else if(chr == 'B')
				{
					special_chr = DOWN_ARROW;
				}

				rx_buff[rx_idx] = 0;

				process_poll(&shell_task);
				rx_special_ready = 1;

				esc_seq = 0;
				break;
			}
		}
		else
		{
			if(rx_idx < SHELL_TEXT_MAXLEN)
			{
				//TODO: start timeouttimer
				rx_buff[rx_idx++] = (uint8_t)chr;
			}
			if(chr == SHELL_ENTER_CODE) /* enter key, we should receive a full command text. */
			{
				rx_state = 0;
				rx_ready = 1;
				rx_buff[rx_idx] = 0;

#ifdef SHELL_TAB_COMPLETION
				shell_complete_reset();
#endif
				process_poll(&shell_task);
			}
			else if(chr == SHELL_BACKSPACE_CODE)
			{
				rx_idx--; /* remove the backspace character */
				if(rx_idx > 0)
				{
					rx_idx--; /* remove the previous character */
				}
#ifdef SHELL_TAB_COMPLETION
				shell_complete_reset();
#endif
			}
#ifdef SHELL_TAB_COMPLETION
			else if(chr == SHELL_TAB_CODE)
			{
				rx_idx--; /* remove the tab character */
				shell_tab_complete(rx_buff, &rx_idx, SHELL_TEXT_MAXLEN);
			}
			else
			{
				/* Any other character resets cycle mode */
				shell_complete_reset();
			}
#endif
		}
	}
}

void shell_process(void)
{
#ifdef SHELL_HISTORY
	if(shell_rx_special_ready())
	{
		shell_clear_rx_special_ready();
		switch(special_chr)
		{
		case UP_ARROW:
//			if(cmd_history_idx > 0)
//			{
//				if(cmd_history_user_idx > 0)
//				{
//					cmd_history_user_idx--;
//					cmd_execute_history = 1;
//
//					shell_puts((const char *)&shell_history[cmd_history_user_idx].buff);
//					shell_puts("\n");
//				}
//			}
			{
				const shell_history_cmd_t *cmd = shell_get_cmd(sh_history, SHELHISTORY_DIR_UP);
				SHELL_LOG( "SH:-> %s\r\n", cmd ? (const char *)cmd->buff : "");
			}

			break;
		case DOWN_ARROW:
//			if(cmd_history_user_idx < cmd_history_idx)
//			{
//				cmd_history_user_idx++;
//				cmd_execute_history = 1;
//				shell_puts((const char *)&shell_history[cmd_history_user_idx].buff);
//				shell_puts("\n");
//			}
			{
				const shell_history_cmd_t *cmd = shell_get_cmd(sh_history, SHELHISTORY_DIR_DOWN);
				SHELL_LOG( "SH:-> %s\r\n", cmd ? (const char *)cmd->buff : "");
			}
			break;
		}
	}
#endif

	if(shell_rx_ready())
	{
		if((rx_idx == 1 && rx_buff[0] == SHELL_ENTER_CODE))
		{
//			if(cmd_execute_history)
//			{
//				memcpy(rx_buff, shell_history[cmd_history_idx].buff, shell_history[cmd_history_idx].len);
//			}
//			else
			{
				shell_puts(prompt_buff);
				shell_reset_rx();
				return;
			}
		}

		shell_on_command_received((const char *)rx_buff);
//		if(cmd_execute_history)
//		{
//			cmd_execute_history = 0;
//		}
//		else
		{
#ifdef SHELL_HISTOR
			shell_history_add(sh_history, rx_buff, rx_idx);
#endif
		}
		shell_reset_rx();
	}
}

static int shell_su(int argc, char *argv[])
{
	(void)argc;

	if(argc < 2)
	{
		SHELL_LOG( "Usage: su <password>\r\n");
		return -1;
	}

	if(strncmp(argv[1], admin_pass, (sizeof(admin_pass) - 1)) == 0)
	{
		//TODO: start session timeout timer
		session_level = SHELL_LVL_SUPER_USER;
	}
	else
	{
		SHELL_LOG( "Authentication failed\r\n");
	}

    return 0;
}

static int shell_whoami(int argc, char *argv[])
{
	(void)argc;
	(void)argv;

	SHELL_LOG( "%s\r\n", session_level == SHELL_LVL_SUPER_USER ? "root" : "user");
    return 0;
}

static int shell_exit(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	session_level = SHELL_LVL_USER;
    return 0;
}

static int shell_reset(int argc, char *argv[])
{
	(void)argc;
	(void)argv;

	bsp_system_reset();

    return 0;
}

static int shell_help(int argc, char *argv[])
{
	(void)argc;
	(void)argv;

    const shell_cmd_t *p = &cmd_list[0];
    for (int i = 0; i < cmd_list_counter; i++)
    {
    	SHELL_LOG("%s\t:%s\r\n", p->cmd, p->desc);
        p++;
    }
    return 0;
}
 
static int shell_usrcmd_info(int argc, char *argv[])
{
	(void)argc;
	(void)argv;

	SHELL_LOG( "[---- ----]\r\n");
	SHELL_LOG( " ***\r\n");
	SHELL_LOG( " ***\r\n");
	SHELL_LOG( "Run Time: %u sec\r\n", bsp_get_run_time() / 1000);

    return 0;
}

static int shell_ps(int argc, char *argv[])
{
	(void)argv;
	(void)argc;

	extern struct process *process_list;
    struct process *q;
    int i = 0;

    SHELL_LOG("%-10s %-16s\n", "No", "Name");
    SHELL_LOG("----------------------------------------\n");

	for(q = process_list; q != NULL; q = q->next)
	{
		if(q->state && q->thread != NULL)
		{
			//xprintf("Task: %s\r\n", q->name);
			SHELL_LOG("%02d        %-24.24s\n", ++i, q->name);

		}
	}

	return 0;
}

static int shell_exec(int argc, char *argv[])
{
	(void)argc;

	extern struct process *process_list;
	struct process *q;

	for(q = process_list; q != NULL; q = q->next)
	{
		 if(strcmp(argv[1], q->name) == 0 && q->thread != NULL)
		{
			 if(!q->state)
			 {
				 process_start(q, NULL);
				 SHELL_LOG("Process started [%s]", q->name);
			 }
			 else
			 {
				 process_start(q, NULL);
				 SHELL_LOG("Process restarted [%s]", q->name);
			 }

			 return 0;
		}
	}

	SHELL_LOG("exec: Process not found\r\n");

	return -1;
}

static int shell_killall(int argc, char *argv[])
{
	(void)argc;
	(void)argv;

	extern struct process *process_list;
	struct process *q;

	for(q = process_list; q != NULL; q = q->next)
	{
		 if(q->state && q->thread != NULL &&
			strcmp(q->name, "wdt") != 0 &&
			strcmp(q->name, "Event timer") != 0)
		{
			 SHELL_LOG("Process killing [%s]\n", q->name);
			 process_exit(q);
		}
	}

	return 0;
}

static int shell_kill(int argc, char *argv[])
{
	(void) argc;

	extern struct process *process_list;
	struct process *q;

	if (argc < 2)
	{
		SHELL_LOG("kill: Name can not be empty!\r\n");
		return -1;
	}

	for (q = process_list; q != NULL; q = q->next)
	{
		if (q->state && q->thread != NULL
			&& strcmp(argv[1], q->name) == 0)
		{
			SHELL_LOG("Process killing [%s]\n", q->name);
			process_exit(q);
			return 0;
		}
	}

	SHELL_LOG("kill: No such process\r\n");
	return -1;
}


static int shell_terminal_char_set_test(int argc, char *argv[])
{
	(void)argc;

	if(argc < 2)
	{
		SHELL_LOG("Usage: term test\r\n");
		return -1;
	}

	if(strcmp(argv[1], "test") == 0)
	{
		for(int i = 127; i < 255; i++)
		{
			shell_putchr('0' + i / 100);
			shell_putchr('0' + (i / 100) / 10);
			shell_putchr('0' + i % 10);
			shell_putchr('-');
			shell_putchr(i);
			shell_puts("\r\n");
		}
	}

	return 0;
}

PROCESS_THREAD(shell_task, ev, data)
{
	(void)ev;
	(void)data;

	PROCESS_BEGIN();

	while(1)
	{
		PROCESS_WAIT_EVENT_UNTIL(rx_ready || rx_special_ready);
		shell_process();
	}
	PROCESS_END();
}


void shell_init(const char *prompt, const char *password, shell_putchar_fn_t putchar_fn)
{
	shell_putchar_fn = putchar_fn;

	if(!prompt){
		strncpy(prompt_buff, SHELL_DEFAULT_PROMPT, sizeof(prompt_buff) - 1);
	}else{
		strncpy(prompt_buff, prompt, sizeof(prompt_buff) - 1);
	}

	if(!password){
		strncpy(admin_pass, SHELL_DEFAULT_PASSWORD, sizeof(admin_pass) - 1);
	}else{
		strncpy(admin_pass, password, sizeof(admin_pass) - 1);
	}

	shell_register_command(&(shell_cmd_t){.cmd = "reset",
										   .desc = "Software reset the MCU",
										   .level = SHELL_LVL_USER,
									       .func = shell_reset
	});
	shell_register_command(&(shell_cmd_t){.cmd = "help",
										   .desc = "This is a description text string for help command.",
										   .level = SHELL_LVL_USER,
									       .func = shell_help
	});
	shell_register_command(&(shell_cmd_t){.cmd = "su",
										   .desc = "su [password]",
										   .level = SHELL_LVL_USER,
									       .func = shell_su
	});
	shell_register_command(&(shell_cmd_t){.cmd = "whoami",
										   .desc = "Show active user level.",
										   .level = SHELL_LVL_USER,
									       .func = shell_whoami
	});
	shell_register_command(&(shell_cmd_t){.cmd = "exit",
										   .desc = "Logout from root.",
										   .level = SHELL_LVL_USER,
									       .func = shell_exit
	});
	shell_register_command(&(shell_cmd_t){.cmd = "info",
										   .desc = "This is a description text string for info command.",
										   .level = SHELL_LVL_USER,
									       .func = shell_usrcmd_info
	});
	shell_register_command(&(shell_cmd_t){.cmd = "term",
										   .desc = "Test terminal extendend ascii characters sets. [127-255]\r\n"
												   "Usage: term test",
										   .level = SHELL_LVL_SUPER_USER,
									       .func = shell_terminal_char_set_test
	});
	shell_register_command(&(shell_cmd_t){.cmd = "ps",
										   .desc = "View actie process list",
										   .level = SHELL_LVL_SUPER_USER,
									       .func = shell_ps
	});
	shell_register_command(&(shell_cmd_t){.cmd = "exec",
										   .desc = "Start a process.\r\n"
										   "\texec [process name]",
										   .level = SHELL_LVL_SUPER_USER,
									       .func = shell_exec
	});
	shell_register_command(&(shell_cmd_t){.cmd = "kill",
										   .desc = "Kill a running process.\r\n"
										   "\tkill [process name]"
										   "\tkill [all]",
										   .level = SHELL_LVL_SUPER_USER,
									       .func = shell_kill
	});
	shell_register_command(&(shell_cmd_t){.cmd = "killall",
										   .desc = "Kill all running process\r\n",
										   .level = SHELL_LVL_SUPER_USER,
									       .func = shell_killall
	});

	cmd_spcl_rcvd_event = process_alloc_event();
	cmd_rcvd_event = process_alloc_event();

	process_start(&shell_task, NULL);

#ifdef SHELL_HISTORY
	shell_history_new(&sh_history);
#endif

#ifdef SHELL_TAB_COMPLETION
	shell_complete_init(cmd_list, &cmd_list_counter, shell_putchr);
#endif
}

