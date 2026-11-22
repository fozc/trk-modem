/*
 * device_auth.c
 *
 *  Created on: Nov 22, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Parola dogrulama ve degistirme islemlerini yurutur.
 */
#include "device_auth.h"

#include "bsp_random.h"
#include "hmac_sha256.h"
#include "nvram.h"

#include <string.h>

#if defined(UNIT_TEST)
#include "auth_defaults_fixture.h"
#else
#include "auth_defaults_gen.h"
#endif

void device_auth_clear(void *value, size_t length)
{
    volatile uint8_t *bytes = value;
    for (size_t index = 0U; index < length; index++)
    {
        bytes[index] = 0U;
    }
}

static bool password_valid(const char *password)
{
    if ((password == NULL) || (password[0] == '\0'))
    {
        return false;
    }

    for (size_t index = 0U; index < DEVICE_AUTH_PASSWORD_CAPACITY; index++)
    {
        const uint8_t byte = (uint8_t)password[index];
        if (byte == 0U)
        {
            return true;
        }
        if ((byte < 0x20U) || (byte == 0x7FU))
        {
            return false;
        }
    }
    return false;
}

bool device_auth_verify(auth_account_t account, const char *password)
{
    const auth_record_t *record = nvram_get_auth_record(account);
    uint8_t digest[32U];
    uint8_t difference = 0U;

    if ((record == NULL) || !password_valid(password) ||
        !pbkdf2_hmac_sha256(digest, (const uint8_t *)password, strlen(password),
                           record->salt, AUTH_DEFAULT_ITERATIONS))
    {
        return false;
    }

    for (size_t index = 0U; index < sizeof(digest); index++)
    {
        difference |= (digest[index] ^ record->digest[index]);
    }
    device_auth_clear(digest, sizeof(digest));
    return difference == 0U;
}

bool device_auth_can_change(auth_account_t actor, auth_account_t target)
{
    return ((uint32_t)target < (uint32_t)AUTH_ACCOUNT_COUNT) &&
           ((actor == target) || (actor == AUTH_ACCOUNT_FACTORY));
}

bool device_auth_set_password(auth_account_t account, const char *password)
{
    auth_record_t record;

    if (((uint32_t)account >= (uint32_t)AUTH_ACCOUNT_COUNT) || !password_valid(password))
    {
        return false;
    }

    for (size_t offset = 0U; offset < sizeof(record.salt); offset += sizeof(uint32_t))
    {
        uint32_t word;
        if (!bsp_random_secure_word(&word))
        {
            return false;
        }
        memcpy(&record.salt[offset], &word, sizeof(word));
    }

    return pbkdf2_hmac_sha256(record.digest, (const uint8_t *)password, strlen(password),
                            record.salt, AUTH_DEFAULT_ITERATIONS) &&
           nvram_save_auth_record(account, &record);
}

uint32_t device_auth_revision(auth_account_t account)
{
    return nvram_auth_revision(account);
}
/*** end of file ***/
