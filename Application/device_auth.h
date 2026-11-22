/*
 * device_auth.h
 *
 *  Created on: Nov 22, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Parola dogrulama ve degistirme arayuzunu tanimlar.
 */
#ifndef DEVICE_AUTH_H
#define DEVICE_AUTH_H

#include "types.h"

#define DEVICE_AUTH_PASSWORD_CAPACITY 64U

bool device_auth_verify(auth_account_t account, const char *password);
bool device_auth_can_change(auth_account_t actor, auth_account_t target);
bool device_auth_set_password(auth_account_t account, const char *password);
uint32_t device_auth_revision(auth_account_t account);
void device_auth_clear(void *value, size_t length);

#endif
/*** end of file ***/
