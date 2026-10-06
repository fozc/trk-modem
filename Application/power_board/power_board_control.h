/*
 * power_board_control.h
 *
 *  Created on: Oct 6, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * SCP power-board settings, command results and raw diagnostics.
 */

#ifndef POWER_BOARD_CONTROL_H
#define POWER_BOARD_CONTROL_H

#include "rf_scp_codec.h"

#define POWER_SETTING_RATE      0x0200U
#define POWER_SETTING_CAPACITY  0x2000U
#define POWER_SETTING_PERIOD    0x4000U

typedef enum
{
    POWER_SETTINGS_IDLE = 0,
    POWER_SETTINGS_READING,
    POWER_SETTINGS_WRITING,
    POWER_SETTINGS_WAIT_ECHO,
    POWER_SETTINGS_WAIT_APPLY,
    POWER_SETTINGS_ECHO_ACCEPTED,
    POWER_SETTINGS_APPLIED,
    POWER_SETTINGS_REJECTED,
    POWER_SETTINGS_CONFLICT,
    POWER_SETTINGS_UNCERTAIN,
    POWER_SETTINGS_RESTARTED
} power_settings_state_t;

typedef enum
{
    POWER_COMMAND_IDLE = 0,
    POWER_COMMAND_SENDING,
    POWER_COMMAND_ACCEPTED,
    POWER_COMMAND_MONITORING,
    POWER_COMMAND_APPLIED,
    POWER_COMMAND_REJECTED,
    POWER_COMMAND_UNCERTAIN,
    POWER_COMMAND_RESTARTED
} power_command_state_t;

typedef struct
{
    uint16_t mask;
    uint8_t rate_permille;
    uint8_t capacity_ah;
    uint8_t period_sec;
} power_settings_request_t;

typedef struct
{
    power_settings_state_t settings_state;
    bool has_config;
    uint8_t config[16];
    bool echo_valid;
    uint8_t echo_gen;
    uint8_t mask1;
    uint8_t mask2;
    uint16_t last_rejected;
    uint8_t written_gen;
    power_command_state_t command_state;
    bool has_command_ack;
    uint8_t command_seq;
    bool has_result;
    uint8_t result_command;
    uint8_t result_seq;
    uint8_t result;
    uint8_t transmissions;
    uint8_t command_flags;
    bool has_raw;
    uint8_t raw[96];
    uint8_t error_code;
    uint16_t rejected_fields;
} power_board_control_status_t;

void power_board_control_init(void);
void power_board_control_hub_restarted(void);
void power_board_control_process(uint32_t now_ms);
bool power_board_control_get_status(power_board_control_status_t *out);
bool power_board_read_settings(void);
bool power_board_write_settings(const power_settings_request_t *request);
bool power_board_battery_replaced(void);
bool power_board_cancel_command(void);
bool power_board_read_command_result(void);
bool power_board_read_raw(void);
bool power_board_handle_command_result(const rf_scp_message_t *message);
bool power_board_handle_raw(const rf_scp_message_t *message);
int power_board_control_shell(int argc, char **argv);

#endif /* POWER_BOARD_CONTROL_H */

/*** end of file ***/
