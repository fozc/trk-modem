/*
 * utils.h
 *
 *  Created on: 29 Agu 2022
 *      Author: fatih
 */

#ifndef UTILS_H_
#define UTILS_H_

#include <stddef.h>
#include <stdint.h>
//#include <types.h>

//typedef struct __attribute__((__packed__))
//{
//	uint8_t year;
//	uint8_t month;
//	uint8_t day;
//}date_t;
//
//typedef struct __attribute__((__packed__))
//{
//	uint8_t hour;
//	uint8_t minute;
//	uint8_t second;
//	uint16_t milli;
//}timee_t;
//
//typedef struct __attribute__((__packed__))
//{
//	date_t date;
//	timee_t time;
//}datetime_t;

#define ONE_MIN_SECONDS   (60)
#define ONE_HOUR_SECONDS  (ONE_MIN_SECONDS*ONE_MIN_SECONDS)
#define ONE_DAY_SECONDS   (24*ONE_HOUR_SECONDS)
#define ONE_MONTH_SECONDS (30*ONE_DAY_SECONDS)
#define ONE_YEAR_SECONDS  (12*ONE_MONTH_SECONDS)

#define ARRAY_SIZE(x) (sizeof((x)) / sizeof((x)[0]))

int is_printable(int c);
int is_numeric(const char *str);

const char * contiki_event_to_str(uint8_t event);


int xstrtoi(const char *str);
float xstrtof(const char *str);


int is_hex(int c);
int is_ascii_hex(const void *data, uint16_t length);
uint32_t ascii_hex_to_integer(const void *data, uint8_t len);
int is_contains(const void *main_block, size_t main_block_size,
		const void *sub_block, size_t sub_block_size);

int is_valid_mac_address(const char* mac);
void hex_dump(const void* data, size_t num_bytes);

uint8_t hex_char_to_val(char c);
uint8_t ascii_hex_to_byte(const void *input);
void byte_to_ascii_hex(uint8_t byte, void *output);

uint32_t count_set_bits(uint32_t v);
#endif /* UTILS_H_ */
