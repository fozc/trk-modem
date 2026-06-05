/*
 * utils.c
 *
 *  Created on: 29 Mar 2018
 *      Author: fozcan
 */

#include "utils.h"
#include <string.h>
//static const char *b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
//isgraph

    /* Test that a string with all the characters in the GSM 03.38 charset
     * are converted from UTF-8 to GSM and back to UTF-8 successfully.
     */
//static const char *s = "@£$¥èéùìòÇ\nØø\rÅåΔ_ΦΓΛΩΠΨΣΘΞÆæßÉ !\"#¤%&'()*+,-./0123456789:;<=>?¡ABCDEFGHIJKLMNOPQRSTUVWXYZÄÖÑÜ§¿abcdefghijklmnopqrstuvwxyzäöñüà";

/*
 * Source: str, destination: buff
 * Start: delim_f, End: delim_l
 * f:first, l:last
 *
 * str_substr(buff1, buff2, "+CSQ: ", "," , 2), max_len -> string len + '\0' olmali
 *
 * */
char *str_substr(const char *str, char *buff, char *delim_f, char *delim_l, uint16_t max_len)
{
	char *res = NULL;
	int32_t index1 = str_index_of(str, delim_f); //first
	if(index1 >= 0)
	{
		index1 += strlen(delim_f);
		int32_t index2 = str_index_of((str + index1), delim_l); //last
		if(index2 > -1)
		{
			index2 += index1;
			int32_t len = index2 - index1;
			if(index2 > -1 && (len > -1 && len < max_len))
			{
				memcpy(buff, &str[index1], len);
				buff[len] = 0;
				res = (char *)&str[index1 + len + strlen(delim_l)];
			}
			else
			{
				buff[0] = 0;
			}
		}
	}
	return res;
}

int32_t str_index_of_s(const char *source, const char *sub, uint32_t len)
{
	const char *a, *b, *s;
	s = source;
	b = sub;

    if (*sub == 0) {
	return -1;
    }

	for(int i = 0; i < len; i++, source++)
	{
		if (*source != *b) {
		    continue;
		}
		a = source;
		while (1) {
		    if (*b == 0) {
			return (source - s);
		    }
		    if (*a++ != *b++) {
			break;
		    }
		}
		b = sub;
	}

	return -1;
}

int32_t str_index_of(const char *source, const char *sub)
{
	int32_t pos;
	char *ptr = strstr(source, sub);
	if(ptr)
		pos = ptr - source;
	else
		pos = -1;
	return pos;
}

int32_t str_index_of_th(const char *source, const char *cr, uint32_t th)
{
	int32_t pos;
	const char *ptr = source;
	while(th--){
		ptr = strstr(ptr, cr);
		if(ptr)
			ptr++;
		else
			break;
	}
	if(th !=0 && ptr)
		pos = ptr - source -1;
	else
		pos = -1;
	return pos;
}

int32_t str_end_withs(const char *source, const char *str)
{
	int32_t len1 = strlen(source);
	int32_t len2 = strlen(str);

	if(len1 < len2)
		return -1;
	return strstr((source + len1 - len2), str) ? 1 : 0;
}

int32_t str_start_withs(const char *source, const char *prefix)
{
	while(*prefix)
	{
		if(*source++ != *prefix++)
			return 0;
	}
	return 1;
}

void str_toupper(char *source, uint32_t len)
{
	for(int i = 0; i < len; i++)
	{
		if(*source >= 'a' && *source <= 'z')
		{
			*source -= 32; //('a' - 'A')
		}
		source++;
	}
}

void str_tolower(char *source, uint32_t len)
{
	for(int i = 0; i < len; i++)
	{
		if(*source >= 'A' && *source <= 'Z')
		{
			*source += 32;
		}
		source++;
	}
}

int32_t str_count(const char *str, uint32_t str_len, char token)
{
	int32_t counter = 0;
	for(int i = 0; i < str_len && str; i++)
	{
		if(*str++ == token)
		{
			counter++;
		}
	}

	return counter ? counter: -1;
}

int32_t str_len(const char * str, uint32_t max_len)
{
	int32_t len = 0;
	for(int i = 0; i < max_len; i++)
	{
		if(*str++ == 0x00)
		{
			len = i;
			break;
		}
	}

	return len;
}

int32_t str_cpy(char *target, char *source, uint32_t max_len)
{
	int32_t len = str_len(source, max_len);
	memcpy(target, source, len);
	target[len] = 0;
	return len;
}

void bit_set(uint8_t *data, uint8_t bit_no, uint8_t bit_val)
{
	if(bit_val)
	{
		*data |= 1UL << bit_no;
	}
	else
	{
		*data &= ~(1UL << bit_no);
	}
}

void bit_toogle(uint8_t *data, uint8_t bit_no)
{
	*data ^= 1UL << bit_no;
}

uint8_t bit_get(uint8_t data, uint8_t bit_no)
{
	return (data >> bit_no) & 1U;
}

uint32_t is_digit(uint8_t c)
{
	return ((c >= '0') && (c <= '9'));
}

uint32_t is_character(uint8_t c)
{
	return ((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z'));
}
/* alphanumeric  character */
uint32_t is_alnum(uint8_t c)
{
	return (is_character(c) || is_digit(c));
}

uint8_t calculate_bcc(uint8_t *buff, uint16_t len)
{
	uint8_t bcc = 0;
	while(len--)
		bcc ^= *buff++;
	return bcc;
}

void hex_to_ascii(uint8_t hex, uint8_t *buff)
{
	buff[1] = (hex % 10) + '0';
	buff[0] = (hex / 10) + '0';
}

uint32_t num_of_digit(uint32_t x)
{
    if(x>=1000000000) return 10;
    if(x>=100000000) return 9;
    if(x>=10000000) return 8;
    if(x>=1000000) return 7;
    if(x>=100000) return 6;
    if(x>=10000) return 5;
    if(x>=1000) return 4;
    if(x>=100) return 3;
    if(x>=10) return 2;
    return 1;
}

uint32_t my_strlen(uint8_t *buff, uint32_t max_len)
{
	uint32_t len = 0;
	while(*buff++)
	{
		len++;
		if(len >= max_len)
		{
			len = 0;
			break;
		}
	}
	return len;
}

void my_itoa(uint32_t num, uint8_t *str)
{
	if(num == 0)
	{
		*str++ = '0';
		*str = 0;
		return;
	}
	++str;
	*str = 0;
	while(num)
	{
		*--str = '0' + num % 10;
		num /= 10;
	}
}

uint32_t str_to_int(const uint8_t *buff, uint8_t len, void *dec, uint8_t type, uint8_t is_hexstr)
{
	uint32_t integer = 0, multiplier = 1, i = 10;

	if(len == 0)
		return 0;
	if(is_hexstr)
		i = 16;
	buff += len - 1;
	while(len--)
	{
		uint8_t cr = *buff--;

		if(cr >= '0' && cr <= '9')
		{
			cr -= '0';
		}
		else if(is_hexstr)
		{
			if(cr >= 'A' && cr <='F')
			{
				cr = cr - 'A' + 10;
			}
			else if(cr >= 'a' && cr <='f')
			{
				cr = cr - 'a' + 10;
			}
			else
			{
				return 0;
			}
		}
		else
		{
			return 0;
		}
		integer += cr * multiplier;
		multiplier *= i;
	}
	if(type == 0) /* uint32_t */
	{
		uint32_t *ptr = (uint32_t *)dec;
		*ptr = integer;
	}
	else if(type == 1) /* uint16_t */
	{
		uint16_t *ptr = (uint16_t *)dec;
		*ptr = (uint16_t)integer;
	}
	else if(type == 2) /* uint8_t */
	{
		uint8_t *ptr = (uint8_t *)dec;
		*ptr = (uint8_t)integer;
	}
	else
		return 0;
	return 1;
}


void dec_to_str(uint32_t num, uint8_t *str, uint32_t len)
{
	if(num == 0)
	{
		while(len--)
			*str++ = '0';
		*str = 0;
		return;
	}
	str += len + 1;
//	*str = 0;
	while(len--)
	{
		*--str = '0' + num % 10;
		num /= 10;
	}
}

// 255.255.255.255, 10.235.1.123, 010.255.007.052
uint32_t ipv4_to_int(const char *ip_str, uint32_t *ip_int) //__attribute__((optimize("-O3")))
{
	uint32_t res = 0;
	uint32_t temp, digit_count = 0;
	uint32_t ip_addr = 0;

	for(int i = 0; i < 4; i++)
	{
		digit_count = 0;
		for(int j = 0; j < 4; j++)
		{
			if(ip_str[j] == '.' || ip_str[j] == '\0')
			{
				digit_count = j;
				break;
			}
		}

		if(digit_count)
		{
			temp = 0;
			while(digit_count--)
			{
				if(is_digit(*ip_str))
				{
					temp = (temp * 10) + (*ip_str++ - '0');
				}
				else
				{
					i = 10;
					res = 1;
					break;
				}
			}
			ip_addr = (ip_addr << 8) | temp;
			ip_str++; //noktayi atladik
		}
		else
		{
			res = 1;
			break;
		}
	}
	if(!res)
		*ip_int = ip_addr;
	return res;
}

uint32_t ipv4_to_str(uint32_t ip_int, uint8_t *ip_str)
{
	uint32_t res = 1;
//	for(int i = 0; i < 4; i++)
//		*ip_str++ = (ip_int >> (i * 8)) & 0xFF;

	for(int i = 3; i >= 0; i--)
	{
		uint32_t dec = (ip_int >> (i * 8)) & 0x000000FF;

		*ip_str++ = (dec / 100) + 48;
		*ip_str++ = ((dec % 100) / 10) + 48;
		*ip_str++ = (dec % 10) + 48;

		*ip_str++ = '.';
	}
	*--ip_str = '\0';

	return res;
}

uint32_t str_is_alphanum(const uint8_t *str, uint32_t max_len)
{
	uint32_t res = 0;
	uint32_t len = str_len((char *)str, max_len);
	for(int i = 0; i < len; i++)
	{
		if(!is_alnum(*str++))
		{
			res = 1;
			break;
		}
	}

	return res;
}

int32_t str_str_is_printable(const char*str, uint16_t len)
{
	int32_t res = 1;
	while(len--)
	{
		if((*str >= 0x20 && *str <= 0x7e))
		{
			res = -1;
			break;
		}
		str++;
	}
	return res;
}

uint32_t my_pow(uint32_t base, uint32_t power)
{
	uint32_t res = 1;
	uint32_t i = 1;

	if(base == 0)
	{
		return res;
	}
	if(power == 0)
	{
		return 1;
	}
	while(i++ <= power)
	{
		res *= base;

	}
	return res;
}

