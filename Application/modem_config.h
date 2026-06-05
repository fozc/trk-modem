/*
 * config_manager.h
 *
 *  Created on: Feb 1, 2026
 *      Author: fatih
 */

#ifndef MODEM_CONFIG_H_
#define MODEM_CONFIG_H_

#include "types.h"

// Basic Functions
int modem_config_sync(void);
const modem_config_t* modem_config_get(void);
void modem_config_set(const modem_config_t* config);
bool modem_config_is_ntp_active(void);


// serial_number  
void modem_config_set_serial_number(uint32_t serial_number);
uint32_t modem_config_get_serial_number(void);

// web_interface_port (Read-Write)
uint16_t modem_config_get_web_port(void);
void modem_config_set_web_port(uint16_t port);

// sim_card_pin (Read-Write)
uint16_t modem_config_get_sim_pin(void);
void modem_config_set_sim_pin(uint16_t pin);

// sim_card_apn (Read-Write)
const char* modem_config_get_sim_apn(void);
void modem_config_set_sim_apn(const char* apn);

// sim_card_apn_username (Read-Write)
const char* modem_config_get_sim_apn_username(void);
void modem_config_set_sim_apn_username(const char* username);

// sim_card_apn_password (Read-Write)
const char* modem_config_get_sim_apn_password(void);
void modem_config_set_sim_apn_password(const char* password);

// ntp_server (Read-Write)
const char* modem_config_get_ntp_server(void);
void modem_config_set_ntp_server(const char* server);

// ntp_server_port (Read-Write)
uint16_t modem_config_get_ntp_port(void);
void modem_config_set_ntp_port(uint16_t port);

// time (Read-Write)
uint32_t modem_config_get_time(void);
void modem_config_set_time(uint32_t time);

// time_zone (Read-Write)
int32_t modem_config_get_timezone(void);
void modem_config_set_timezone(int32_t timezone);

// coordinates (Read-Only for get, but has setter)
const coordinates_t* modem_config_get_coordinates(void);
void modem_config_set_coordinates(const coordinates_t* coordinates);

// production_date
uint32_t modem_config_get_production_date(void);
void modem_config_set_production_date(uint32_t date);

// lifetime
uint32_t modem_config_get_lifetime(void);
void modem_config_set_lifetime(uint32_t lifetime);

// periodic_modem_reset_period (Read-Write)
uint32_t modem_config_get_reset_period(void);
void modem_config_set_reset_period(uint32_t period);

// commissioning_time
uint32_t modem_config_get_commissioning_time(void);
void modem_config_set_commissioning_time(uint32_t time);

// rf_firmware_version
const uint8_t* modem_config_get_rf_firmware_version(void);
void modem_config_set_rf_firmware_version(const uint8_t* version);

// web_session_counter
uint32_t modem_config_get_web_session_counter(void);
void modem_config_set_web_session_counter(uint32_t counter);

// iec_session_counter
uint32_t modem_config_get_iec_session_counter(void);
void modem_config_set_iec_session_counter(uint32_t counter);
void modem_config_get_simcard_phone_number(phone_number_t* phone_number);
void modem_config_set_simcard_phone_number(const phone_number_t* phone_number);

void modem_config_set_imei(const char* imei);
const char* modem_config_get_imei(void);


//Callbacks

void modem_config_webserver_client_connected_cb(void);
void modem_config_iec_client_connected_cb(void);

#endif /* MODEM_CONFIG_H_ */
