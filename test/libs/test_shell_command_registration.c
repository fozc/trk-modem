/*
 * test_shell_command_registration.c
 *
 *  Created on: Oct 4, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Exercise the production shell command table at its boundaries.
 */
#include "unity.h"
#include "mock_bsp.h"

/* Include production code to reset its private table between tests. */
#include "../../Application/libs/shell.c"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static char command_names[SHELL_MAX_CMD_LIST_COUNT + 1U][4];
static uint32_t handler_calls;

static int handle_command(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    handler_calls++;
    return 17;
}

static int handle_replacement(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    return 23;
}

static shell_cmd_t make_command(size_t index)
{
    const shell_cmd_t command =
    {
        .cmd = command_names[index],
        .desc = "Boundary test command",
        .level = SHELL_LVL_USER,
        .func = handle_command
    };
    return command;
}

static void fill_table(void)
{
    for (size_t index = 0U; index < SHELL_MAX_CMD_LIST_COUNT; index++)
    {
        const shell_cmd_t command = make_command(index);
        TEST_ASSERT_EQUAL_UINT32(index, shell_register_command(&command));
    }
}

static void assert_command_executes(size_t index)
{
    char *arguments[] = {command_names[index]};
    TEST_ASSERT_EQUAL_INT(17, shell_command_executer(1, arguments));
}

void setUp(void)
{
    memset(cmd_list, 0, sizeof(cmd_list));
    cmd_list_counter = 0U;
    session_level = SHELL_LVL_USER;
    handler_calls = 0U;

    for (size_t index = 0U; index <= SHELL_MAX_CMD_LIST_COUNT; index++)
    {
        command_names[index][0] = 'c';
        /* Indices are 0..32, so each digit fits in char. */
        command_names[index][1] = (char)('0' + (index / 10U));
        command_names[index][2] = (char)('0' + (index % 10U));
        command_names[index][3] = '\0';
    }
}

void tearDown(void)
{
}

void test_full_table_rejects_new_command_and_preserves_existing_commands(void)
{
    fill_table();
    const shell_cmd_t extra = make_command(SHELL_MAX_CMD_LIST_COUNT);
    TEST_ASSERT_EQUAL_INT(-1, shell_register_command(&extra));
    TEST_ASSERT_EQUAL_UINT32(SHELL_MAX_CMD_LIST_COUNT, cmd_list_counter);
    TEST_ASSERT_EQUAL_INT(-1, shell_find_command(&extra));

    char *arguments[] = {extra.cmd};
    TEST_ASSERT_EQUAL_INT(-2, shell_command_executer(1, arguments));
    for (size_t index = 0U; index < SHELL_MAX_CMD_LIST_COUNT; index++)
    {
        assert_command_executes(index);
    }
    TEST_ASSERT_EQUAL_UINT32(SHELL_MAX_CMD_LIST_COUNT, handler_calls);
}

void test_duplicate_name_does_not_replace_handler_or_consume_slot(void)
{
    const shell_cmd_t original = make_command(0U);
    shell_cmd_t duplicate = original;
    duplicate.func = handle_replacement;

    TEST_ASSERT_EQUAL_INT(0, shell_register_command(&original));
    TEST_ASSERT_EQUAL_INT(-2, shell_register_command(&duplicate));
    TEST_ASSERT_EQUAL_UINT8(1U, cmd_list_counter);
    assert_command_executes(0U);
    TEST_ASSERT_EQUAL_UINT32(1U, handler_calls);

    const shell_cmd_t next = make_command(1U);
    TEST_ASSERT_EQUAL_INT(1, shell_register_command(&next));
    assert_command_executes(1U);
}

void test_removing_middle_command_from_full_table_allows_new_registration(void)
{
    fill_table();
    const shell_cmd_t removed = make_command(15U);
    TEST_ASSERT_EQUAL_INT(0, shell_unregister_command(&removed));
    TEST_ASSERT_EQUAL_UINT8(31U, cmd_list_counter);
    TEST_ASSERT_EQUAL_INT(-1, shell_find_command(&removed));

    char *arguments[] = {removed.cmd};
    TEST_ASSERT_EQUAL_INT(-2, shell_command_executer(1, arguments));
    for (size_t index = 0U; index < SHELL_MAX_CMD_LIST_COUNT; index++)
    {
        if (15U != index)
        {
            assert_command_executes(index);
        }
    }

    const shell_cmd_t extra = make_command(SHELL_MAX_CMD_LIST_COUNT);
    TEST_ASSERT_EQUAL_INT(31, shell_register_command(&extra));
    TEST_ASSERT_EQUAL_UINT8(32U, cmd_list_counter);
    assert_command_executes(SHELL_MAX_CMD_LIST_COUNT);
    TEST_ASSERT_EQUAL_UINT32(32U, handler_calls);
}

void test_removing_last_command_allows_same_name_to_be_registered_again(void)
{
    fill_table();
    const shell_cmd_t last = make_command(31U);
    TEST_ASSERT_EQUAL_INT(0, shell_unregister_command(&last));
    TEST_ASSERT_EQUAL_INT(-2, shell_unregister_command(&last));
    TEST_ASSERT_EQUAL_UINT8(31U, cmd_list_counter);
    TEST_ASSERT_EQUAL_INT(31, shell_register_command(&last));
    assert_command_executes(31U);

    const shell_cmd_t extra = make_command(SHELL_MAX_CMD_LIST_COUNT);
    TEST_ASSERT_EQUAL_INT(-1, shell_register_command(&extra));
    TEST_ASSERT_EQUAL_UINT8(32U, cmd_list_counter);
}

/*** end of file ***/
