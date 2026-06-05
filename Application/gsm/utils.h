/*
 * utils.h
 *
 *  Created on: 29 Mar 2018
 *      Author: fozcan
 */

#ifndef UTILS_H_
#define UTILS_H_

#include <gsm_types.h>

/* From Chromium. */
#define ARRAY_SIZE(x) \
    ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))

#define OFFSET(x, y)	&(((x*)0)->y)

#define IS_INDEXABLE(arg) (sizeof(arg[0]))
#define IS_ARRAY(arg) (IS_INDEXABLE(arg) && (((void *) &arg) == ((void *) arg)))
#define ARRAYSIZE(arr) (IS_ARRAY(arr) ? (sizeof(arr) / sizeof(arr[0])) : 0)


/* a=target variable, b=bit number to act upon 0-n */
#define BIT_SET(a,b)   ((a) |= (1ULL<<(b)))
#define BIT_CLEAR(a,b) ((a) &= ~(1ULL<<(b)))
#define BIT_FLIP(a,b)  ((a) ^= (1ULL<<(b)))
#define BIT_CHECK(a,b) (!!((a) & (1ULL<<(b))))        // '!!' to make sure this returns 0 or 1

/* x=target variable, y=mask */
#define BITMASK_SET(x,y) ((x) |= (y))
#define BITMASK_CLEAR(x,y) ((x) &= (~(y)))
#define BITMASK_FLIP(x,y) ((x) ^= (y))
#define BITMASK_CHECK_ALL(x,y) (((x) & (y)) == (y))   // warning: evaluates y twice
#define BITMASK_CHECK_ANY(x,y) ((x) & (y))


/*
 *
 // https://en.cppreference.com/w/cpp/types/integer
 // http://www.pixelbeat.org/programming/gcc/format_specs.html
 hhu, hhx, hhd, hho, hx, ho, hd, d, u, lu, ld, llu ...

%hh   -   char
%d    -   int
%u    -   unsigned
%h    -   short unsigned int
%ld   -   long
%lld  -   long long
%lu   -   unsigned long
%llu  -   unsigned long long

 * */

//static const char tokens[256] = {
///*   0 nul    1 soh    2 stx    3 etx    4 eot    5 enq    6 ack    7 bel  */
//        0,       0,       0,       0,       0,       0,       0,       0,
///*   8 bs     9 ht    10 nl    11 vt    12 np    13 cr    14 so    15 si   */
//        0,       0,       0,       0,       0,       0,       0,       0,
///*  16 dle   17 dc1   18 dc2   19 dc3   20 dc4   21 nak   22 syn   23 etb */
//        0,       0,       0,       0,       0,       0,       0,       0,
///*  24 can   25 em    26 sub   27 esc   28 fs    29 gs    30 rs    31 us  */
//        0,       0,       0,       0,       0,       0,       0,       0,
///*  32 sp    33  !    34  "    35  #    36  $    37  %    38  &    39  '  */
//        0,      '!',      0,      '#',     '$',     '%',     '&',    '\'',
///*  40  (    41  )    42  *    43  +    44  ,    45  -    46  .    47  /  */
//        0,       0,      '*',     '+',      0,      '-',     '.',      0,
///*  48  0    49  1    50  2    51  3    52  4    53  5    54  6    55  7  */
//       '0',     '1',     '2',     '3',     '4',     '5',     '6',     '7',
///*  56  8    57  9    58  :    59  ;    60  <    61  =    62  >    63  ?  */
//       '8',     '9',      0,       0,       0,       0,       0,       0,
///*  64  @    65  A    66  B    67  C    68  D    69  E    70  F    71  G  */
//        0,      'a',     'b',     'c',     'd',     'e',     'f',     'g',
///*  72  H    73  I    74  J    75  K    76  L    77  M    78  N    79  O  */
//       'h',     'i',     'j',     'k',     'l',     'm',     'n',     'o',
///*  80  P    81  Q    82  R    83  S    84  T    85  U    86  V    87  W  */
//       'p',     'q',     'r',     's',     't',     'u',     'v',     'w',
///*  88  X    89  Y    90  Z    91  [    92  \    93  ]    94  ^    95  _  */
//       'x',     'y',     'z',      0,       0,       0,      '^',     '_',
///*  96  `    97  a    98  b    99  c   100  d   101  e   102  f   103  g  */
//       '`',     'a',     'b',     'c',     'd',     'e',     'f',     'g',
///* 104  h   105  i   106  j   107  k   108  l   109  m   110  n   111  o  */
//       'h',     'i',     'j',     'k',     'l',     'm',     'n',     'o',
///* 112  p   113  q   114  r   115  s   116  t   117  u   118  v   119  w  */
//       'p',     'q',     'r',     's',     't',     'u',     'v',     'w',
///* 120  x   121  y   122  z   123  {   124  |   125  }   126  ~   127 del */
//       'x',     'y',     'z',      0,      '|',      0,      '~',       0 };

char *str_substr(const char *str, char *buff, char *delim_f, char *delim_l, uint16_t max_len);
int32_t  str_index_of_s	(const char *source, const char *sub, uint32_t len);
int32_t  str_index_of	(const char *source, const char *sub);
int32_t  str_index_of_th (const char *source, const char *cr, uint32_t th);
int32_t  str_start_withs (const char *source, const char *prefix);
int32_t  str_end_withs	(const char *source, const char *str);
void str_toupper	(char *source, uint32_t len);
void str_tolower	(char *source, uint32_t len);
int32_t  str_count		(const char *str, uint32_t str_len, char token);
int32_t  str_len		(const char * str, uint32_t max_len);
int32_t  str_cpy		(char *target, char *source, uint32_t max_len);
uint32_t  is_digit		(uint8_t c);
uint32_t  is_character	(uint8_t c);
uint32_t  is_alnum		(uint8_t c);
uint32_t  str_is_alphanum(const uint8_t *str, uint32_t len);
uint8_t   calculate_bcc	(uint8_t *buff, uint16_t len);
void hex_to_ascii	(uint8_t hex, uint8_t *buff);
uint32_t  num_of_digit	(uint32_t num);
uint32_t  str_to_int		(const uint8_t *buff, uint8_t len, void *dec, uint8_t type, uint8_t is_hexstr);
uint32_t  my_strlen		(uint8_t *buff, uint32_t max_len);
void my_itoa		(uint32_t num, uint8_t *str);
void dec_to_str		(uint32_t num, uint8_t *str, uint32_t len);
uint32_t  ipv4_to_int	(const char *ip_str, uint32_t *ip_int);
uint32_t  ipv4_to_str	(uint32_t ip_int, uint8_t *ip_str);
int32_t  str_str_is_printable	(const char*str, uint16_t len);

uint32_t my_pow(uint32_t base, uint32_t power);

#endif /* UTILS_H_ */

