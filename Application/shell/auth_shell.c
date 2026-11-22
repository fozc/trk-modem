/*
 * auth_shell.c
 *
 *  Created on: Nov 22, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * USB konsolunda giris ve parola degistirme adimlarini yurutur.
 */
#include "auth_shell.h"

#include "device_auth.h"
#include "shell.h"

#include <string.h>

static auth_account_t actor_account;
static auth_account_t target_account;
static uint32_t actor_revision;
static char new_password[DEVICE_AUTH_PASSWORD_CAPACITY];

static auth_account_t account_for_level(uint8_t level)
{
    return (level == SHELL_LVL_FACTORY) ? AUTH_ACCOUNT_FACTORY : AUTH_ACCOUNT_ADMIN;
}

static bool verify_level(uint8_t level, const char *password)
{
    return device_auth_verify(account_for_level(level), password);
}

static uint32_t level_revision(uint8_t level)
{
    return device_auth_revision(account_for_level(level));
}

static void confirm_password(const char *password, size_t length)
{
    const bool authorized = (shell_get_session_level() >= SHELL_LVL_SUPER_USER) &&
                            (actor_revision == device_auth_revision(actor_account));
    const bool match = (length == strlen(new_password)) &&
                       (memcmp(new_password, password, length) == 0);
    const bool saved = authorized && match && device_auth_set_password(target_account, new_password);
    device_auth_clear(new_password, sizeof(new_password));
    if (saved)
    {
        SHELL_LOG("Password changed\r\n");
    }
    else
    {
        SHELL_LOG("Password change failed\r\n");
    }
}

static void read_new_password(const char *password, size_t length)
{
    if ((length == 0U) || (length >= sizeof(new_password)) ||
        (length != strlen(password)) ||
        !shell_read_secret(confirm_password))
    {
        SHELL_LOG("Password change failed\r\n");
        return;
    }
    memcpy(new_password, password, length + 1U);
    SHELL_LOG("Repeat new password: ");
}

static void check_current_password(const char *password, size_t length)
{
    if ((length == strlen(password)) && device_auth_verify(actor_account, password) &&
        shell_read_secret(read_new_password))
    {
        SHELL_LOG("New password: ");
    }
    else
    {
        SHELL_LOG("Authentication failed\r\n");
    }
}

static int change_password(int argc, char *argv[])
{
    if ((argc != 2) ||
        ((strcmp(argv[1], "admin") != 0) && (strcmp(argv[1], "factory-admin") != 0)))
    {
        SHELL_LOG("Usage: passwd <admin|factory-admin>\r\n");
        return -1;
    }

    const auth_account_t actor = account_for_level(shell_get_session_level());
    const auth_account_t target = (strcmp(argv[1], "admin") == 0) ?
                                  AUTH_ACCOUNT_ADMIN : AUTH_ACCOUNT_FACTORY;
    if (!device_auth_can_change(actor, target))
    {
        SHELL_LOG("Permission denied\r\n");
        return -1;
    }
    if (!shell_read_secret(check_current_password))
    {
        SHELL_LOG("Use the web password form for remote changes\r\n");
        return -1;
    }

    actor_account = actor;
    target_account = target;
    actor_revision = device_auth_revision(actor);
    SHELL_LOG("Current password: ");
    return 0;
}

void auth_shell_init(void)
{
    shell_set_authenticator(verify_level, level_revision);
    shell_register_command(&(shell_cmd_t){
        .cmd = "passwd", .desc = "Change an account password\r\n\tpasswd <admin|factory-admin>",
        .level = SHELL_LVL_SUPER_USER, .func = change_password
    });
}
/*** end of file ***/
