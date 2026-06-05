/*
 * config_manager.c
 *
 *  Created on: Feb 1, 2026
 *      Author: fatih
 */
#include <string.h>
#include "modem_config.h"
#include "nvram.h"
#include "bsp.h"

#define modem_config (nvram_get_modem_config_rw())

int modem_config_sync(void)
{
	return nvram_sync(false);
}

const modem_config_t* modem_config_get(void)
{
	return nvram_get_modem_config();
}

void modem_config_set(const modem_config_t* config)
{
	if(config == NULL) {
		return;
	}

	modem_config_set_web_port(config->web_interface_port);
	modem_config_set_sim_pin(config->sim_card_pin);
	modem_config_set_sim_apn(config->apn.apn);
	modem_config_set_sim_apn_username(config->apn.user_name);
	modem_config_set_sim_apn_password(config->apn.user_pass);
	modem_config_set_ntp_server(config->ntp_server);
	modem_config_set_ntp_port(config->ntp_server_port);
	modem_config_set_timezone(config->time_zone);
	modem_config_set_reset_period(config->periodic_modem_reset_period);
}

bool modem_config_is_ntp_active(void)
{
	bool ntp_server_valid = false;

	for(int i = 0; i < MAX_NTP_SERVER_LEN - 1; i++) 
	{
		if(modem_config->ntp_server[i] != '\0' &&
		   modem_config->ntp_server[i + 1] == '\0') {
			ntp_server_valid = true;
			break;
		}
	}

	return ntp_server_valid;
}

// serial_number (Read-Only)
uint32_t modem_config_get_serial_number(void)
{
	return modem_config->serial_number;
}

void modem_config_set_serial_number(uint32_t serial_number)
{
	modem_config->serial_number = serial_number;
}

// web_interface_port (Read-Write)
uint16_t modem_config_get_web_port(void)
{
	return modem_config->web_interface_port;
}

void modem_config_set_web_port(uint16_t port)
{
	modem_config->web_interface_port = port;
}

// sim_card_pin (Read-Write)
uint16_t modem_config_get_sim_pin(void)
{
	return modem_config->sim_card_pin;
}

void modem_config_set_sim_pin(uint16_t pin)
{
	modem_config->sim_card_pin = pin;
}

// sim_card_apn (Read-Write)
const char* modem_config_get_sim_apn(void)
{
	return modem_config->apn.apn;
}

void modem_config_set_sim_apn(const char* apn)
{
	if(apn) {
		strncpy(modem_config->apn.apn, apn, MAX_APN_LEN - 1);
		modem_config->apn.apn[MAX_APN_LEN - 1] = '\0';
	}
}

const char* modem_config_get_sim_apn_username(void)
{
	return modem_config->apn.user_name;
}

void modem_config_set_sim_apn_username(const char* username)
{
	if(username) {
		strncpy(modem_config->apn.user_name, username, MAX_APN_USER_NAME_LEN - 1);
		modem_config->apn.user_name[MAX_APN_USER_NAME_LEN - 1] = '\0';
	}
}

// sim_card_apn_password (Read-Write)
const char* modem_config_get_sim_apn_password(void)
{
	return modem_config->apn.user_pass;
}

void modem_config_set_sim_apn_password(const char* password)
{
	if(password) {
		strncpy(modem_config->apn.user_pass, password, MAX_APN_PASSWORD_LEN - 1);
		modem_config->apn.user_pass[MAX_APN_PASSWORD_LEN - 1] = '\0';
	}
}

// ntp_server (Read-Write)
const char* modem_config_get_ntp_server(void)
{
	return modem_config->ntp_server;
}

void modem_config_set_ntp_server(const char* server)
{
	if(server) {
		strncpy(modem_config->ntp_server, server, MAX_NTP_SERVER_LEN - 1);
		modem_config->ntp_server[MAX_NTP_SERVER_LEN - 1] = '\0';
	}
}

// ntp_server_port (Read-Write)
uint16_t modem_config_get_ntp_port(void)
{
	return modem_config->ntp_server_port;
}

void modem_config_set_ntp_port(uint16_t port)
{
	modem_config->ntp_server_port = port;
}

// time (Read-Write)
uint32_t modem_config_get_time(void)
{
	return modem_config->time;
}

void modem_config_set_time(uint32_t time)
{
	modem_config->time = time;
}

// time_zone (Read-Write)
int32_t modem_config_get_timezone(void)
{
	return modem_config->time_zone;
}

void modem_config_set_timezone(int32_t timezone)
{
	modem_config->time_zone = timezone;
}

// coordinates (Read-Only)
const coordinates_t* modem_config_get_coordinates(void)
{
	return &modem_config->coordinates;
}

void modem_config_set_coordinates(const coordinates_t* coordinates)
{
	if(coordinates) {
		strncpy(modem_config->coordinates.mcc, coordinates->mcc, sizeof(modem_config->coordinates.mcc) - 1);
		strncpy(modem_config->coordinates.mnc, coordinates->mnc, sizeof(modem_config->coordinates.mnc) - 1);
		strncpy(modem_config->coordinates.lac, coordinates->lac, sizeof(modem_config->coordinates.lac) - 1);
		strncpy(modem_config->coordinates.ci, coordinates->ci, sizeof(modem_config->coordinates.ci) - 1);
		modem_config->coordinates.mcc[sizeof(modem_config->coordinates.mcc) - 1] = '\0';
		modem_config->coordinates.mnc[sizeof(modem_config->coordinates.mnc) - 1] = '\0';
		modem_config->coordinates.lac[sizeof(modem_config->coordinates.lac) - 1] = '\0';
		modem_config->coordinates.ci[sizeof(modem_config->coordinates.ci) - 1] = '\0';
	}
}

// production_date
uint32_t modem_config_get_production_date(void)
{
	return modem_config->production_date;
}

void modem_config_set_production_date(uint32_t date)
{
	modem_config->production_date = date;
}

// lifetime
uint32_t modem_config_get_lifetime(void)
{
	return modem_config->lifetime;
}

void modem_config_set_lifetime(uint32_t lifetime)
{
	modem_config->lifetime = lifetime;
}

// periodic_modem_reset_period (Read-Write)
uint32_t modem_config_get_reset_period(void)
{
	return modem_config->periodic_modem_reset_period;
}

void modem_config_set_reset_period(uint32_t period)
{
	modem_config->periodic_modem_reset_period = period;
}

// commissioning_time
uint32_t modem_config_get_commissioning_time(void)
{
	return modem_config->commissioning_time;
}

void modem_config_set_commissioning_time(uint32_t time)
{
	modem_config->commissioning_time = time;
}

// rf_firmware_version
const uint8_t* modem_config_get_rf_firmware_version(void)
{
	return modem_config->rf_firmware_version;
}

void modem_config_set_rf_firmware_version(const uint8_t* version)
{
	if(version) {
		memcpy(modem_config->rf_firmware_version, version, sizeof(modem_config->rf_firmware_version));
	}
}

// web session_counter
uint32_t modem_config_get_web_session_counter(void)
{
	return modem_config->web_session_counter;
}

void modem_config_set_web_session_counter(uint32_t counter)
{
	modem_config->web_session_counter = counter;
}

// iec session_counter
uint32_t modem_config_get_iec_session_counter(void)
{
	return modem_config->iec_session_counter;
}	

void modem_config_set_iec_session_counter(uint32_t counter)
{
	modem_config->iec_session_counter = counter;
}

void modem_config_get_simcard_phone_number(phone_number_t* phone_number)
{
	if(phone_number) {
		size_t copy_len = sizeof(phone_number->number) - 1U;

		if (copy_len > (sizeof(modem_config->phone_num) - 1U)) {
			copy_len = sizeof(modem_config->phone_num) - 1U;
		}

		memcpy(phone_number->number, modem_config->phone_num, copy_len);
		phone_number->number[sizeof(phone_number->number) - 1U] = '\0';
	}
}

void modem_config_set_simcard_phone_number(const phone_number_t* phone_number)
{
	if(phone_number) {
		size_t copy_len = sizeof(modem_config->phone_num) - 1U;

		if (copy_len > (sizeof(phone_number->number) - 1U)) {
			copy_len = sizeof(phone_number->number) - 1U;
		}

		memcpy(modem_config->phone_num, phone_number->number, copy_len);
		modem_config->phone_num[sizeof(modem_config->phone_num) - 1U] = '\0';
	}
}

void modem_config_set_imei(const char* imei)
{
	if(imei) {
		memcpy(modem_config->imei, imei, sizeof(modem_config->imei));
	}
}

const char* modem_config_get_imei(void)
{
	return (const char*)modem_config->imei;
}


















void modem_config_webserver_client_connected_cb(void)
{
	modem_config->web_session_counter++;
}

void modem_config_iec_client_connected_cb(void)
{
	modem_config->iec_session_counter++;
}
